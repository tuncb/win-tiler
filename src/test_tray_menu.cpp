#ifndef DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>

#include "config_file.h"
#include "engine.h"
#include "tray_menu.h"
#include "winapi.h"

namespace {
using namespace wintiler;

struct TrayConfigFixture {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("win-tiler-tray-" + std::to_string(GetCurrentProcessId()) + "-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".toml");
  explicit TrayConfigFixture(std::string_view text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
  }
  ~TrayConfigFixture() {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
  std::string read() const {
    auto text = read_config_text(path);
    REQUIRE(text.has_value());
    return *text;
  }
};

MENUITEMINFOW menu_item(HMENU menu, unsigned int id) {
  MENUITEMINFOW info{sizeof(MENUITEMINFOW)};
  info.fMask = MIIM_STATE | MIIM_FTYPE | MIIM_DATA;
  REQUIRE(GetMenuItemInfoW(menu, id, FALSE, &info) != FALSE);
  return info;
}

struct MenuOwner {
  HWND hwnd = CreateWindowExW(0, L"STATIC", L"Tray menu test", 0, 0, 0, 0, 0, nullptr, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
  ~MenuOwner() {
    if (hwnd && !DestroyWindow(hwnd))
      spdlog::error("Failed to destroy menu test window: {}", GetLastError());
  }
};
} // namespace

TEST_SUITE("Balanced tray menu") {
  TEST_CASE("main menu prioritizes tiling and groups maintenance") {
    MenuOwner owner;
    REQUIRE(owner.hwnd != nullptr);
    ui::PopupMenu menu(owner.hwnd, ui::ThemeSurface::System);
    TrayMenuState state;
    state.has_desktop = state.has_config = true;
    state.movement_mode = MovementMode::Insert;
    state.layout.split_target = LayoutSplitTarget::LargestAllMonitors;
    state.layout.split_mode = LayoutSplitMode::Horizontal;
    state.render.show_only_active_window = true;
    state.verbose_logging = true;
    populate_tray_menu(menu, state);
    REQUIRE(menu.ok);
    std::vector<std::wstring> labels;
    for (int i = 0; i < GetMenuItemCount(menu.handle); ++i) {
      MENUITEMINFOW info{sizeof(MENUITEMINFOW)};
      info.fMask = MIIM_DATA;
      REQUIRE(GetMenuItemInfoW(menu.handle, i, TRUE, &info) != FALSE);
      const auto* item = reinterpret_cast<ui::MenuItem*>(info.dwItemData);
      if (!item->separator)
        labels.push_back(item->label);
    }
    CHECK(labels == std::vector<std::wstring>{L"Pause tiling", L"Rebuild layout", L"New windows",
                                              L"Keyboard movement: Insert",
                                              L"Window outlines: Active only",
                                              L"Ignored windows...", L"Edit configuration...",
                                              L"Tools & help", L"Exit win-tiler"});
    for (auto id :
         {ID_MOVEMENT_INSERT, ID_PLACE_ALL_MONITORS, ID_SPLIT_HORIZONTAL, ID_OUTLINES_ACTIVE}) {
      auto info = menu_item(menu.handle, id);
      CHECK((info.fState & MFS_CHECKED) != 0);
      CHECK((info.fType & MFT_RADIOCHECK) != 0);
    }
    for (auto id : {ID_MOVEMENT_SWAP, ID_PLACE_POINTER, ID_SPLIT_AUTO, ID_OUTLINES_OFF})
      CHECK((menu_item(menu.handle, id).fState & MFS_CHECKED) == 0);
    CHECK((menu_item(menu.handle, ID_SHOW_LOG).fState & MFS_DISABLED) != 0);
    CHECK((menu_item(menu.handle, ID_DUMP_DIAGNOSTICS).fState & MFS_DISABLED) != 0);
    CHECK((menu_item(menu.handle, ID_TOGGLE_VERBOSE_LOGGING).fState & MFS_CHECKED) != 0);
    CHECK((menu_item(menu.handle, ID_TOGGLE_VERBOSE_LOGGING).fType & MFT_RADIOCHECK) == 0);
  }

  TEST_CASE("paused or unavailable layouts disable transient actions but retain preferences") {
    MenuOwner owner;
    REQUIRE(owner.hwnd != nullptr);
    TrayMenuState state;
    SUBCASE("paused") {
      state.paused = state.has_desktop = true;
    }
    SUBCASE("no desktop") {
      state.has_desktop = false;
    }
    ui::PopupMenu menu(owner.hwnd);
    populate_tray_menu(menu, state);
    REQUIRE(menu.ok);
    for (auto id : {ID_RESET, ID_MOVEMENT_SWAP, ID_MOVEMENT_INSERT})
      CHECK((menu_item(menu.handle, id).fState & MFS_DISABLED) != 0);
    for (auto id : {ID_PLACE_FOCUSED, ID_SPLIT_VERTICAL, ID_OUTLINES_ALL, ID_OPEN_CONFIG, ID_EXIT})
      CHECK((menu_item(menu.handle, id).fState & MFS_DISABLED) == 0);
    auto* config =
        reinterpret_cast<ui::MenuItem*>(menu_item(menu.handle, ID_OPEN_CONFIG).dwItemData);
    CHECK(config->label == L"Create configuration...");
    auto* pause =
        reinterpret_cast<ui::MenuItem*>(menu_item(menu.handle, ID_TOGGLE_PAUSE).dwItemData);
    CHECK(pause->label == (state.paused ? L"Resume tiling" : L"Pause tiling"));
  }

  TEST_CASE("zero width outlines are shown as off") {
    renderer::RenderOptions options;
    options.border_width = 0;
    CHECK(std::wstring(tray_outlines_label(options)) == L"Window outlines: Off");
    MenuOwner owner;
    ui::PopupMenu menu(owner.hwnd);
    TrayMenuState state;
    state.render = options;
    populate_tray_menu(menu, state);
    CHECK((menu_item(menu.handle, ID_OUTLINES_OFF).fState & MFS_CHECKED) != 0);
    CHECK((menu_item(menu.handle, ID_OUTLINES_ALL).fState & MFS_CHECKED) == 0);
  }

  TEST_CASE("explicit movement requests are consumed once without toggling") {
    winapi::request_notification_area_movement_mode(MovementMode::Insert);
    CHECK(winapi::consume_notification_area_movement_mode_request() == MovementMode::Insert);
    CHECK_FALSE(winapi::consume_notification_area_movement_mode_request());
    CHECK(tray_movement_choice(ID_MOVEMENT_SWAP) == MovementMode::Swap);
    CHECK(tray_movement_choice(ID_MOVEMENT_INSERT) == MovementMode::Insert);
    CHECK_FALSE(tray_movement_choice(ID_OUTLINES_ALL));
  }
}

TEST_SUITE("Tray settings persistence") {
  TEST_CASE("placement and split choices round-trip without replacing other settings") {
    TrayConfigFixture file(
        "# keep me\n[gap]\nhorizontal = 17\n[keyboard]\nmovement_mode = 'insert'\n");
    for (const auto& [id, value] :
         {std::pair{ID_PLACE_POINTER, LayoutSplitTarget::Pointer},
          std::pair{ID_PLACE_FOCUSED, LayoutSplitTarget::Focused},
          std::pair{ID_PLACE_LARGEST, LayoutSplitTarget::Largest},
          std::pair{ID_PLACE_ALL_MONITORS, LayoutSplitTarget::LargestAllMonitors}}) {
      REQUIRE(save_tray_setting(file.path, id).has_value());
      auto options = read_options_toml(file.path);
      REQUIRE(options.has_value());
      CHECK(options->layoutOptions.split_target == value);
      CHECK(options->gapOptions.horizontal == 17);
      CHECK(options->keyboardOptions.movement_mode == MovementMode::Insert);
    }
    for (const auto& [id, value] : {std::pair{ID_SPLIT_AUTO, LayoutSplitMode::Dwindle},
                                    std::pair{ID_SPLIT_VERTICAL, LayoutSplitMode::Vertical},
                                    std::pair{ID_SPLIT_HORIZONTAL, LayoutSplitMode::Horizontal}}) {
      REQUIRE(save_tray_setting(file.path, id).has_value());
      CHECK(tray_split_choice(id) == value);
      auto options = read_options_toml(file.path);
      REQUIRE(options.has_value());
      CHECK(options->layoutOptions.split_mode == value);
      CHECK(options->layoutOptions.split_target == LayoutSplitTarget::LargestAllMonitors);
    }
    CHECK(file.read().find("# keep me") != std::string::npos);
    CHECK(file.read().find("movement_mode = 'insert'") != std::string::npos);
  }

  TEST_CASE("editing values preserves comments Unicode and alternate TOML representations") {
    std::string original;
    SUBCASE("explicit table and CRLF") {
      original = "# café\r\n[layout] # directions\r\nsplit_mode = 'vertical' # keep\r\n";
    }
    SUBCASE("quoted keys") {
      original = "['layout']\n'split_mode' = 'vertical' # keep\n";
    }
    SUBCASE("dotted key") {
      original = "layout.split_mode = 'vertical' # keep\n";
    }
    SUBCASE("inline table after Unicode") {
      original = "layout = { note = '東京', split_mode = 'vertical' } # keep\n";
    }
    SUBCASE("UTF-8 BOM") {
      original = "\xef\xbb\xbf[layout]\nsplit_mode = 'vertical' # keep\n";
    }
    TrayConfigFixture file(original);
    REQUIRE(save_tray_setting(file.path, ID_SPLIT_HORIZONTAL).has_value());
    original.replace(original.find("'vertical'"), 10, "\"horizontal\"");
    CHECK(file.read() == original);
  }

  TEST_CASE("missing nested settings are added without losing existing tables") {
    std::string original;
    SUBCASE("empty") {
      original = "# keep\n";
    }
    SUBCASE("explicit parent") {
      original = "[visualization]\ntoast_duration_ms = 4321\n";
    }
    SUBCASE("explicit child") {
      original = "[visualization.render] # keep\nborder_width = 7\n";
    }
    SUBCASE("implicit parent") {
      original = "[visualization.other]\ntext = 'keep'\n";
    }
    SUBCASE("inline parent") {
      original = "visualization = { toast_duration_ms = 4321 } # keep\n";
    }
    SUBCASE("inline child") {
      original = "visualization = { render = { border_width = 7 } } # keep\n";
    }
    SUBCASE("dotted child") {
      original = "visualization.render.border_width = 7 # keep\n";
    }
    TrayConfigFixture file(original);
    auto saved = save_tray_setting(file.path, ID_OUTLINES_ACTIVE);
    INFO((saved ? "" : saved.error()));
    REQUIRE(saved.has_value());
    auto options = read_options_toml(file.path);
    REQUIRE(options.has_value());
    CHECK(options->visualizationOptions.renderOptions.show_rectangles);
    CHECK(options->visualizationOptions.renderOptions.show_only_active_window);
    if (original.find("border_width") != std::string::npos)
      CHECK(options->visualizationOptions.renderOptions.border_width == 7);
    if (original.find("4321") != std::string::npos)
      CHECK(options->visualizationOptions.toastDurationMs == 4321);
    CHECK((file.read().find("keep") != std::string::npos ||
           original.find("keep") == std::string::npos));
  }

  TEST_CASE("outline choices preserve custom styling and suppression rules") {
    TrayConfigFixture file("[visualization.render]\nborder_width = 0 # keep\n"
                           "hide_rectangles_when_processes_open = ['ScreenShare.exe']\n");
    REQUIRE(save_tray_setting(file.path, ID_OUTLINES_ACTIVE).has_value());
    auto options = read_options_toml(file.path);
    REQUIRE(options.has_value());
    CHECK(options->visualizationOptions.renderOptions.border_width == kDefaultBorderWidth);
    CHECK(options->visualizationOptions.renderOptions.show_only_active_window);
    REQUIRE(save_tray_setting(file.path, ID_OUTLINES_OFF).has_value());
    options = read_options_toml(file.path);
    REQUIRE(options.has_value());
    CHECK_FALSE(options->visualizationOptions.renderOptions.show_rectangles);
    CHECK(options->visualizationOptions.renderOptions.show_only_active_window);
    REQUIRE(save_tray_setting(file.path, ID_OUTLINES_ALL).has_value());
    options = read_options_toml(file.path);
    REQUIRE(options.has_value());
    CHECK(options->visualizationOptions.renderOptions.show_rectangles);
    CHECK_FALSE(options->visualizationOptions.renderOptions.show_only_active_window);
    CHECK(options->visualizationOptions.renderOptions.hide_rectangles_when_processes_open ==
          std::vector<std::string>{"ScreenShare.exe"});
    CHECK(file.read().find("# keep") != std::string::npos);
  }

  TEST_CASE("first tray edit creates a documented config and can be adopted for reload") {
    TrayConfigFixture file("");
    REQUIRE(std::filesystem::remove(file.path));
    REQUIRE(save_tray_setting(file.path, ID_PLACE_FOCUSED).has_value());
    CHECK(file.read().find("# Layout options") != std::string::npos);
    GlobalOptionsProvider provider;
    REQUIRE(provider.reload_from(file.path).has_value());
    CHECK(provider.configPath == file.path);
    CHECK(provider.options.layoutOptions.split_target == LayoutSplitTarget::Focused);
    const auto original = file.read();
    REQUIRE(ensure_tray_config(file.path).has_value());
    CHECK(file.read() == original);
  }

  TEST_CASE("create configuration writes documented defaults without changing later edits") {
    TrayConfigFixture file("");
    REQUIRE(std::filesystem::remove(file.path));
    REQUIRE(ensure_tray_config(file.path).has_value());
    auto options = read_options_toml(file.path);
    REQUIRE(options.has_value());
    CHECK(options->layoutOptions.split_mode == LayoutSplitMode::Dwindle);
    CHECK(file.read().find("# Tray New windows choices") != std::string::npos);
    CHECK(file.read().find("# Tray Window outlines saves") != std::string::npos);
    REQUIRE(save_tray_setting(file.path, ID_SPLIT_VERTICAL).has_value());
    const auto saved = file.read();
    REQUIRE(ensure_tray_config(file.path).has_value());
    CHECK(file.read() == saved);
  }

  TEST_CASE("create configuration does not overwrite an existing invalid file") {
    TrayConfigFixture file("[broken");
    REQUIRE(ensure_tray_config(file.path).has_value());
    CHECK(file.read() == "[broken");
  }

  TEST_CASE("invalid or unwritable config leaves the original intact") {
    std::string original;
    SUBCASE("invalid TOML") {
      original = "[layout\n";
    }
    SUBCASE("parent has wrong type") {
      original = "layout = 7\n";
    }
    SUBCASE("value is a table") {
      original = "layout.split_mode = { custom = true }\n";
    }
    TrayConfigFixture file(original);
    CHECK_FALSE(save_tray_setting(file.path, ID_SPLIT_AUTO));
    CHECK(file.read() == original);
  }

  TEST_CASE("failed atomic replacement and stale edits never truncate the config") {
    TrayConfigFixture file("# original\n");
    CHECK_FALSE(write_config_text_atomically(file.path, "# new\n", "# stale\n"));
    CHECK_FALSE(write_config_text_atomically(file.path, "# new\n", std::nullopt, true));
    HANDLE locked = CreateFileW(file.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(locked != INVALID_HANDLE_VALUE);
    CHECK_FALSE(save_tray_setting(file.path, ID_PLACE_FOCUSED));
    CHECK(CloseHandle(locked) != FALSE);
    CHECK(file.read() == "# original\n");
  }
}

TEST_SUITE("Tray frame requests") {
  TEST_CASE("movement is explicit idempotent and local to the desktop session") {
    Engine first, second;
    const std::vector<ctrl::ClusterInitInfo> clusters = {{0, 0, 800, 600, 0, 0, 800, 600, {1, 2}}};
    first.init(clusters);
    second.init(clusters);
    EngineFrameInput input;
    input.cluster_updates = {{{1, 2}, false}};
    input.update_hover_selection = false;
    input.requested_movement_mode = MovementMode::Insert;
    CHECK(first.process_frame(input).movement_mode == MovementMode::Insert);
    CHECK(first.process_frame(input).movement_mode == MovementMode::Insert);
    input.requested_movement_mode.reset();
    CHECK(first.process_frame(input).movement_mode == MovementMode::Insert);
    CHECK(second.process_frame(input).movement_mode == MovementMode::Swap);
    input.initial_movement_mode = MovementMode::Insert;
    CHECK(first.process_frame(input).movement_mode == MovementMode::Insert);
    CHECK(second.process_frame(input).movement_mode == MovementMode::Insert);
    input.initial_movement_mode = MovementMode::Swap;
    CHECK(first.process_frame(input).movement_mode == MovementMode::Swap);
  }

  TEST_CASE("live split preferences affect new splits and retain the existing layout") {
    Engine engine;
    engine.init({{0, 0, 800, 600, 0, 0, 800, 600, {1, 2}}});
    LayoutOptions layout;
    EngineFrameInput input;
    input.layout_options = &layout;
    input.cluster_updates = {{{1, 2}, false}};
    input.update_hover_selection = false;
    input.has_completed_initial_tile_pass = true;
    auto before = engine.process_frame(input);
    CHECK(before.split_mode == LayoutSplitMode::Dwindle);
    layout.split_mode = LayoutSplitMode::Horizontal;
    auto output = engine.process_frame(input);
    CHECK(output.split_mode == LayoutSplitMode::Horizontal);
    CHECK_FALSE(output.layout_changed);
    CHECK(engine.system.clusters[0].tree[0].split_dir == ctrl::SplitDir::Vertical);
    input.cluster_updates = {{{1, 2, 3}, false}};
    output = engine.process_frame(input);
    REQUIRE(output.topology_changed);
    REQUIRE(engine.find_leaf(3).has_value());
    const auto leaf = *engine.find_leaf(3);
    const auto& tree = engine.system.clusters[static_cast<size_t>(leaf.cluster_index)].tree;
    REQUIRE(tree.get_parent(leaf.cell_index).has_value());
    CHECK(tree[*tree.get_parent(leaf.cell_index)].split_dir == ctrl::SplitDir::Horizontal);
  }

  TEST_CASE(
      "cycle shortcut survives unrelated reloads and an explicit choice restores the saved mode") {
    Engine engine;
    engine.init({{0, 0, 800, 600, 0, 0, 800, 600, {1, 2}}});
    LayoutOptions layout;
    EngineFrameInput input;
    input.layout_options = &layout;
    input.cluster_updates = {{{1, 2}, false}};
    input.hotkey_action = HotkeyAction::CycleSplitMode;
    CHECK(engine.process_frame(input).split_mode == LayoutSplitMode::Vertical);
    input.hotkey_action.reset();
    layout.split_target = LayoutSplitTarget::Focused;
    CHECK(engine.process_frame(input).split_mode == LayoutSplitMode::Vertical);
    input.requested_split_mode = LayoutSplitMode::Dwindle;
    CHECK(engine.process_frame(input).split_mode == LayoutSplitMode::Dwindle);
    input.requested_split_mode.reset();
    CHECK(engine.process_frame(input).split_mode == LayoutSplitMode::Dwindle);
  }
}
#endif
