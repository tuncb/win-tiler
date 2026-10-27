#include "tray_menu.h"

#include <tuple>

#include "config_file.h"
#include "winapi.h"

namespace wintiler {

const wchar_t* tray_outlines_label(const renderer::RenderOptions& options) {
  if (!options.show_rectangles || options.border_width <= 0)
    return L"Window outlines: Off";
  return options.show_only_active_window ? L"Window outlines: Active only"
                                         : L"Window outlines: All";
}

void populate_tray_menu(ui::PopupMenu& menu, const TrayMenuState& state) {
  const bool can_control = state.has_desktop && !state.paused;
  menu.append(ID_TOGGLE_PAUSE, winapi::get_notification_area_toggle_pause_menu_text(state.paused));
  menu.append(ID_RESET, L"Rebuild layout", {}, can_control);
  menu.separator();
  if (auto child = menu.submenu(L"New windows")) {
    menu.append(0, L"Place new windows", {}, false, false, child);
    for (const auto& [id, label, value] :
         {std::tuple{ID_PLACE_POINTER, L"Under pointer", LayoutSplitTarget::Pointer},
          std::tuple{ID_PLACE_FOCUSED, L"At focused window", LayoutSplitTarget::Focused},
          std::tuple{ID_PLACE_LARGEST, L"Largest space on active monitor",
                     LayoutSplitTarget::Largest},
          std::tuple{ID_PLACE_ALL_MONITORS, L"Largest space across monitors",
                     LayoutSplitTarget::LargestAllMonitors}}) {
      menu.append(id, label, {}, true, state.layout.split_target == value, child, true);
    }
    menu.separator(child);
    menu.append(0, L"Split direction for new windows", {}, false, false, child);
    for (const auto& [id, label, value] :
         {std::tuple{ID_SPLIT_AUTO, L"Automatic (Dwindle)", LayoutSplitMode::Dwindle},
          std::tuple{ID_SPLIT_VERTICAL, L"Side by side", LayoutSplitMode::Vertical},
          std::tuple{ID_SPLIT_HORIZONTAL, L"Stacked", LayoutSplitMode::Horizontal}}) {
      menu.append(id, label, {}, true, state.layout.split_mode == value, child, true);
    }
  }
  if (auto child = menu.submenu(
          winapi::get_notification_area_movement_mode_menu_text(state.movement_mode))) {
    menu.append(0, L"This desktop, this session", {}, false, false, child);
    menu.append(ID_MOVEMENT_SWAP, L"Swap", {}, can_control,
                state.movement_mode == MovementMode::Swap, child, true);
    menu.append(ID_MOVEMENT_INSERT, L"Insert", {}, can_control,
                state.movement_mode == MovementMode::Insert, child, true);
  }
  if (auto child = menu.submenu(tray_outlines_label(state.render))) {
    const bool visible = state.render.show_rectangles && state.render.border_width > 0;
    menu.append(ID_OUTLINES_ALL, L"All windows", {}, true,
                visible && !state.render.show_only_active_window, child, true);
    menu.append(ID_OUTLINES_ACTIVE, L"Active window only", {}, true,
                visible && state.render.show_only_active_window, child, true);
    menu.append(ID_OUTLINES_OFF, L"Off", {}, true, !visible, child, true);
  }
  menu.append(ID_MANAGE_IGNORED_WINDOWS, L"Ignored windows...");
  menu.separator();
  menu.append(ID_OPEN_CONFIG,
              state.has_config ? L"Edit configuration..." : L"Create configuration...");
  if (auto child = menu.submenu(L"Tools & help")) {
    menu.append(ID_SHOW_LOG, L"Open log file", {}, state.has_log, false, child);
    menu.append(ID_TOGGLE_VERBOSE_LOGGING, L"Verbose logging", {}, true, state.verbose_logging,
                child);
    menu.append(ID_DUMP_DIAGNOSTICS, L"Write diagnostics to log", {}, state.has_log, false, child);
    menu.separator(child);
    menu.append(ID_INSTALLER, L"Startup & installation...", {}, true, false, child);
    menu.append(ID_CHECK_UPDATES, L"Check for updates...", {}, true, false, child);
    menu.append(ID_ABOUT, L"About win-tiler...", {}, true, false, child);
  }
  menu.separator();
  menu.append(ID_EXIT, L"Exit win-tiler");
}

std::optional<MovementMode> tray_movement_choice(unsigned int command) {
  if (command == ID_MOVEMENT_SWAP)
    return MovementMode::Swap;
  if (command == ID_MOVEMENT_INSERT)
    return MovementMode::Insert;
  return std::nullopt;
}

std::optional<LayoutSplitMode> tray_split_choice(unsigned int command) {
  if (command == ID_SPLIT_AUTO)
    return LayoutSplitMode::Dwindle;
  if (command == ID_SPLIT_VERTICAL)
    return LayoutSplitMode::Vertical;
  if (command == ID_SPLIT_HORIZONTAL)
    return LayoutSplitMode::Horizontal;
  return std::nullopt;
}

bool is_tray_config_command(unsigned int command) {
  return command >= ID_PLACE_POINTER && command <= ID_OUTLINES_OFF;
}

tl::expected<void, std::string> ensure_tray_config(const std::filesystem::path& path) {
  std::error_code error;
  if (std::filesystem::exists(path, error))
    return {};
  if (error)
    return tl::unexpected("Cannot inspect config: " + error.message());
  return write_config_text_atomically(path, format_options_toml(get_default_global_options()),
                                      std::nullopt, true);
}

tl::expected<void, std::string> save_tray_setting(const std::filesystem::path& path,
                                                  unsigned int command) {
  std::error_code error;
  const bool exists = std::filesystem::exists(path, error);
  if (error)
    return tl::unexpected("Cannot inspect config: " + error.message());
  auto original = exists ? read_config_text(path)
                         : tl::expected<std::string, std::string>(
                               format_options_toml(get_default_global_options()));
  if (!original)
    return tl::unexpected(original.error());
  std::vector<ConfigValueEdit> edits;
  switch (command) {
  case ID_PLACE_POINTER:
    edits.push_back({{"layout", "split_target"}, "\"pointer\""});
    break;
  case ID_PLACE_FOCUSED:
    edits.push_back({{"layout", "split_target"}, "\"focused\""});
    break;
  case ID_PLACE_LARGEST:
    edits.push_back({{"layout", "split_target"}, "\"largest\""});
    break;
  case ID_PLACE_ALL_MONITORS:
    edits.push_back({{"layout", "split_target"}, "\"largest_all_monitors\""});
    break;
  case ID_SPLIT_AUTO:
    edits.push_back({{"layout", "split_mode"}, "\"dwindle\""});
    break;
  case ID_SPLIT_VERTICAL:
    edits.push_back({{"layout", "split_mode"}, "\"vertical\""});
    break;
  case ID_SPLIT_HORIZONTAL:
    edits.push_back({{"layout", "split_mode"}, "\"horizontal\""});
    break;
  case ID_OUTLINES_ALL:
  case ID_OUTLINES_ACTIVE:
  case ID_OUTLINES_OFF:
    edits.push_back({{"visualization", "render", "show_rectangles"},
                     command == ID_OUTLINES_OFF ? "false" : "true"});
    if (command != ID_OUTLINES_OFF) {
      edits.push_back({{"visualization", "render", "show_only_active_window"},
                       command == ID_OUTLINES_ACTIVE ? "true" : "false"});
      // A zero-width border is another way of turning outlines off. Restore a usable
      // width when explicitly enabling them, but retain every positive custom width.
      try {
        const auto parsed = toml::parse(*original);
        if (parsed["visualization"]["render"]["border_width"].value_or<double>(
                kDefaultBorderWidth) <= 0)
          edits.push_back({{"visualization", "render", "border_width"}, "3.0"});
      } catch (const toml::parse_error& parse_error) {
        return tl::unexpected(std::string("Config parse error: ") + parse_error.what());
      }
    }
    break;
  default:
    return tl::unexpected("Unknown tray setting.");
  }
  auto updated = rewrite_config_values(*original, edits);
  if (!updated)
    return tl::unexpected(updated.error());
  if (exists && *updated == *original)
    return {};
  return write_config_text_atomically(path, *updated, *original, !exists);
}

} // namespace wintiler
