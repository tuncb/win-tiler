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
  std::string className;
};

std::vector<MonitorInfo> get_monitors();
void log_monitors(const std::vector<MonitorInfo>& monitors);
bool monitors_equal(const std::vector<MonitorInfo>& a, const std::vector<MonitorInfo>& b);
void log_windows_per_monitor(const wintiler::IgnoreOptions& ignore_options,
                             std::optional<size_t> monitor_index = std::nullopt);
void update_window_position(const TileInfo& tile_info);
std::vector<HWND_T> get_hwnds_for_monitor(size_t monitor_index,
                                          const wintiler::IgnoreOptions& ignore_options);
WindowInfo get_window_info(HWND_T hwnd);

// Get window position and size (returns nullopt if window is invalid)
std::optional<WindowPosition> get_window_rect(HWND_T hwnd);

struct Point {
  long x;
  long y;
};

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

// Wait for messages or timeout using MsgWaitForMultipleObjectsEx
// Returns true if messages are available, false on timeout
bool wait_for_messages_or_timeout(unsigned long timeout_ms);

// Window move/resize detection (for pausing tiling during user drag operations)
void register_move_size_hook();
void unregister_move_size_hook();

// Drag operation tracking (for mouse-based window move operations)
struct DragInfo {
  HWND_T hwnd;     // Window being dragged
  bool move_ended; // True when drag just ended (one-shot detection)
};

// Clear the drag ended flag after handling it
void clear_drag_ended();

// Per-window data for consolidated queries
struct ManagedWindowInfo {
  HWND_T handle;
  bool is_fullscreen;
};

// Consolidated input state for the main loop
struct LoopInputState {
  // Window movement state
  bool is_any_window_being_moved;
  std::optional<DragInfo> drag_info;

  // Cursor and keyboard state
  std::optional<Point> cursor_pos;
  bool is_ctrl_pressed;

  // Window state
  HWND_T foreground_window;

  // Monitor data (index in vector = monitor index)
  std::vector<MonitorInfo> monitors;

  // Per-monitor managed windows (index matches monitors vector)
  std::vector<std::vector<ManagedWindowInfo>> windows_per_monitor;
};

// Gather all input state for the main loop in a single call
LoopInputState gather_loop_input_state(const wintiler::IgnoreOptions& ignore_options);

} // namespace winapi
