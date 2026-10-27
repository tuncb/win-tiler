#pragma once

#include "options.h"
#include "ui_theme.h"

namespace wintiler {

enum TrayCommand : unsigned int {
  ID_OPEN_CONFIG = 1001,
  ID_SHOW_LOG,
  ID_TOGGLE_PAUSE,
  ID_RESET,
  ID_ABOUT,
  ID_INSTALLER,
  ID_CHECK_UPDATES,
  ID_EXIT,
  ID_TOGGLE_VERBOSE_LOGGING,
  ID_MANAGE_IGNORED_WINDOWS,
  ID_MOVEMENT_SWAP,
  ID_MOVEMENT_INSERT,
  ID_PLACE_POINTER,
  ID_PLACE_FOCUSED,
  ID_PLACE_LARGEST,
  ID_PLACE_ALL_MONITORS,
  ID_SPLIT_AUTO,
  ID_SPLIT_VERTICAL,
  ID_SPLIT_HORIZONTAL,
  ID_OUTLINES_ALL,
  ID_OUTLINES_ACTIVE,
  ID_OUTLINES_OFF,
  ID_DUMP_DIAGNOSTICS,
};

struct TrayMenuState {
  bool paused = false;
  bool has_desktop = false;
  bool has_config = false;
  bool has_log = false;
  bool verbose_logging = false;
  MovementMode movement_mode = MovementMode::Swap;
  LayoutOptions layout;
  renderer::RenderOptions render;
};

void populate_tray_menu(ui::PopupMenu& menu, const TrayMenuState& state);
[[nodiscard]] const wchar_t* tray_outlines_label(const renderer::RenderOptions& options);
[[nodiscard]] std::optional<MovementMode> tray_movement_choice(unsigned int command);
[[nodiscard]] std::optional<LayoutSplitMode> tray_split_choice(unsigned int command);
[[nodiscard]] bool is_tray_config_command(unsigned int command);
[[nodiscard]] tl::expected<void, std::string> save_tray_setting(const std::filesystem::path& path,
                                                                unsigned int command);
[[nodiscard]] tl::expected<void, std::string> ensure_tray_config(const std::filesystem::path& path);

} // namespace wintiler
