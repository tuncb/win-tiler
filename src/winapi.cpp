#include "winapi.h"

#include <dwmapi.h>
#include <psapi.h>
#include <spdlog/spdlog.h>
#include <windows.h>

#include <algorithm>
#include <cctype>

// Link with Psapi.lib
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Dwmapi.lib")

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

std::optional<DWORD_T> get_window_pid(HWND_T hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId((HWND)hwnd, &pid);
  if (pid != 0) {
    return pid;
  }
  return std::nullopt;
}

std::string get_process_name_from_pid(DWORD_T pid) {
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

bool is_window_maximized(HWND_T hwnd) {
  return IsZoomed((HWND)hwnd);
}

BOOL CALLBACK WindowEnumProc(HWND hwnd, LPARAM lParam) {
  std::vector<WindowInfo>* windows = reinterpret_cast<std::vector<WindowInfo>*>(lParam);

  if (!IsWindowVisible(hwnd)) {
    return TRUE;
  }

  char title[256];
  if (GetWindowTextA(hwnd, title, sizeof(title)) == 0) {
    return TRUE;
  }

  WindowInfo info;
  info.handle = (HWND_T)hwnd;
  info.title = title;
  info.pid = get_window_pid((HWND_T)hwnd);
  if (info.pid.has_value()) {
    info.processName = get_process_name_from_pid(info.pid.value());
  }

  windows->push_back(info);
  return TRUE;
}

std::vector<WindowInfo> get_windows_list() {
  std::vector<WindowInfo> windows;
  EnumWindows(WindowEnumProc, (LPARAM)&windows);
  return windows;
}

bool is_ignored(const IgnoreOptions& options, const WindowInfo& win) {
  // Check ignored processes
  for (const auto& proc : options.ignored_processes) {
    if (win.processName == proc)
      return true;
  }

  // Check ignored titles
  for (const auto& title : options.ignored_window_titles) {
    if (win.title == title)
      return true;
  }

  // Check ignored process/title pairs
  for (const auto& pair : options.ignored_process_title_pairs) {
    if (iequals(win.processName, pair.first) && iequals(win.title, pair.second))
      return true;
  }

  // Check small window barrier
  if (options.small_window_barrier.has_value()) {
    RECT rect;
    if (GetWindowRect((HWND)win.handle, &rect)) {
      int width = rect.right - rect.left;
      int height = rect.bottom - rect.top;
      if (width < options.small_window_barrier->width ||
          height < options.small_window_barrier->height) {
        return true;
      }
    }
  }

  return false;
}

std::vector<WindowInfo> gather_raw_window_data(const IgnoreOptions& ignore_options) {
  auto windows = get_windows_list();
  std::vector<WindowInfo> filtered_windows;

  for (const auto& win : windows) {
    if (win.processName.empty())
      continue;
    if (win.title.empty())
      continue;
    if (is_ignored(ignore_options, win))
      continue;
    filtered_windows.push_back(win);
  }

  std::sort(filtered_windows.begin(), filtered_windows.end(),
            [](const WindowInfo& a, const WindowInfo& b) {
              return (uintptr_t)a.handle < (uintptr_t)b.handle;
            });

  return filtered_windows;
}

IgnoreOptions get_default_ignore_options() {
  IgnoreOptions options;
  options.ignored_processes = {
      "TextInputHost.exe",
      "ApplicationFrameHost.exe",
      "Microsoft.CmdPal.UI.exe",
      "PowerToys.PowerLauncher.exe",
  };
  options.ignored_window_titles = {};
  options.ignored_process_title_pairs = {{"SystemSettings.exe", "Settings"},
                                         {"explorer.exe", "Program Manager"},
                                         {"explorer.exe", "System tray overflow window."},
                                         {"explorer.exe", "PopupHost"}};
  options.small_window_barrier = SmallWindowBarrier{50, 50};
  return options;
}

void log_windows_per_monitor(std::optional<size_t> monitor_index) {
  auto monitors = get_monitors();
  auto options = get_default_ignore_options();
  auto windows = gather_raw_window_data(options);

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
    for (const auto& win : windows) {
      HMONITOR winMonitor = MonitorFromWindow((HWND)win.handle, MONITOR_DEFAULTTONULL);
      if (winMonitor == (HMONITOR)monitor.handle) {
        RECT rect;
        GetWindowRect((HWND)win.handle, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        spdlog::debug("    Handle: {}, PID: {}, Process: {}, Title: {}", win.handle,
                      win.pid.has_value() ? std::to_string(win.pid.value()) : "N/A",
                      win.processName, win.title);
        spdlog::debug("      Position: ({}, {}), Size: {}x{}", rect.left, rect.top, width, height);
      }
    }
    spdlog::debug("--------------------------------------------------");
  }
}

void update_window_position(const TileInfo& tile_info) {
  SetWindowPos((HWND)tile_info.handle, NULL, tile_info.window_position.x,
               tile_info.window_position.y, tile_info.window_position.width,
               tile_info.window_position.height, SWP_NOZORDER | SWP_NOACTIVATE);
}

std::vector<HWND_T> get_hwnds_for_monitor(size_t monitor_index) {
  std::vector<HWND_T> hwnds;
  auto monitors = get_monitors();

  if (monitor_index >= monitors.size()) {
    return hwnds;
  }

  auto options = get_default_ignore_options();
  auto windows = gather_raw_window_data(options);
  const auto& monitor = monitors[monitor_index];

  for (const auto& win : windows) {
    HMONITOR winMonitor = MonitorFromWindow((HWND)win.handle, MONITOR_DEFAULTTONULL);
    if (winMonitor == (HMONITOR)monitor.handle) {
      hwnds.push_back(win.handle);
    }
  }

  return hwnds;
}

HWND_T get_foreground_window() {
  return reinterpret_cast<HWND_T>(GetForegroundWindow());
}

std::optional<Point> get_cursor_pos() {
  POINT pt;
  if (GetCursorPos(&pt)) {
    return Point{pt.x, pt.y};
  }
  spdlog::error("GetCursorPos failed");
  return std::nullopt;
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
      return std::nullopt;  // Unknown modifier
    }
  }

  // Parse key - only single character keys supported
  if (keyStr.length() != 1) {
    return std::nullopt;
  }

  unsigned int key = static_cast<unsigned int>(
      std::toupper(static_cast<unsigned char>(keyStr[0])));

  return HotKeyInfo{id, modifiers, key};
}

bool register_hotkey(const HotKeyInfo& hotkey) {
  BOOL result = RegisterHotKey(nullptr, hotkey.id, hotkey.modifiers, hotkey.key);
  return result != 0;
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

} // namespace winapi
