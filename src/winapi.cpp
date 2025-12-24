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

} // namespace winapi
