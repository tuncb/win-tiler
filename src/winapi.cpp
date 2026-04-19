#include "winapi.h"

#include <dwmapi.h>
#include <objbase.h>
#include <psapi.h>
#include <shobjidl.h>
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
#pragma comment(lib, "Ole32.lib")

namespace winapi {

// Helper function for case-insensitive string comparison
static bool iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size())
    return false;
  return std::equal(a.begin(), a.end(), b.begin(),
                    [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

bool should_ignore_owned_dialog_window(bool has_owner, const std::string& class_name) {
  return has_owner && class_name == "#32770";
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

static bool visible_size_differs_from_target(const WindowPosition& target_visible_position,
                                             const WindowPosition& actual_visible_position) {
  constexpr int kSizeTolerance = 2;

  int width_delta = actual_visible_position.width - target_visible_position.width;
  int height_delta = actual_visible_position.height - target_visible_position.height;

  return (actual_visible_position.width > 0 &&
          (width_delta < -kSizeTolerance || width_delta > kSizeTolerance)) ||
         (actual_visible_position.height > 0 &&
          (height_delta < -kSizeTolerance || height_delta > kSizeTolerance));
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

struct WindowEnumContext {
  std::vector<HWND_T>* handles;
  const wintiler::IgnoreOptions* ignore_options;
};

BOOL CALLBACK WindowEnumProc(HWND hwnd, LPARAM lParam) {
  auto* ctx = reinterpret_cast<WindowEnumContext*>(lParam);

  if (!IsWindowVisible(hwnd)) {
    return TRUE;
  }

  BOOL cloaked = FALSE;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
    return TRUE;
  }

  char title[256];
  if (GetWindowTextA(hwnd, title, sizeof(title)) == 0) {
    return TRUE;
  }

  char class_name_buf[256];
  std::string class_name;
  if (GetClassNameA(hwnd, class_name_buf, sizeof(class_name_buf)) > 0) {
    class_name = class_name_buf;
  }

  if (class_name == "SysDragImage") {
    return TRUE;
  }

  if (class_name == "tooltips_class32") {
    return TRUE;
  }

  HWND owner = GetWindow(hwnd, GW_OWNER);
  if (should_ignore_owned_dialog_window(owner != nullptr, class_name)) {
    return TRUE;
  }

  LONG ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
  if (ex_style & WS_EX_TOOLWINDOW) {
    return TRUE;
  }
  if (ex_style & WS_EX_TOPMOST) {
    return TRUE;
  }
  if (ex_style & WS_EX_TRANSPARENT) {
    return TRUE;
  }
  if (ex_style & WS_EX_NOACTIVATE) {
    return TRUE;
  }

  if (IsHungAppWindow(hwnd)) {
    return TRUE;
  }

  auto pid = get_window_pid(reinterpret_cast<HWND_T>(hwnd));
  std::string process_name;
  if (pid.has_value()) {
    process_name = get_process_name_from_pid(*pid);
  }

  if (process_name.empty()) {
    return TRUE;
  }

  const auto& options = *ctx->ignore_options;

  for (const auto& proc : options.ignored_processes) {
    if (process_name == proc) {
      return TRUE;
    }
  }

  for (const auto& ignored_title : options.ignored_window_titles) {
    if (title == ignored_title) {
      return TRUE;
    }
  }

  for (const auto& pair : options.ignored_process_title_pairs) {
    if (iequals(process_name, pair.first) && iequals(title, pair.second)) {
      return TRUE;
    }
  }

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

  if (!options.ignore_children_of_processes.empty()) {
    HWND owner = GetWindow(hwnd, GW_OWNER);
    HWND parent = GetParent(hwnd);

    if (owner != nullptr || parent != nullptr) {
      for (const auto& proc : options.ignore_children_of_processes) {
        if (iequals(process_name, proc)) {
          return TRUE;
        }
      }
    }
  }

  ctx->handles->push_back(reinterpret_cast<HWND_T>(hwnd));
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

  std::sort(handles.begin(), handles.end(), [](HWND_T lhs, HWND_T rhs) {
    return reinterpret_cast<uintptr_t>(lhs) < reinterpret_cast<uintptr_t>(rhs);
  });

  return handles;
}

static bool is_window_fullscreen(HWND_T hwnd);

struct WindowManagementState {
  HWND_T handle = nullptr;
  std::string title;
  std::string class_name;
  std::optional<DWORD_T> pid;
  std::string process_name;
  std::optional<Rect> rect;
  long ex_style = 0;
  bool is_visible = false;
  bool is_cloaked = false;
  bool has_title = false;
  bool is_sys_drag_image = false;
  bool is_tooltip = false;
  bool is_tool_window = false;
  bool is_topmost = false;
  bool is_transparent = false;
  bool is_no_activate = false;
  bool is_hung = false;
  bool has_owner = false;
  bool has_parent = false;
  bool is_child_or_owned = false;
  bool is_owned_dialog = false;
  bool process_name_missing = false;
  bool matches_ignored_process = false;
  bool matches_ignored_title = false;
  bool matches_ignored_process_title_pair = false;
  bool is_below_small_window_barrier = false;
  bool is_ignored_child_of_process = false;
  bool is_maximized = false;
  bool is_fullscreen = false;
  bool passes_filters = false;
  bool is_currently_managed = false;
  std::optional<size_t> monitor_index;
  std::vector<std::string> reasons;
};

struct DumpWindowEnumContext {
  std::vector<WindowManagementState>* windows;
  const wintiler::IgnoreOptions* ignore_options;
  const std::vector<MonitorInfo>* monitors;
};

static std::optional<Rect> get_raw_window_rect(HWND hwnd) {
  RECT rect;
  if (GetWindowRect(hwnd, &rect) == 0) {
    return std::nullopt;
  }

  return Rect{rect.left, rect.top, rect.right, rect.bottom};
}

static std::optional<size_t>
get_monitor_index_for_window(HWND hwnd, const std::vector<MonitorInfo>& monitors) {
  HMONITOR window_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
  if (window_monitor == nullptr) {
    return std::nullopt;
  }

  for (size_t i = 0; i < monitors.size(); ++i) {
    if (window_monitor == reinterpret_cast<HMONITOR>(monitors[i].handle)) {
      return i;
    }
  }

  return std::nullopt;
}

static void add_reason_if(std::vector<std::string>& reasons, bool condition, const char* reason) {
  if (condition) {
    reasons.emplace_back(reason);
  }
}

static std::string join_reasons(const std::vector<std::string>& reasons) {
  if (reasons.empty()) {
    return "none";
  }

  std::string joined;
  for (size_t i = 0; i < reasons.size(); ++i) {
    if (i > 0) {
      joined += ", ";
    }
    joined += reasons[i];
  }
  return joined;
}

static WindowManagementState
inspect_window_management_state(HWND hwnd, const wintiler::IgnoreOptions& options,
                                const std::vector<MonitorInfo>& monitors) {
  WindowManagementState state;
  state.handle = reinterpret_cast<HWND_T>(hwnd);
  state.is_visible = IsWindowVisible(hwnd) != 0;

  BOOL cloaked = FALSE;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
    state.is_cloaked = cloaked != FALSE;
  }

  char title[256] = {};
  int title_length = GetWindowTextA(hwnd, title, sizeof(title));
  if (title_length > 0) {
    state.title = title;
  }
  state.has_title = title_length > 0;

  char class_name_buf[256] = {};
  if (GetClassNameA(hwnd, class_name_buf, sizeof(class_name_buf)) > 0) {
    state.class_name = class_name_buf;
  }

  state.is_sys_drag_image = state.class_name == "SysDragImage";
  state.is_tooltip = state.class_name == "tooltips_class32";

  state.ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
  state.is_tool_window = (state.ex_style & WS_EX_TOOLWINDOW) != 0;
  state.is_topmost = (state.ex_style & WS_EX_TOPMOST) != 0;
  state.is_transparent = (state.ex_style & WS_EX_TRANSPARENT) != 0;
  state.is_no_activate = (state.ex_style & WS_EX_NOACTIVATE) != 0;
  state.is_hung = IsHungAppWindow(hwnd) != 0;

  state.pid = get_window_pid(state.handle);
  if (state.pid.has_value()) {
    state.process_name = get_process_name_from_pid(*state.pid);
  }
  state.process_name_missing = state.process_name.empty();

  state.rect = get_raw_window_rect(hwnd);

  HWND owner = GetWindow(hwnd, GW_OWNER);
  HWND parent = GetParent(hwnd);
  state.has_owner = owner != nullptr;
  state.has_parent = parent != nullptr;
  state.is_child_or_owned = state.has_owner || state.has_parent;
  state.is_owned_dialog = should_ignore_owned_dialog_window(state.has_owner, state.class_name);

  for (const auto& proc : options.ignored_processes) {
    if (state.process_name == proc) {
      state.matches_ignored_process = true;
      break;
    }
  }

  for (const auto& ignored_title : options.ignored_window_titles) {
    if (state.title == ignored_title) {
      state.matches_ignored_title = true;
      break;
    }
  }

  for (const auto& pair : options.ignored_process_title_pairs) {
    if (iequals(state.process_name, pair.first) && iequals(state.title, pair.second)) {
      state.matches_ignored_process_title_pair = true;
      break;
    }
  }

  if (options.small_window_barrier.has_value() && state.rect.has_value()) {
    int width = static_cast<int>(state.rect->right - state.rect->left);
    int height = static_cast<int>(state.rect->bottom - state.rect->top);
    state.is_below_small_window_barrier = width < options.small_window_barrier->width ||
                                          height < options.small_window_barrier->height;
  }

  if (state.is_child_or_owned && !options.ignore_children_of_processes.empty()) {
    for (const auto& proc : options.ignore_children_of_processes) {
      if (iequals(state.process_name, proc)) {
        state.is_ignored_child_of_process = true;
        break;
      }
    }
  }

  state.is_maximized = is_window_maximized(state.handle);
  state.is_fullscreen = is_window_fullscreen(state.handle);
  state.monitor_index = get_monitor_index_for_window(hwnd, monitors);

  add_reason_if(state.reasons, !state.is_visible, "not visible");
  add_reason_if(state.reasons, state.is_cloaked, "cloaked");
  add_reason_if(state.reasons, !state.has_title, "empty title");
  add_reason_if(state.reasons, state.is_sys_drag_image, "SysDragImage window");
  add_reason_if(state.reasons, state.is_tooltip, "tooltip window");
  add_reason_if(state.reasons, state.is_tool_window, "WS_EX_TOOLWINDOW");
  add_reason_if(state.reasons, state.is_topmost, "WS_EX_TOPMOST");
  add_reason_if(state.reasons, state.is_transparent, "WS_EX_TRANSPARENT");
  add_reason_if(state.reasons, state.is_no_activate, "WS_EX_NOACTIVATE");
  add_reason_if(state.reasons, state.is_hung, "hung window");
  add_reason_if(state.reasons, state.process_name_missing, "missing process name");
  add_reason_if(state.reasons, state.matches_ignored_process, "ignored process");
  add_reason_if(state.reasons, state.matches_ignored_title, "ignored title");
  add_reason_if(state.reasons, state.matches_ignored_process_title_pair,
                "ignored process/title pair");
  add_reason_if(state.reasons, state.is_below_small_window_barrier, "below small window barrier");
  add_reason_if(state.reasons, state.is_owned_dialog, "owned #32770 dialog");
  add_reason_if(state.reasons, state.is_ignored_child_of_process, "ignored child/owned window");

  state.passes_filters = state.reasons.empty();
  if (!state.monitor_index.has_value()) {
    state.reasons.emplace_back("not on a known monitor");
  }
  state.is_currently_managed = state.passes_filters && state.monitor_index.has_value();

  return state;
}

BOOL CALLBACK DumpWindowEnumProc(HWND hwnd, LPARAM lParam) {
  auto* ctx = reinterpret_cast<DumpWindowEnumContext*>(lParam);
  ctx->windows->push_back(
      inspect_window_management_state(hwnd, *ctx->ignore_options, *ctx->monitors));
  return TRUE;
}

static std::vector<WindowManagementState>
gather_window_management_states(const wintiler::IgnoreOptions& ignore_options,
                                const std::vector<MonitorInfo>& monitors) {
  std::vector<WindowManagementState> windows;
  DumpWindowEnumContext ctx{&windows, &ignore_options, &monitors};
  EnumWindows(DumpWindowEnumProc, (LPARAM)&ctx);

  std::sort(windows.begin(), windows.end(),
            [](const WindowManagementState& lhs, const WindowManagementState& rhs) {
              return reinterpret_cast<uintptr_t>(lhs.handle) <
                     reinterpret_cast<uintptr_t>(rhs.handle);
            });

  return windows;
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

void dump_window_management_state(const wintiler::IgnoreOptions& ignore_options) {
  auto monitors = get_monitors();
  auto windows = gather_window_management_states(ignore_options, monitors);

  spdlog::info("=== Window Management Dump ({} windows, {} monitors) ===", windows.size(),
               monitors.size());
  if (ignore_options.small_window_barrier.has_value()) {
    spdlog::info("Small window barrier: {}x{}", ignore_options.small_window_barrier->width,
                 ignore_options.small_window_barrier->height);
  } else {
    spdlog::info("Small window barrier: disabled");
  }

  for (size_t i = 0; i < windows.size(); ++i) {
    const auto& window = windows[i];
    auto rect_text = std::string("N/A");
    auto size_text = std::string("N/A");
    if (window.rect.has_value()) {
      rect_text = std::to_string(window.rect->left) + ", " + std::to_string(window.rect->top) +
                  ", " + std::to_string(window.rect->right) + ", " +
                  std::to_string(window.rect->bottom);
      size_text = std::to_string(window.rect->right - window.rect->left) + "x" +
                  std::to_string(window.rect->bottom - window.rect->top);
    }

    spdlog::info("[{}] hwnd={}, managed={}, passes_filters={}, monitor_index={}, reasons={}", i,
                 window.handle, window.is_currently_managed, window.passes_filters,
                 window.monitor_index.has_value() ? std::to_string(*window.monitor_index) : "N/A",
                 join_reasons(window.reasons));
    spdlog::info("     title=\"{}\"", window.title);
    spdlog::info("     class=\"{}\"", window.class_name);
    spdlog::info("     pid={}, process=\"{}\"",
                 window.pid.has_value() ? std::to_string(*window.pid) : "N/A", window.process_name);
    spdlog::info("     rect=[{}], size={}", rect_text, size_text);
    spdlog::info("     visible={}, cloaked={}, has_title={}, hung={}", window.is_visible,
                 window.is_cloaked, window.has_title, window.is_hung);
    spdlog::info("     ex_style=0x{:08X}, tool_window={}, topmost={}, transparent={}, "
                 "no_activate={}",
                 static_cast<unsigned long>(window.ex_style), window.is_tool_window,
                 window.is_topmost, window.is_transparent, window.is_no_activate);
    spdlog::info("     owner_present={}, parent_present={}, child_or_owned={}", window.has_owner,
                 window.has_parent, window.is_child_or_owned);
    spdlog::info("     ignored_process={}, ignored_title={}, ignored_process_title_pair={}, "
                 "ignored_child_of_process={}",
                 window.matches_ignored_process, window.matches_ignored_title,
                 window.matches_ignored_process_title_pair, window.is_ignored_child_of_process);
    spdlog::info("     small_window_barrier_hit={}, maximized={}, fullscreen={}",
                 window.is_below_small_window_barrier, window.is_maximized, window.is_fullscreen);
  }

  spdlog::info("=== End Window Management Dump ===");
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

    auto actual_visible_rect = get_window_rect(tile_info.handle);
    if (actual_visible_rect.has_value() &&
        visible_size_differs_from_target(tile_info.window_position, *actual_visible_rect)) {
      SetWindowPos(hwnd, NULL, targetX, targetY, targetW, targetH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
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

    auto actual_visible_rect = get_window_rect(tile_info.handle);
    if (actual_visible_rect.has_value() &&
        visible_size_differs_from_target(tile_info.window_position, *actual_visible_rect)) {
      SetWindowPos(hwnd, NULL, targetX, targetY, targetW, targetH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
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

// ============================================================================
// Virtual Desktop Detection
// ============================================================================
// Uses IVirtualDesktopManager COM interface to get desktop IDs from windows.
// The desktop ID is returned in LoopInputState::desktop_id for comparison
// in the main loop.

// Global state for virtual desktop detection
static IVirtualDesktopManager* g_vdm = nullptr;
static bool g_com_initialized = false;

void register_virtual_desktop_notifications() {
  // Initialize COM on this thread
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    spdlog::error("Failed to initialize COM for virtual desktop detection, hr=0x{:08X}", hr);
    return;
  }
  g_com_initialized = true;

  // Create IVirtualDesktopManager instance
  hr = CoCreateInstance(__uuidof(VirtualDesktopManager), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&g_vdm));
  if (FAILED(hr) || !g_vdm) {
    spdlog::error("Failed to create VirtualDesktopManager, hr=0x{:08X}", hr);
    if (g_com_initialized) {
      CoUninitialize();
      g_com_initialized = false;
    }
    return;
  }

  spdlog::debug("Initialized virtual desktop manager");
}

void unregister_virtual_desktop_notifications() {
  if (g_vdm) {
    g_vdm->Release();
    g_vdm = nullptr;
  }

  if (g_com_initialized) {
    CoUninitialize();
    g_com_initialized = false;
  }

  spdlog::debug("Unregistered virtual desktop notifications");
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
        managed_info.is_maximized = is_window_maximized(hwnd);
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

  // Get desktop ID from first managed window
  if (g_vdm && !all_handles.empty()) {
    GUID desktop_guid;
    HRESULT hr = g_vdm->GetWindowDesktopId((HWND)all_handles[0], &desktop_guid);
    if (SUCCEEDED(hr)) {
      // Format GUID as string: {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
      char guid_str[64];
      snprintf(guid_str, sizeof(guid_str), "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
               desktop_guid.Data1, desktop_guid.Data2, desktop_guid.Data3, desktop_guid.Data4[0],
               desktop_guid.Data4[1], desktop_guid.Data4[2], desktop_guid.Data4[3],
               desktop_guid.Data4[4], desktop_guid.Data4[5], desktop_guid.Data4[6],
               desktop_guid.Data4[7]);
      state.desktop_id = guid_str;
    }
  }

  return state;
}

} // namespace winapi
