#ifndef DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>

#include "ignore_config.h"
#include "ignore_dialog.h"
#include "ui_theme.h"

namespace {
struct RuleConfigFixture {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("win-tiler-rule-edit-" + std::to_string(GetCurrentProcessId()) + "-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".toml");
  explicit RuleConfigFixture(std::string_view text) {
    write(text);
  }
  ~RuleConfigFixture() {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
  void write(std::string_view text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
  }
  std::string read() const {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }
};
using namespace wintiler;
} // namespace

TEST_SUITE("Native ignore rule editing") {
  TEST_CASE("app-wide rule changes to one title in one config edit") {
    RuleConfigFixture file(
        "# settings\n[ignore] # keep header comment\nprocesses = [\"chrome.exe\"] # keep inline\n"
        "[gap]\nhorizontal = 17\n");
    IgnoreRule before{IgnoreRuleKind::Process, "CHROME.EXE", {}};
    IgnoreRule after{IgnoreRuleKind::ProcessTitlePair, "chrome.exe", "Project notes"};
    auto result = edit_ignore_rules(file.path, {before}, {after});
    REQUIRE(result.has_value());
    CHECK(result->removed.size() == 1);
    CHECK(result->added.size() == 1);
    auto rules = read_user_ignore_rules(file.path);
    REQUIRE(rules.has_value());
    REQUIRE(rules->size() == 1);
    CHECK(ignore_rule_matches(rules->front(), "chrome.exe", "Project notes", false));
    CHECK_FALSE(ignore_rule_matches(rules->front(), "chrome.exe", "Other tab", false));
    CHECK(file.read().find("# keep inline") != std::string::npos);
    CHECK(file.read().find("[gap]\nhorizontal = 17") != std::string::npos);
  }

  TEST_CASE("undo merges only its inverse edit into externally changed config") {
    RuleConfigFixture file("[ignore]\nprocesses = [\"app.exe\"]\n[gap]\nhorizontal = 5\n");
    IgnoreRule before{IgnoreRuleKind::Process, "app.exe", {}};
    IgnoreRule after{IgnoreRuleKind::Title, {}, "Tool"};
    auto changed = edit_ignore_rules(file.path, {before}, {after});
    REQUIRE(changed.has_value());
    std::string external = file.read();
    external.replace(external.find("horizontal = 5"), 14, "horizontal = 9");
    file.write(external);
    auto other = edit_ignore_rules(file.path, {}, {{IgnoreRuleKind::Process, "other.exe", {}}});
    REQUIRE(other.has_value());
    auto undone = edit_ignore_rules(file.path, changed->added, changed->removed);
    REQUIRE(undone.has_value());
    auto rules = read_user_ignore_rules(file.path);
    REQUIRE(rules.has_value());
    CHECK(rules->size() == 2);
    CHECK(file.read().find("horizontal = 9") != std::string::npos);
    CHECK(file.read().find("other.exe") != std::string::npos);
  }

  TEST_CASE("merging an existing target does not make undo remove that target") {
    RuleConfigFixture file("[ignore]\nprocesses = [\"app.exe\"]\nwindow_titles = [\"Tool\"]\n");
    auto result = edit_ignore_rules(file.path, {{IgnoreRuleKind::Process, "app.exe", {}}},
                                    {{IgnoreRuleKind::Title, {}, "Tool"}});
    REQUIRE(result.has_value());
    CHECK(result->added.empty());
    CHECK(result->removed.size() == 1);
    auto undo = edit_ignore_rules(file.path, result->added, result->removed);
    REQUIRE(undo.has_value());
    auto rules = read_user_ignore_rules(file.path);
    REQUIRE(rules.has_value());
    CHECK(rules->size() == 2);
  }

  TEST_CASE("stale removal rejects the entire replacement") {
    RuleConfigFixture file("[ignore]\nprocesses = [\"changed.exe\"]\n");
    auto original = file.read();
    auto result = edit_ignore_rules(file.path, {{IgnoreRuleKind::Process, "old.exe", {}}},
                                    {{IgnoreRuleKind::Title, {}, "Tool"}});
    CHECK_FALSE(result.has_value());
    CHECK(file.read() == original);
  }

  TEST_CASE("invalid config and unsupported values are never rewritten") {
    for (const std::string value :
         {"[ignore\n", "[ignore]\nprocesses = [4]\n",
          "[ignore]\nprocess_title_pairs = [{ process = 'app.exe' }]\n"}) {
      RuleConfigFixture file(value);
      CHECK_FALSE(
          edit_ignore_rules(file.path, {}, {{IgnoreRuleKind::Process, "new.exe", {}}}).has_value());
      CHECK(file.read() == value);
    }
  }

  TEST_CASE("multiline Unicode arrays preserve adjacent values and comments") {
    RuleConfigFixture file("[ignore]\nwindow_titles = [\n  'Gr\xc3\xbc\xc3\x9f'\n] # "
                           "retained\nprocesses = ['keep.exe']\n");
    auto edited =
        edit_ignore_rules(file.path, {}, {{IgnoreRuleKind::Title, {}, "\xe6\x96\x87\xe6\xa1\xa3"}});
    REQUIRE(edited.has_value());
    auto rules = read_user_ignore_rules(file.path);
    REQUIRE(rules.has_value());
    CHECK(rules->size() == 3);
    CHECK(file.read().find("# retained") != std::string::npos);
    CHECK(file.read().find("processes = ['keep.exe']") != std::string::npos);
  }

  TEST_CASE("inline tables support adding another rule kind") {
    RuleConfigFixture file("ignore = { processes = ['app.exe'] }\n[gap]\nhorizontal = 3\n");
    auto edited = edit_ignore_rules(file.path, {}, {{IgnoreRuleKind::Title, {}, "Tool"}});
    REQUIRE(edited.has_value());
    CHECK(file.read().find("[gap]\nhorizontal = 3") != std::string::npos);
    auto rules = read_user_ignore_rules(file.path);
    REQUIRE(rules.has_value());
    CHECK(rules->size() == 2);
  }

  TEST_CASE("read-only or locked config is not partially changed") {
    RuleConfigFixture file("[ignore]\nprocesses = ['app.exe']\n");
    const auto original = file.read();
    HANDLE lock = CreateFileW(file.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(lock != INVALID_HANDLE_VALUE);
    auto edited = edit_ignore_rules(file.path, {{IgnoreRuleKind::Process, "app.exe", {}}},
                                    {{IgnoreRuleKind::Title, {}, "Tool"}});
    CHECK_FALSE(edited.has_value());
    CHECK(file.read() == original);
    CHECK(CloseHandle(lock) != FALSE);
  }

  TEST_CASE("owned rules match the window process only when it has an owner or parent") {
    IgnoreRule rule{IgnoreRuleKind::ChildProcess, "app.exe", {}};
    CHECK(ignore_rule_matches(rule, "APP.EXE", "Dialog", true));
    CHECK_FALSE(ignore_rule_matches(rule, "APP.EXE", "Main", false));
    CHECK_FALSE(ignore_rule_matches(rule, "helper.exe", "Dialog", true));
    CHECK(ignore_rule_matches({IgnoreRuleKind::ProcessTitlePair, "APP.EXE", "TITLE"}, "app.exe",
                              "title", false));
    CHECK_FALSE(
        ignore_rule_matches({IgnoreRuleKind::Title, {}, "TITLE"}, "app.exe", "title", false));
  }

  TEST_CASE("new config never overwrites an existing file") {
    RuleConfigFixture file("# existing\n");
    CHECK_FALSE(create_ignore_config(file.path).has_value());
    CHECK(file.read() == "# existing\n");
    std::error_code error;
    REQUIRE(std::filesystem::remove(file.path, error));
    REQUIRE(create_ignore_config(file.path).has_value());
    CHECK(read_user_ignore_rules(file.path).has_value());
  }
}

TEST_SUITE("System-aware native UI") {
  TEST_CASE("native themes use the selected icon palettes") {
    using namespace wintiler::ui;
    for (auto surface : {ThemeSurface::Application, ThemeSurface::System}) {
      const auto dark = palette_for(ThemeMode::Dark, surface);
      const auto light = palette_for(ThemeMode::Light, surface);
      CHECK(dark.accent == RGB(54, 197, 240));
      CHECK(light.accent == RGB(255, 176, 32));
      CHECK(light.accent_label != light.accent);
    }
    CHECK(palette_for(ThemeMode::Dark).background == RGB(11, 23, 38));
    const auto contrast = palette_for(ThemeMode::HighContrast);
    CHECK(contrast.background == GetSysColor(COLOR_WINDOW));
    CHECK(contrast.accent == GetSysColor(COLOR_HIGHLIGHT));
    CHECK(contrast.accent_text == GetSysColor(COLOR_HIGHLIGHTTEXT));
  }
  TEST_CASE("dialog and menu text stays readable across both brand palettes") {
    using namespace wintiler::ui;
    auto luminance = [](COLORREF color) {
      auto linear = [](BYTE channel) {
        const double value = channel / 255.0;
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
      };
      return 0.2126 * linear(GetRValue(color)) + 0.7152 * linear(GetGValue(color)) +
             0.0722 * linear(GetBValue(color));
    };
    auto contrast = [&](COLORREF text, COLORREF background) {
      const double a = luminance(text), b = luminance(background);
      return ((std::max)(a, b) + 0.05) / ((std::min)(a, b) + 0.05);
    };
    for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
      for (auto surface : {ThemeSurface::Application, ThemeSurface::System}) {
        CAPTURE(static_cast<int>(mode));
        CAPTURE(static_cast<int>(surface));
        const auto colors = palette_for(mode, surface);
        for (auto background : {colors.background, colors.surface, colors.raised}) {
          CHECK(contrast(colors.foreground, background) >= 4.5);
          CHECK(contrast(colors.muted, background) >= 4.5);
          CHECK(contrast(colors.accent_label, background) >= 4.5);
        }
        CHECK(contrast(colors.selection_text, colors.selection) >= 4.5);
        CHECK(contrast(colors.muted, colors.selection) >= 4.5);
        CHECK(contrast(colors.accent_label, colors.selection) >= 4.5);
        CHECK(contrast(colors.accent_text, colors.accent) >= 4.5);
        CHECK(contrast(colors.error, colors.background) >= 4.5);
      }
    }
  }
  TEST_CASE("dialog follows app theme and tray follows system theme") {
    using namespace wintiler::ui;
    CHECK(choose_theme(false, true, false, ThemeSurface::Application) == ThemeMode::Dark);
    CHECK(choose_theme(false, true, false, ThemeSurface::System) == ThemeMode::Light);
    CHECK(choose_theme(true, false, false, ThemeSurface::Application) == ThemeMode::Light);
    CHECK(choose_theme(true, false, false, ThemeSurface::System) == ThemeMode::Dark);
  }
  TEST_CASE("high contrast overrides personalization and missing settings default safely") {
    using namespace wintiler::ui;
    CHECK(choose_theme(false, false, true, ThemeSurface::System) == ThemeMode::HighContrast);
    CHECK(choose_theme(true, false, true, ThemeSurface::Application) == ThemeMode::HighContrast);
    CHECK(choose_theme(std::nullopt, std::nullopt, false, ThemeSurface::System) ==
          ThemeMode::Light);
    CHECK(choose_theme(false, std::nullopt, false, ThemeSurface::System) == ThemeMode::Dark);
    CHECK(scale(32, 144) == 48);
    const std::string unicode = "Gr\xc3\xbc\xc3\x9f \xe6\x96\x87\xe6\xa1\xa3";
    CHECK(utf8(wide(unicode)) == unicode);
  }
}

TEST_CASE("Saved rules retain their source and honor merge flags") {
  auto options = get_default_ignore_options();
  IgnoreRule rule{IgnoreRuleKind::Process, "TextInputHost.exe", {}};
  auto entries = make_ignore_rule_entries({rule}, options);
  REQUIRE(entries.size() > 1);
  CHECK_FALSE(entries.front().built_in);
  CHECK(std::count_if(entries.begin(), entries.end(), [&](const auto& entry) {
          return entry.built_in && ignore_rules_equal(entry.rule, rule);
        }) == 1);
  options.merge_processes = false;
  options.merge_window_titles = false;
  options.merge_process_title_pairs = false;
  options.merge_ignore_children_of_processes = false;
  entries = make_ignore_rule_entries({rule}, options);
  REQUIRE(entries.size() == 1);
  CHECK_FALSE(entries.front().built_in);
}

TEST_CASE("A main window can manage its owned-window rule without being ignored") {
  winapi::WindowManagementSnapshot window;
  window.process_name = "app.exe";
  window.title = "Main";
  IgnoreRule owned{IgnoreRuleKind::ChildProcess, "APP.EXE", {}};
  CHECK(rule_is_related_to_window(owned, window));
  CHECK_FALSE(
      ignore_rule_matches(owned, window.process_name, window.title, window.is_child_or_owned));
  CHECK_FALSE(rule_is_related_to_window({IgnoreRuleKind::Process, "other.exe", {}}, window));
}

TEST_CASE("Window details preserve full Unicode titles for exact rules") {
  std::wstring title = L"Notes - \u6771\u4eac \u00e9 " + std::wstring(300, L'x');
  HWND window = CreateWindowExW(0, L"STATIC", title.c_str(), WS_OVERLAPPEDWINDOW, 0, 0, 640, 480,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  REQUIRE(window != nullptr);
  const auto info = winapi::get_window_info(reinterpret_cast<winapi::HWND_T>(window));
  CHECK(info.title == ui::utf8(title));
  CHECK(DestroyWindow(window) != FALSE);
}

TEST_CASE("Creating a config adopts it for later live reloads") {
  RuleConfigFixture file("[ignore]\nprocesses = [\"app.exe\"]\n");
  GlobalOptionsProvider provider;
  REQUIRE_FALSE(provider.configPath.has_value());
  REQUIRE(provider.reload_from(file.path).has_value());
  CHECK(provider.configPath == file.path);
  CHECK(std::find(provider.options.ignoreOptions.ignored_processes.begin(),
                  provider.options.ignoreOptions.ignored_processes.end(),
                  "app.exe") != provider.options.ignoreOptions.ignored_processes.end());
  CHECK(provider.nextConfigRefreshCheck == std::chrono::steady_clock::time_point::min());
  auto previous = provider.configPath;
  RuleConfigFixture invalid("[ignore\n");
  CHECK_FALSE(provider.reload_from(invalid.path).has_value());
  CHECK(provider.configPath == previous);
}

TEST_CASE("Native menus cycle ambiguous letters and skip disabled actions") {
  HWND owner = CreateWindowExW(0, L"STATIC", L"Menu test", 0, 0, 0, 0, 0, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
  REQUIRE(owner != nullptr);
  {
    ui::PopupMenu menu(owner, ui::ThemeSurface::System);
    menu.append(1, L"Open config");
    menu.append(2, L"Open log", {}, false);
    menu.append(3, L"Other action");
    menu.append(4, L"Exit");
    REQUIRE(menu.ok);
    auto character = [&](wchar_t key) {
      return ui::PopupMenu::subclass(owner, WM_MENUCHAR, key, reinterpret_cast<LPARAM>(menu.handle),
                                     0, reinterpret_cast<DWORD_PTR>(&menu));
    };
    auto first = character(L'o');
    CHECK(HIWORD(first) == MNC_SELECT);
    CHECK(LOWORD(first) == 0);
    MENUITEMINFOW info{sizeof(MENUITEMINFOW)};
    info.fMask = MIIM_STATE;
    info.fState = MFS_HILITE;
    REQUIRE(SetMenuItemInfoW(menu.handle, 0, TRUE, &info) != FALSE);
    auto second = character(L'o');
    CHECK(HIWORD(second) == MNC_SELECT);
    CHECK(LOWORD(second) == 2);
    auto unique = character(L'e');
    CHECK(HIWORD(unique) == MNC_EXECUTE);
    CHECK(LOWORD(unique) == 3);
  }
  CHECK(DestroyWindow(owner) != FALSE);
}

TEST_CASE("Unsupported table-array rules are preserved when editing is refused") {
  const std::string original =
      "[ignore]\n[[ignore.process_title_pairs]]\nprocess=\"app.exe\"\ntitle=\"Title\"\n";
  RuleConfigFixture file(original);
  auto result =
      edit_ignore_rules(file.path, {{IgnoreRuleKind::ProcessTitlePair, "app.exe", "Title"}},
                        {{IgnoreRuleKind::Process, "app.exe", {}}});
  CHECK_FALSE(result.has_value());
  CHECK(file.read() == original);
}

TEST_CASE("The first rule atomically creates a config and can be undone") {
  RuleConfigFixture file("");
  REQUIRE(std::filesystem::remove(file.path));
  IgnoreRule rule{IgnoreRuleKind::ProcessTitlePair, "fastpdf.exe", "FastPDF"};
  auto edit = edit_ignore_rules(file.path, {}, {rule}, true);
  REQUIRE(edit.has_value());
  REQUIRE(edit->added.size() == 1);
  auto rules = read_user_ignore_rules(file.path);
  REQUIRE(rules.has_value());
  REQUIRE(rules->size() == 1);
  CHECK(ignore_rules_equal(rules->front(), rule));
  GlobalOptionsProvider provider;
  REQUIRE(provider.reload_from(file.path).has_value());
  CHECK(provider.configPath == file.path);
  auto undone = edit_ignore_rules(file.path, edit->added, edit->removed);
  REQUIRE(undone.has_value());
  rules = read_user_ignore_rules(file.path);
  REQUIRE(rules.has_value());
  CHECK(rules->empty());
  CHECK(read_options_toml(file.path).has_value());
}

TEST_CASE("Automatic config creation preserves existing files and rejects stale replacements") {
  RuleConfigFixture file("# Keep this\n[gap]\nhorizontal = 17\n");
  IgnoreRule rule{IgnoreRuleKind::Process, "fastpdf.exe", {}};
  REQUIRE(edit_ignore_rules(file.path, {}, {rule}, true).has_value());
  CHECK(file.read().find("# Keep this\n[gap]\nhorizontal = 17") != std::string::npos);
  REQUIRE(std::filesystem::remove(file.path));
  CHECK_FALSE(edit_ignore_rules(file.path, {rule}, {{IgnoreRuleKind::Title, {}, "FastPDF"}}, true)
                  .has_value());
  CHECK_FALSE(std::filesystem::exists(file.path));
  CHECK_FALSE(
      edit_ignore_rules(file.path, {}, {{IgnoreRuleKind::Process, {}, {}}}, true).has_value());
  CHECK_FALSE(std::filesystem::exists(file.path));
  file.write("[ignore\n");
  CHECK_FALSE(edit_ignore_rules(file.path, {}, {rule}, true).has_value());
  CHECK(file.read() == "[ignore\n");
}
#endif
