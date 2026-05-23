#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
  std::string deviceName;
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

// Standard Win32 dialogs use the predefined "#32770" class and are commonly owned popups.
bool should_ignore_owned_dialog_window(bool has_owner, const std::string& class_name);

void fill_monitors(std::vector<MonitorInfo>& monitors);
void invalidate_monitor_cache();
void log_monitors(const std::vector<MonitorInfo>& monitors);
bool monitors_equal(const std::vector<MonitorInfo>& a, const std::vector<MonitorInfo>& b);
void log_windows_per_monitor(const wintiler::IgnoreOptions& ignore_options,
                             std::optional<size_t> monitor_index = std::nullopt);
void dump_window_management_state(const wintiler::IgnoreOptions& ignore_options);
void update_window_position(const TileInfo& tile_info);
std::vector<HWND_T> get_hwnds_for_monitor(size_t monitor_index,
                                          const wintiler::IgnoreOptions& ignore_options);
WindowInfo get_window_info(HWND_T hwnd);

// Lightweight HWND helpers used by session-scoped runtime ignore rules.
bool is_window_valid(HWND_T hwnd);
std::optional<DWORD_T> get_window_process_id(HWND_T hwnd);
bool is_window_or_owned_or_parented_by(HWND_T hwnd, HWND_T root);

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
[[nodiscard]] std::string format_register_hotkey_failure(const HotKeyInfo& hotkey,
                                                         std::string_view action_name,
                                                         std::string_view shortcut, DWORD_T error);
bool register_hotkey(const HotKeyInfo& hotkey, std::string_view action_name,
                     std::string_view shortcut);

// Unregister a previously registered hotkey
bool unregister_hotkey(int id);

// Check for pending hotkey messages, returns the hotkey id if triggered
std::optional<int> check_keyboard_action();

// Returns true for messages that should remain queued for check_keyboard_action().
[[nodiscard]] bool should_defer_message_to_hotkey_poll(unsigned int message);

// Returns true for messages that can change monitor geometry or work-area bounds.
[[nodiscard]] bool should_invalidate_monitor_cache_for_message(unsigned int message);

// Process queued window messages while leaving hotkeys queued for check_keyboard_action().
void process_pending_non_hotkey_messages();

// Wait for messages or timeout using MsgWaitForMultipleObjectsEx
// Returns true if messages are available, false on timeout
bool wait_for_messages_or_timeout(unsigned long timeout_ms);

// Window move/resize detection (for pausing tiling during user drag operations)
void register_move_size_hook();
void unregister_move_size_hook();

// Session/Power state management - pauses loop on lock/sleep/display-off
void register_session_power_notifications();
void unregister_session_power_notifications();

struct NotificationAreaIconOptions {
  std::optional<std::filesystem::path> config_path;
  std::optional<std::filesystem::path> log_file_path;
};

struct NotificationAreaMenuAvailability {
  bool can_open_config = false;
  bool can_show_log = false;
  bool can_exit = true;
};

[[nodiscard]] NotificationAreaMenuAvailability
get_notification_area_menu_availability(const NotificationAreaIconOptions& options);

[[nodiscard]] std::wstring get_notification_area_about_message();
[[nodiscard]] std::wstring get_notification_area_about_dialog_content();
[[nodiscard]] const wchar_t* get_notification_area_toggle_pause_menu_text(bool is_paused);

void register_notification_area_icon(const NotificationAreaIconOptions& options);
void unregister_notification_area_icon();
void set_notification_area_manual_pause_active(bool is_paused);
void request_notification_area_hotkey_action(wintiler::HotkeyAction action);
[[nodiscard]] bool consume_notification_area_exit_requested();
[[nodiscard]] std::optional<wintiler::HotkeyAction> consume_notification_area_hotkey_action();

// Blocks until session is active (unlocked, awake, display on)
// Returns immediately if already active
void wait_for_session_active();

// Check if session is currently paused (locked, sleeping, or display off)
bool is_session_paused();

// Virtual desktop management - initializes COM interface for desktop ID detection
void register_virtual_desktop_notifications();
void unregister_virtual_desktop_notifications();

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
  bool is_fullscreen = false;
  bool is_maximized = false;
  bool is_minimized = false;
  std::optional<WindowPosition> actual_rect;
};

// Consolidated input state for the main loop
struct LoopInputState {
  // Window movement state
  bool is_any_window_being_moved = false;
  std::optional<DragInfo> drag_info;

  // Cursor and keyboard state
  std::optional<Point> cursor_pos;
  bool is_ctrl_pressed = false;
  bool is_right_mouse_pressed = false;

  // Window state
  HWND_T foreground_window = nullptr;

  // Monitor data (index in vector = monitor index)
  std::vector<MonitorInfo> monitors;

  // Per-monitor managed windows (index matches monitors vector)
  std::vector<std::vector<ManagedWindowInfo>> windows_per_monitor;

  // Virtual desktop ID (GUID as string, from first managed window)
  std::optional<std::string> desktop_id;
};

// Gather all input state for the main loop in a single call
void gather_loop_input_state_into(const wintiler::IgnoreOptions& ignore_options,
                                  LoopInputState& state, std::vector<HWND_T>& all_handles);

#ifndef DOCTEST_CONFIG_DISABLE
void set_monitor_cache_for_test(const std::vector<MonitorInfo>& monitors);
bool is_monitor_cache_dirty_for_test();
#endif // !DOCTEST_CONFIG_DISABLE

} // namespace winapi
