#include "winapi.h"

#include <dwmapi.h>
#include <psapi.h>
#include <spdlog/spdlog.h>
#include <windows.h>
#include <wtsapi32.h>

#include <algorithm>
#include <atomic>
#include <cctype>

// Link with Psapi.lib
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Wtsapi32.lib")

namespace winapi {

// Helper function for case-insensitive string comparison
static bool iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size())
    return false;
  return std::equal(a.begin(), a.end(), b.begin(),
                    [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor,
                              LPARAM dwData) {
  std::vector<MonitorInfo>* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(dwData);
  MONITORINFO mi;
  mi.cbSize = sizeof(MONITORINFO);
  if (GetMonitorInfoA(hMonitor, &mi)) {
    MonitorInfo info;
    info.handle = (HMONITOR_T)hMonitor;
    info.rect = {mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom};
    info.workArea = {mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom};
    info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY);
    monitors->push_back(info);
  }
  return TRUE;
}

std::vector<MonitorInfo> get_monitors() {
  std::vector<MonitorInfo> monitors;
  EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&monitors);
  return monitors;
}

void log_monitors(const std::vector<MonitorInfo>& monitors) {
  spdlog::info("=== Monitor Info ({} monitors) ===", monitors.size());
  for (size_t i = 0; i < monitors.size(); ++i) {
    const auto& m = monitors[i];
    long rectW = m.rect.right - m.rect.left;
    long rectH = m.rect.bottom - m.rect.top;
    long workW = m.workArea.right - m.workArea.left;
    long workH = m.workArea.bottom - m.workArea.top;
    spdlog::info("Monitor {}: handle={}, primary={}", i, m.handle, m.isPrimary);
    spdlog::info("  rect: [{}, {}, {}, {}] ({}x{})", m.rect.left, m.rect.top, m.rect.right,
                 m.rect.bottom, rectW, rectH);
    spdlog::info("  workArea: [{}, {}, {}, {}] ({}x{})", m.workArea.left, m.workArea.top,
                 m.workArea.right, m.workArea.bottom, workW, workH);
  }
}

bool monitors_equal(const std::vector<MonitorInfo>& a, const std::vector<MonitorInfo>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    const auto& ma = a[i];
    const auto& mb = b[i];
    // Compare rect
    if (ma.rect.left != mb.rect.left || ma.rect.top != mb.rect.top ||
        ma.rect.right != mb.rect.right || ma.rect.bottom != mb.rect.bottom) {
      return false;
    }
    // Compare workArea
    if (ma.workArea.left != mb.workArea.left || ma.workArea.top != mb.workArea.top ||
        ma.workArea.right != mb.workArea.right || ma.workArea.bottom != mb.workArea.bottom) {
      return false;
    }
    // Compare isPrimary
    if (ma.isPrimary != mb.isPrimary) {
      return false;
    }
    // Note: handle is not compared as it may change between enumerations
  }
  return true;
}

static std::optional<DWORD_T> get_window_pid(HWND_T hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId((HWND)hwnd, &pid);
  if (pid != 0) {
    return pid;
  }
  return std::nullopt;
}

static std::string get_process_name_from_pid(DWORD_T pid) {
  std::string processName;
  HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  if (hProcess) {
    char buffer[MAX_PATH];
    if (GetModuleBaseNameA(hProcess, NULL, buffer, MAX_PATH)) {
      processName = buffer;
    }
    CloseHandle(hProcess);
  }
  return processName;
}

static bool is_window_maximized(HWND_T hwnd) {
  return IsZoomed((HWND)hwnd);
}

// Context struct for passing data to WindowEnumProc callback
struct WindowEnumContext {
  std::vector<HWND_T>* handles;
  const wintiler::IgnoreOptions* ignore_options;
};

