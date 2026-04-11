#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <variant>
#include <vector>

#include "options.h"

namespace wintiler {

struct AgentListWindowsRequest {
  bool managed_only = false;
};

struct AgentGetStateRequest {
  bool include_layout = true;
};

struct AgentFocusWindowRequest {
  size_t leaf_id = 0;
  bool select = true;
};

struct AgentSendActionRequest {
  HotkeyAction action = HotkeyAction::NavigateLeft;
};

struct AgentSwapWindowsRequest {
  size_t first_leaf_id = 0;
  size_t second_leaf_id = 0;
};

struct AgentMoveWindowToMonitorRequest {
  size_t leaf_id = 0;
  int target_monitor_index = 0;
  std::optional<size_t> anchor_leaf_id;
};

struct AgentRetileRequest {};

using AgentRequestPayload =
    std::variant<AgentListWindowsRequest, AgentGetStateRequest, AgentFocusWindowRequest,
                 AgentSendActionRequest, AgentSwapWindowsRequest, AgentMoveWindowToMonitorRequest,
                 AgentRetileRequest>;

struct AgentRequest {
  std::string id;
  AgentRequestPayload payload;
};

struct AgentRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct AgentWindowSnapshot {
  std::string window_id;
  std::optional<unsigned long> pid;
  std::string process_name;
  std::string title;
  std::string class_name;
  int monitor_index = -1;
  std::optional<int> cluster_index;
  std::optional<int> cell_index;
  AgentRect rect;
  std::optional<AgentRect> actual_rect;
  std::optional<AgentRect> layout_rect;
  bool is_managed = false;
  bool is_foreground = false;
  bool is_maximized = false;
  bool is_fullscreen = false;
};

struct AgentSelectionSnapshot {
  std::string window_id;
  int cluster_index = 0;
  int cell_index = 0;
};

struct AgentStateResponse {
  std::optional<std::string> desktop_id;
  std::string split_mode;
  std::vector<AgentWindowSnapshot> windows;
  std::optional<AgentSelectionSnapshot> selection;
};

struct AgentMutationResponse {
  bool selection_changed = false;
  bool layout_changed = false;
  std::optional<std::string> focused_window_id;
  std::optional<std::string> toast_message;
};

using AgentResponsePayload = std::variant<AgentStateResponse, AgentMutationResponse>;

struct AgentResponse {
  std::string id;
  bool ok = false;
  std::optional<std::string> error;
  std::optional<AgentResponsePayload> payload;
};

[[nodiscard]] std::string format_window_id(size_t leaf_id);

[[nodiscard]] tl::expected<size_t, std::string> parse_window_id(std::string_view window_id);

[[nodiscard]] tl::expected<AgentRequest, std::string> parse_agent_request(std::string_view text);

[[nodiscard]] std::string serialize_agent_response(const AgentResponse& response);

} // namespace wintiler
