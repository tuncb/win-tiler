#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

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

struct SmallWindowBarrier {
  int width;
  int height;
};

struct IgnoreOptions {
  std::vector<std::string> ignored_processes;
  std::vector<std::string> ignored_window_titles;
  std::vector<std::pair<std::string, std::string>> ignored_process_title_pairs;
  std::optional<SmallWindowBarrier> small_window_barrier;
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
std::vector<WindowInfo> gather_raw_window_data(const IgnoreOptions& ignore_options);
IgnoreOptions get_default_ignore_options();
void log_windows_per_monitor(std::optional<size_t> monitor_index = std::nullopt);
void update_window_position(const TileInfo& tile_info);
std::vector<HWND_T> get_hwnds_for_monitor(size_t monitor_index);

} // namespace winapi