BOOL CALLBACK WindowEnumProc(HWND hwnd, LPARAM lParam) {
  auto* ctx = reinterpret_cast<WindowEnumContext*>(lParam);

  if (!IsWindowVisible(hwnd)) {
    return TRUE;
  }

  // Check if window is cloaked (hidden by shell/virtual desktops)
  BOOL cloaked = FALSE;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
    if (cloaked) {
      return TRUE;
    }
  }

  char title[256];
  if (GetWindowTextA(hwnd, title, sizeof(title)) == 0) {
    return TRUE;
  }

  char classNameBuf[256];
  std::string className;
  if (GetClassNameA(hwnd, classNameBuf, sizeof(classNameBuf)) > 0) {
    className = classNameBuf;
  }

  // Check for system drag image windows (always ignore)
  if (className == "SysDragImage") {
    return TRUE;
  }

  // Check for tooltip windows (always ignore)
  if (className == "tooltips_class32") {
    return TRUE;
  }

  // Check extended window styles
  LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
  if (exStyle & WS_EX_TOOLWINDOW) {
    return TRUE; // Tool windows (floating panels, utility windows)
  }
  if (exStyle & WS_EX_TOPMOST) {
    return TRUE; // Always-on-top windows (overlays)
  }
  if (exStyle & WS_EX_TRANSPARENT) {
    return TRUE; // Click-through windows
  }
  if (exStyle & WS_EX_NOACTIVATE) {
    return TRUE; // Windows that can't be activated
  }

  // Skip unresponsive windows
  if (IsHungAppWindow(hwnd)) {
    return TRUE;
  }

  auto pid = get_window_pid((HWND_T)hwnd);
  std::string processName;
  if (pid.has_value()) {
    processName = get_process_name_from_pid(pid.value());
  }

  // Skip windows with empty process name
  if (processName.empty()) {
    return TRUE;
  }

  const auto& options = *ctx->ignore_options;

  // Check ignored processes
  for (const auto& proc : options.ignored_processes) {
    if (processName == proc) {
      return TRUE;
    }
  }

  // Check ignored titles
  for (const auto& ignored_title : options.ignored_window_titles) {
    if (title == ignored_title) {
      return TRUE;
    }
  }

  // Check ignored process/title pairs
  for (const auto& pair : options.ignored_process_title_pairs) {
    if (iequals(processName, pair.first) && iequals(title, pair.second)) {
      return TRUE;
    }
  }

  // Check small window barrier
  if (options.small_window_barrier.has_value()) {
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
      int width = rect.right - rect.left;
      int height = rect.bottom - rect.top;
      if (width < options.small_window_barrier->width ||
          height < options.small_window_barrier->height) {
        return TRUE;
      }
    }
  }

  // Check if this is a child/owned window of a process we want to ignore children for
  if (!options.ignore_children_of_processes.empty()) {
    HWND owner = GetWindow(hwnd, GW_OWNER);
    HWND parent = GetParent(hwnd);

    if (owner != NULL || parent != NULL) {
      // This is a child/owned window - check if process is in the ignore list
      for (const auto& proc : options.ignore_children_of_processes) {
        if (iequals(processName, proc)) {
          return TRUE; // Skip this child window
        }
      }
    }
  }

  ctx->handles->push_back((HWND_T)hwnd);
  return TRUE;
}

static std::vector<HWND_T> get_windows_list(const wintiler::IgnoreOptions& ignore_options) {
  std::vector<HWND_T> handles;
  WindowEnumContext ctx{&handles, &ignore_options};
  EnumWindows(WindowEnumProc, (LPARAM)&ctx);
  return handles;
}

static std::vector<HWND_T> gather_raw_window_data(const wintiler::IgnoreOptions& ignore_options) {
  auto handles = get_windows_list(ignore_options);

  std::sort(handles.begin(), handles.end(),
            [](HWND_T a, HWND_T b) { return (uintptr_t)a < (uintptr_t)b; });

  return handles;
}

