#include "options.h"

namespace wintiler {

IgnoreOptions get_default_ignore_options() {
  IgnoreOptions options;
  options.ignored_processes = {
      "TextInputHost.exe",
      "ApplicationFrameHost.exe",
      "Microsoft.CmdPal.UI.exe",
      "PowerToys.PowerLauncher.exe",
  };
  options.ignored_window_titles = {};
  options.ignored_process_title_pairs = {
      {"SystemSettings.exe", "Settings"},
      {"explorer.exe", "Program Manager"},
      {"explorer.exe", "System tray overflow window."},
      {"explorer.exe", "PopupHost"},
      {"claude.exe", "Title: Claude"},
  };
  options.small_window_barrier = SmallWindowBarrier{50, 50};
  return options;
}

GlobalOptions get_default_global_options() {
  GlobalOptions options;
  options.ignoreOptions = get_default_ignore_options();
  options.keyboardOptions.bindings = {
      {HotkeyAction::NavigateLeft, "super+shift+h"}, {HotkeyAction::NavigateDown, "super+shift+j"},
      {HotkeyAction::NavigateUp, "super+shift+k"},   {HotkeyAction::NavigateRight, "super+shift+l"},
      {HotkeyAction::ToggleSplit, "super+shift+y"},  {HotkeyAction::Exit, "super+shift+escape"},
      {HotkeyAction::ToggleGlobal, "super+shift+;"}, {HotkeyAction::StoreCell, "super+shift+["},
      {HotkeyAction::ClearStored, "super+shift+]"},  {HotkeyAction::Exchange, "super+shift+,"},
      {HotkeyAction::Move, "super+shift+."},
  };
  return options;
}

} // namespace wintiler
