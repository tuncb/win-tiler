#include "track_windows.h"

#include <dwmapi.h>
#include <spdlog/spdlog.h>
#include <Windows.h>

#include <chrono>
#include <thread>

#include "winapi.h"

namespace wintiler {

namespace {

constexpr int EXIT_HOTKEY_ID = 1;

std::optional<std::string> find_exit_hotkey(const KeyboardOptions& keyboard_options) {
  for (const auto& binding : keyboard_options.bindings) {
    if (binding.action == HotkeyAction::Exit) {
      return binding.hotkey;
    }
  }
  return std::nullopt;
}

bool register_exit_hotkey(const KeyboardOptions& keyboard_options) {
  auto hotkey_str = find_exit_hotkey(keyboard_options);
  if (!hotkey_str) {
    spdlog::warn("No exit hotkey configured");
    return false;
  }

  auto hotkey = winapi::create_hotkey(*hotkey_str, EXIT_HOTKEY_ID);
  if (!hotkey) {
    spdlog::error("Failed to parse exit hotkey: {}", *hotkey_str);
    return false;
  }

  if (!winapi::register_hotkey(*hotkey)) {
    spdlog::error("Failed to register exit hotkey");
    return false;
  }

  spdlog::info("Registered exit hotkey: {}", *hotkey_str);
  return true;
}

void unregister_exit_hotkey() {
  winapi::unregister_hotkey(EXIT_HOTKEY_ID);
}

} // namespace

void run_track_windows_mode(GlobalOptionsProvider& optionsProvider) {
  const auto& options = optionsProvider.options;

  // Register exit hotkey
  bool hotkey_registered = register_exit_hotkey(options.keyboardOptions);

  spdlog::info("Track windows mode started. Press exit hotkey to quit.");

  while (true) {
    // Check for exit hotkey
    if (hotkey_registered) {
      if (auto hotkey_id = winapi::check_keyboard_action()) {
        if (*hotkey_id == EXIT_HOTKEY_ID) {
          spdlog::info("Exit hotkey pressed, shutting down...");
          break;
        }
      }
    }

    // Get monitors and windows
    auto monitors = winapi::get_monitors();
    for (size_t i = 0; i < monitors.size(); ++i) {
      const auto& monitor = monitors[i];
      auto hwnds = winapi::get_hwnds_for_monitor(i, options.ignoreOptions);
      spdlog::info("--- Monitor {} ({} windows) ---", i, hwnds.size());

      for (auto hwnd : hwnds) {
        auto info = winapi::get_window_info(hwnd);

        // Get window rect for position and size
        RECT rect;
        GetWindowRect((HWND)hwnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        // Get extended window styles
        LONG exStyle = GetWindowLong((HWND)hwnd, GWL_EXSTYLE);

        // Check if window is hung
        bool isHung = IsHungAppWindow((HWND)hwnd) != 0;

        // Check if window is cloaked
        BOOL cloaked = FALSE;
        DwmGetWindowAttribute((HWND)hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));

        spdlog::info("  HWND: {}, PID: {}, Process: {}, Class: {}, Pos: ({},{}), Size: {}x{}, "
                     "ExStyle: 0x{:08X}, Hung: {}, Cloaked: {}, Title: \"{}\"",
                     info.handle, info.pid.value_or(0), info.processName, info.className, rect.left,
                     rect.top, width, height, exStyle, isHung, cloaked != 0, info.title);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  if (hotkey_registered) {
    unregister_exit_hotkey();
  }
}

} // namespace wintiler