void log_windows_per_monitor(const wintiler::IgnoreOptions& ignore_options,
                             std::optional<size_t> monitor_index) {
  auto monitors = get_monitors();
  auto handles = gather_raw_window_data(ignore_options);

  if (monitor_index.has_value() && *monitor_index >= monitors.size()) {
    spdlog::error("Monitor index {} is out of bounds. Available monitors: 0-{}", *monitor_index,
                  monitors.size() - 1);
    return;
  }

  for (size_t i = 0; i < monitors.size(); ++i) {
    if (monitor_index.has_value() && i != *monitor_index) {
      continue;
    }

    const auto& monitor = monitors[i];
    spdlog::debug("Monitor {} (Handle: {})", i, monitor.handle);
    spdlog::debug("  Rect: [{}, {}, {}, {}]", monitor.rect.left, monitor.rect.top,
                  monitor.rect.right, monitor.rect.bottom);

    spdlog::debug("  Windows:");
    for (const auto& hwnd : handles) {
      HMONITOR winMonitor = MonitorFromWindow((HWND)hwnd, MONITOR_DEFAULTTONULL);
      if (winMonitor == (HMONITOR)monitor.handle) {
        auto win = get_window_info(hwnd);
        RECT rect;
        GetWindowRect((HWND)hwnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        spdlog::debug("    Handle: {}, PID: {}, Process: {}, Title: {}", hwnd,
                      win.pid.has_value() ? std::to_string(win.pid.value()) : "N/A",
                      win.processName, win.title);
        spdlog::debug("      Position: ({}, {}), Size: {}x{}", rect.left, rect.top, width, height);
      }
    }
    spdlog::debug("--------------------------------------------------");
  }
}

void update_window_position(const TileInfo& tile_info) {
  HWND hwnd = (HWND)tile_info.handle;

  // Restore maximized or minimized windows to normal state before repositioning
  if (IsZoomed(hwnd) || IsIconic(hwnd)) {
    ShowWindow(hwnd, SW_RESTORE);
  }

  // Get DWM frame bounds to compensate for invisible borders
  RECT windowRect, frameRect;
  GetWindowRect(hwnd, &windowRect);
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frameRect,
                                      sizeof(frameRect)))) {
    int borderLeft = frameRect.left - windowRect.left;
    int borderTop = frameRect.top - windowRect.top;
    int borderRight = windowRect.right - frameRect.right;
    int borderBottom = windowRect.bottom - frameRect.bottom;

    int targetX = tile_info.window_position.x - borderLeft;
    int targetY = tile_info.window_position.y - borderTop;
    int targetW = tile_info.window_position.width + borderLeft + borderRight;
    int targetH = tile_info.window_position.height + borderTop + borderBottom;

    // Skip if window is already at the correct position and size
    if (windowRect.left == targetX && windowRect.top == targetY &&
        (windowRect.right - windowRect.left) == targetW &&
        (windowRect.bottom - windowRect.top) == targetH) {
      return;
    }

    SetWindowPos(hwnd, NULL, targetX, targetY, targetW, targetH, SWP_NOZORDER | SWP_NOACTIVATE);
  } else {
    // Fallback if DWM query fails
    int targetX = tile_info.window_position.x;
    int targetY = tile_info.window_position.y;
    int targetW = tile_info.window_position.width;
    int targetH = tile_info.window_position.height;

    if (windowRect.left == targetX && windowRect.top == targetY &&
        (windowRect.right - windowRect.left) == targetW &&
        (windowRect.bottom - windowRect.top) == targetH) {
      return;
    }

    SetWindowPos(hwnd, NULL, targetX, targetY, targetW, targetH, SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

std::vector<HWND_T> get_hwnds_for_monitor(size_t monitor_index,
                                          const wintiler::IgnoreOptions& ignore_options) {
  std::vector<HWND_T> hwnds;
  auto monitors = get_monitors();

  if (monitor_index >= monitors.size()) {
    return hwnds;
  }

  auto handles = gather_raw_window_data(ignore_options);
  const auto& monitor = monitors[monitor_index];

  for (const auto& hwnd : handles) {
    HMONITOR winMonitor = MonitorFromWindow((HWND)hwnd, MONITOR_DEFAULTTONULL);
    if (winMonitor == (HMONITOR)monitor.handle) {
      hwnds.push_back(hwnd);
    }
  }

  return hwnds;
}

WindowInfo get_window_info(HWND_T hwnd) {
  WindowInfo info;
  info.handle = hwnd;

  char title[256];
  if (GetWindowTextA((HWND)hwnd, title, sizeof(title)) > 0) {
    info.title = title;
  }

  char classNameBuf[256];
  if (GetClassNameA((HWND)hwnd, classNameBuf, sizeof(classNameBuf)) > 0) {
    info.className = classNameBuf;
  }

  info.pid = get_window_pid(hwnd);
  if (info.pid.has_value()) {
    info.processName = get_process_name_from_pid(info.pid.value());
  }

  return info;
}

std::optional<WindowPosition> get_window_rect(HWND_T hwnd) {
  if (hwnd == nullptr) {
    return std::nullopt;
  }

  HWND win = reinterpret_cast<HWND>(hwnd);
  if (!IsWindow(win)) {
    return std::nullopt;
  }

  // Use DWM frame bounds for accurate visible rect (excludes invisible borders)
  RECT frameRect;
  if (SUCCEEDED(
          DwmGetWindowAttribute(win, DWMWA_EXTENDED_FRAME_BOUNDS, &frameRect, sizeof(frameRect)))) {
    return WindowPosition{frameRect.left, frameRect.top, frameRect.right - frameRect.left,
                          frameRect.bottom - frameRect.top};
  }

  // Fallback to regular window rect
  RECT windowRect;
  if (!GetWindowRect(win, &windowRect)) {
    return std::nullopt;
  }

  return WindowPosition{windowRect.left, windowRect.top, windowRect.right - windowRect.left,
                        windowRect.bottom - windowRect.top};
}

static HWND_T get_foreground_window() {
  return reinterpret_cast<HWND_T>(GetForegroundWindow());
}

static std::optional<Point> get_cursor_pos() {
  POINT pt;
  if (GetCursorPos(&pt)) {
    return Point{pt.x, pt.y};
  }
  spdlog::error("GetCursorPos failed");
  return std::nullopt;
}

bool set_cursor_pos(long x, long y) {
  if (SetCursorPos(static_cast<int>(x), static_cast<int>(y)) == 0) {
    return false;
  }

  // Send synthetic mouse move event to properly notify applications
  // This fixes the "busy cursor" issue after programmatic cursor movement
  // Use MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK for multi-monitor support
  int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
  int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  input.mi.dx = static_cast<LONG>(((x - virtualLeft) * 65535) / virtualWidth);
  input.mi.dy = static_cast<LONG>(((y - virtualTop) * 65535) / virtualHeight);
  SendInput(1, &input, sizeof(INPUT));

  return true;
}

bool set_foreground_window(HWND_T hwnd) {
  HWND targetHwnd = reinterpret_cast<HWND>(hwnd);
  HWND foregroundHwnd = GetForegroundWindow();

  if (foregroundHwnd == targetHwnd) {
    return true; // Already foreground
  }

  DWORD foregroundThreadId = GetWindowThreadProcessId(foregroundHwnd, nullptr);
  DWORD currentThreadId = GetCurrentThreadId();

  bool attached = false;
  if (foregroundThreadId != currentThreadId) {
    attached = AttachThreadInput(currentThreadId, foregroundThreadId, TRUE) != 0;
  }

  // Simulate a "null" keyboard event — a keypress with no actual key.
  // This satisfies condition: your process becomes the one that "received the last input event,"
  // which grants it permission to call SetForegroundWindow successfully.
  keybd_event(0, 0, 0, 0);
  bool result = SetForegroundWindow(targetHwnd) != 0;

  if (attached) {
    AttachThreadInput(currentThreadId, foregroundThreadId, FALSE);
  }

  return result;
}

std::optional<HotKeyInfo> create_hotkey(const std::string& text, int id) {
  // Split by '+' and trim/lowercase each part
  std::vector<std::string> parts;
  std::string current;

  for (char c : text) {
    if (c == '+') {
      if (!current.empty()) {
        // Trim and lowercase
        std::string trimmed;
        for (char tc : current) {
          if (!std::isspace(static_cast<unsigned char>(tc))) {
            trimmed += static_cast<char>(std::tolower(static_cast<unsigned char>(tc)));
          }
        }
        if (!trimmed.empty()) {
          parts.push_back(trimmed);
        }
        current.clear();
      }
    } else {
      current += c;
    }
  }
  // Don't forget the last part
  if (!current.empty()) {
    std::string trimmed;
    for (char tc : current) {
      if (!std::isspace(static_cast<unsigned char>(tc))) {
        trimmed += static_cast<char>(std::tolower(static_cast<unsigned char>(tc)));
      }
    }
    if (!trimmed.empty()) {
      parts.push_back(trimmed);
    }
  }

  if (parts.empty()) {
    spdlog::error("create_hotkey: Empty hotkey text '{}'", text);
    return std::nullopt;
  }

  unsigned int modifiers = 0;
  const std::string& keyStr = parts.back();

  // Parse modifiers (all parts except the last)
  for (size_t i = 0; i < parts.size() - 1; ++i) {
    const std::string& part = parts[i];
    if (part == "alt") {
      modifiers |= MOD_ALT;
    } else if (part == "ctrl") {
      modifiers |= MOD_CONTROL;
    } else if (part == "shift") {
      modifiers |= MOD_SHIFT;
    } else if (part == "super") {
      modifiers |= MOD_WIN;
    } else {
      spdlog::error("create_hotkey: Unknown modifier '{}' in '{}'", part, text);
      return std::nullopt;
    }
  }

  // Parse key - support single character keys and special keys
  unsigned int key = 0;
  if (keyStr.length() == 1) {
    char c = keyStr[0];
    // Handle special characters that need OEM virtual key codes
    if (c == ';') {
      key = VK_OEM_1; // ;:
    } else if (c == ',') {
      key = VK_OEM_COMMA;
    } else if (c == '.') {
      key = VK_OEM_PERIOD;
    } else if (c == '/') {
      key = VK_OEM_2; // /?
    } else if (c == '[') {
      key = VK_OEM_4; // [{
    } else if (c == '\\') {
      key = VK_OEM_5; // \|
    } else if (c == ']') {
      key = VK_OEM_6; // ]}
    } else if (c == '\'') {
      key = VK_OEM_7; // '"
    } else if (c == '-') {
      key = VK_OEM_MINUS;
    } else if (c == '=') {
      key = VK_OEM_PLUS;
    } else if (c == '`') {
      key = VK_OEM_3; // `~
    } else {
      key = static_cast<unsigned int>(std::toupper(static_cast<unsigned char>(c)));
    }
  } else if (keyStr == "escape" || keyStr == "esc") {
    key = VK_ESCAPE;
  } else if (keyStr == "enter" || keyStr == "return") {
    key = VK_RETURN;
  } else if (keyStr == "space") {
    key = VK_SPACE;
  } else if (keyStr == "tab") {
    key = VK_TAB;
  } else if (keyStr == "backspace") {
    key = VK_BACK;
  } else if (keyStr == "delete") {
    key = VK_DELETE;
  } else if (keyStr == "insert") {
    key = VK_INSERT;
  } else if (keyStr == "home") {
    key = VK_HOME;
  } else if (keyStr == "end") {
    key = VK_END;
  } else if (keyStr == "pageup") {
    key = VK_PRIOR;
  } else if (keyStr == "pagedown") {
    key = VK_NEXT;
  } else if (keyStr == "left") {
    key = VK_LEFT;
  } else if (keyStr == "right") {
    key = VK_RIGHT;
  } else if (keyStr == "up") {
    key = VK_UP;
  } else if (keyStr == "down") {
    key = VK_DOWN;
  } else {
    spdlog::error("create_hotkey: Unknown key '{}'", keyStr);
    return std::nullopt;
  }

  return HotKeyInfo{id, modifiers, key};
}

bool register_hotkey(const HotKeyInfo& hotkey) {
  BOOL result = RegisterHotKey(nullptr, hotkey.id, hotkey.modifiers, hotkey.key);
  if (result == 0) {
    spdlog::error(
        "register_hotkey: Failed to register hotkey id={}, key={}, modifiers={}, error={}",
        hotkey.id, hotkey.key, hotkey.modifiers, GetLastError());
    return false;
  }
  return true;
}

bool unregister_hotkey(int id) {
  BOOL result = UnregisterHotKey(nullptr, id);
  return result != 0;
}

std::optional<int> check_keyboard_action() {
  MSG msg;
  if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_HOTKEY) {
      return static_cast<int>(msg.wParam);
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return std::nullopt;
}

bool wait_for_messages_or_timeout(unsigned long timeout_ms) {
  DWORD result =
      MsgWaitForMultipleObjectsEx(0,       // No handles to wait on
                                  nullptr, // No handle array
                                  timeout_ms, QS_HOTKEY | QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  return result == WAIT_OBJECT_0; // Messages available
}

// Window move/resize detection using SetWinEventHook
namespace {
std::atomic<bool> g_is_moving{false};
std::atomic<HWND> g_moving_hwnd{nullptr};
std::atomic<bool> g_move_just_ended{false};
HWINEVENTHOOK g_move_start_hook = nullptr;
HWINEVENTHOOK g_move_end_hook = nullptr;

void CALLBACK move_size_hook_proc(HWINEVENTHOOK /*hWinEventHook*/, DWORD event, HWND hwnd,
                                  LONG idObject, LONG idChild, DWORD /*idEventThread*/,
                                  DWORD /*dwmsEventTime*/) {
  // Only handle window events (OBJID_WINDOW == 0 and CHILDID_SELF == 0)
  if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
    return;
  }

  if (event == EVENT_SYSTEM_MOVESIZESTART) {
    g_moving_hwnd = hwnd;
    g_is_moving = true;
    g_move_just_ended = false;
    spdlog::trace("Window move/resize started: hwnd={}", static_cast<void*>(hwnd));
  } else if (event == EVENT_SYSTEM_MOVESIZEEND) {
    g_is_moving = false;
    g_move_just_ended = true;
    spdlog::trace("Window move/resize ended: hwnd={}", static_cast<void*>(g_moving_hwnd.load()));
  }
}
} // namespace

void register_move_size_hook() {
  g_move_start_hook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZESTART,
                                      nullptr, move_size_hook_proc, 0, 0, WINEVENT_OUTOFCONTEXT);
  g_move_end_hook = SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND, nullptr,
                                    move_size_hook_proc, 0, 0, WINEVENT_OUTOFCONTEXT);

  if (g_move_start_hook == nullptr || g_move_end_hook == nullptr) {
    spdlog::error("Failed to register move/size hooks");
  } else {
    spdlog::info("Registered window move/size hooks");
  }
}

