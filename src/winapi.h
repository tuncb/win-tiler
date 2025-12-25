#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "options.h"

namespace winapi {

using HWND_T = void*;
using HMONITOR_T = void*;
using DWORD_T = unsigned long;

struct Rect {
  long left;
  long top;
  long right;
  long bottom;
};

struct MonitorInfo {
  HMONITOR_T handle;
  Rect rect;
  Rect workArea;
  bool isPrimary;
};

struct WindowPosition {
  int x;
  int y;
  int width;
  int height;
};

struct TileInfo {
  HWND_T handle;
  WindowPosition window_position;
};

struct WindowInfo {
  HWND_T handle;
  std::string title;
  std::optional<DWORD_T> pid;
  std::string processName;
};

std::vector<MonitorInfo> get_monitors();
std::optional<DWORD_T> get_window_pid(HWND_T hwnd);
std::string get_process_name_from_pid(DWORD_T pid);
bool is_window_maximized(HWND_T hwnd);
std::vector<WindowInfo> get_windows_list();
std::vector<WindowInfo> gather_raw_window_data(const wintiler::IgnoreOptions& ignore_options);
void log_windows_per_monitor(const wintiler::IgnoreOptions& ignore_options,
                             std::optional<size_t> monitor_index = std::nullopt);
void update_window_position(const TileInfo& tile_info);
std::vector<HWND_T> get_hwnds_for_monitor(size_t monitor_index,
                                          const wintiler::IgnoreOptions& ignore_options);
WindowInfo get_window_info(HWND_T hwnd);

struct Point {
  long x;
  long y;
};

HWND_T get_foreground_window();
std::optional<Point> get_cursor_pos();
bool set_cursor_pos(long x, long y);
bool set_foreground_window(HWND_T hwnd);

// Keyboard hotkey support
struct HotKeyInfo {
  int id;
  unsigned int modifiers;
  unsigned int key;
};

// Parse a hotkey string like "ctrl+alt+a" and return HotKeyInfo
// Supported modifiers: alt, ctrl, shift, super (Windows key)
// Only single character keys are supported
std::optional<HotKeyInfo> create_hotkey(const std::string& text, int id);

// Register a hotkey with Windows
bool register_hotkey(const HotKeyInfo& hotkey);

// Unregister a previously registered hotkey
bool unregister_hotkey(int id);

// Check for pending hotkey messages, returns the hotkey id if triggered
std::optional<int> check_keyboard_action();

} // namespace winapi