void unregister_move_size_hook() {
  if (g_move_start_hook != nullptr) {
    UnhookWinEvent(g_move_start_hook);
    g_move_start_hook = nullptr;
  }
  if (g_move_end_hook != nullptr) {
    UnhookWinEvent(g_move_end_hook);
    g_move_end_hook = nullptr;
  }
  spdlog::info("Unregistered window move/size hooks");
}

static bool is_any_window_being_moved() {
  return g_is_moving.load();
}

static std::optional<DragInfo> get_drag_info() {
  HWND hwnd = g_moving_hwnd.load();
  if (hwnd == nullptr) {
    return std::nullopt;
  }
  return DragInfo{reinterpret_cast<HWND_T>(hwnd), g_move_just_ended.load()};
}

void clear_drag_ended() {
  g_move_just_ended = false;
  g_moving_hwnd = nullptr;
}

// Session/Power notification handling
namespace {
// GUID for display power state notifications
// {6FE69556-704A-47A0-8F24-C28D936FDA47}
static const GUID GUID_CONSOLE_DISPLAY_STATE_LOCAL = {
    0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

// Hidden window for receiving notifications
HWND g_notification_hwnd = nullptr;
HPOWERNOTIFY g_power_notify_handle = nullptr;

// Pause state flags (atomic for thread safety)
std::atomic<bool> g_session_locked{false};
std::atomic<bool> g_system_suspended{false};
std::atomic<bool> g_display_off{false};

// Track if we've received initial display state (to avoid spurious "resuming" on startup)
std::atomic<bool> g_display_state_initialized{false};

// Event for blocking wait
HANDLE g_resume_event = nullptr;

const wchar_t* NOTIFICATION_WINDOW_CLASS = L"WinTilerNotificationWindow";

LRESULT CALLBACK notification_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_WTSSESSION_CHANGE:
    if (wParam == WTS_SESSION_LOCK) {
      g_session_locked = true;
      if (g_resume_event) {
        ResetEvent(g_resume_event);
      }
      spdlog::info("Session locked - pausing");
    } else if (wParam == WTS_SESSION_UNLOCK) {
      g_session_locked = false;
      if (g_resume_event && !g_system_suspended && !g_display_off) {
        SetEvent(g_resume_event);
      }
      spdlog::info("Session unlocked - resuming");
    }
    return 0;

  case WM_POWERBROADCAST:
    if (wParam == PBT_APMSUSPEND) {
      g_system_suspended = true;
      if (g_resume_event) {
        ResetEvent(g_resume_event);
      }
      spdlog::info("System suspending - pausing");
    } else if (wParam == PBT_APMRESUMESUSPEND || wParam == PBT_APMRESUMEAUTOMATIC) {
      g_system_suspended = false;
      if (g_resume_event && !g_session_locked && !g_display_off) {
        SetEvent(g_resume_event);
      }
      spdlog::info("System resumed - resuming");
    } else if (wParam == PBT_POWERSETTINGCHANGE) {
      auto* setting = reinterpret_cast<POWERBROADCAST_SETTING*>(lParam);
      if (setting && IsEqualGUID(setting->PowerSetting, GUID_CONSOLE_DISPLAY_STATE_LOCAL)) {
        DWORD state = *reinterpret_cast<DWORD*>(setting->Data);
        bool was_initialized = g_display_state_initialized.exchange(true);
        if (state == 0) { // Display off
          g_display_off = true;
          if (g_resume_event) {
            ResetEvent(g_resume_event);
          }
          spdlog::info("Display off - pausing");
        } else { // Display on or dimmed
          g_display_off = false;
          if (g_resume_event && !g_session_locked && !g_system_suspended) {
            SetEvent(g_resume_event);
          }
          // Only log if this isn't the initial state notification on startup
          if (was_initialized) {
            spdlog::info("Display on - resuming");
          }
        }
      }
    }
    return TRUE; // Must return TRUE for power messages

  default:
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

bool create_notification_window() {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = notification_wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = NOTIFICATION_WINDOW_CLASS;

  if (!RegisterClassExW(&wc)) {
    // Class may already be registered
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      spdlog::error("Failed to register notification window class, error={}", GetLastError());
      return false;
    }
  }

  g_notification_hwnd =
      CreateWindowExW(0, NOTIFICATION_WINDOW_CLASS, L"WinTiler Notifications", 0, 0, 0, 0, 0,
                      HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);

  if (!g_notification_hwnd) {
    spdlog::error("Failed to create notification window, error={}", GetLastError());
    return false;
  }

  return true;
}
} // namespace

void register_session_power_notifications() {
  // Create event for blocking wait (manual reset, initially signaled)
  g_resume_event = CreateEventW(nullptr, TRUE, TRUE, nullptr);
  if (!g_resume_event) {
    spdlog::error("Failed to create resume event, error={}", GetLastError());
    return;
  }

  // Create hidden window for notifications
  if (!create_notification_window()) {
    spdlog::error("Failed to create notification window");
    CloseHandle(g_resume_event);
    g_resume_event = nullptr;
    return;
  }

  // Register for session notifications (lock/unlock)
  if (!WTSRegisterSessionNotification(g_notification_hwnd, NOTIFY_FOR_THIS_SESSION)) {
    spdlog::error("Failed to register session notifications, error={}", GetLastError());
  }

  // Register for display power state changes
  g_power_notify_handle = RegisterPowerSettingNotification(
      g_notification_hwnd, &GUID_CONSOLE_DISPLAY_STATE_LOCAL, DEVICE_NOTIFY_WINDOW_HANDLE);
  if (!g_power_notify_handle) {
    spdlog::error("Failed to register power setting notification, error={}", GetLastError());
  }

  spdlog::info("Registered session/power notifications");
}

void unregister_session_power_notifications() {
  if (g_notification_hwnd) {
    WTSUnRegisterSessionNotification(g_notification_hwnd);

    if (g_power_notify_handle) {
      UnregisterPowerSettingNotification(g_power_notify_handle);
      g_power_notify_handle = nullptr;
    }

    DestroyWindow(g_notification_hwnd);
    g_notification_hwnd = nullptr;
  }

  if (g_resume_event) {
    CloseHandle(g_resume_event);
    g_resume_event = nullptr;
  }

  // Reset state
  g_session_locked = false;
  g_system_suspended = false;
  g_display_off = false;
  g_display_state_initialized = false;

  spdlog::info("Unregistered session/power notifications");
}

void wait_for_session_active() {
  if (!g_resume_event) {
    return;
  }

  // Process messages while waiting to ensure notifications are received
  while (g_session_locked || g_system_suspended || g_display_off) {
    DWORD result = MsgWaitForMultipleObjects(1, &g_resume_event, FALSE, INFINITE, QS_ALLINPUT);

    if (result == WAIT_OBJECT_0) {
      // Event signaled - session is active
      break;
    } else if (result == WAIT_OBJECT_0 + 1) {
      // Messages available - process them
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
  }
}

bool is_session_paused() {
  return g_session_locked || g_system_suspended || g_display_off;
}

static bool is_ctrl_pressed() {
  return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
}

static bool is_window_fullscreen(HWND_T hwnd) {
  if (hwnd == nullptr) {
    return false;
  }

  HWND win = reinterpret_cast<HWND>(hwnd);

  // Skip invisible windows
  if (!IsWindowVisible(win)) {
    return false;
  }

  // Get the monitor this window is on
  HMONITOR monitor = MonitorFromWindow(win, MONITOR_DEFAULTTONULL);
  if (monitor == nullptr) {
    return false;
  }

  // Get monitor info
  MONITORINFO mi;
  mi.cbSize = sizeof(MONITORINFO);
  if (!GetMonitorInfoA(monitor, &mi)) {
    return false;
  }

  // Get window rect
  RECT windowRect;
  if (!GetWindowRect(win, &windowRect)) {
    return false;
  }

  // Check if window covers the entire monitor (use full rect, not work area)
  return windowRect.left <= mi.rcMonitor.left && windowRect.top <= mi.rcMonitor.top &&
         windowRect.right >= mi.rcMonitor.right && windowRect.bottom >= mi.rcMonitor.bottom;
}

LoopInputState gather_loop_input_state(const wintiler::IgnoreOptions& ignore_options) {
  LoopInputState state;

  // Gather monitor and window data
  state.monitors = get_monitors();
  state.windows_per_monitor.reserve(state.monitors.size());

  auto all_handles = gather_raw_window_data(ignore_options);

  for (size_t i = 0; i < state.monitors.size(); ++i) {
    const auto& monitor = state.monitors[i];
    std::vector<ManagedWindowInfo> monitor_windows;

    for (const auto& hwnd : all_handles) {
      HMONITOR winMonitor = MonitorFromWindow((HWND)hwnd, MONITOR_DEFAULTTONULL);
      if (winMonitor == (HMONITOR)monitor.handle) {
        ManagedWindowInfo managed_info;
        managed_info.handle = hwnd;
        managed_info.is_fullscreen = is_window_fullscreen(hwnd);
        monitor_windows.push_back(managed_info);
      }
    }

    state.windows_per_monitor.push_back(std::move(monitor_windows));
  }

  // Gather input state
  state.is_any_window_being_moved = is_any_window_being_moved();
  state.drag_info = get_drag_info();
  state.cursor_pos = get_cursor_pos();
  state.is_ctrl_pressed = is_ctrl_pressed();
  state.foreground_window = get_foreground_window();

  return state;
}

} // namespace winapi
