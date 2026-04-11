#include "agent_protocol.h"

#include <iomanip>
#include <magic_enum/magic_enum.hpp>
#include <nlohmann/json.hpp>
#include <sstream>

namespace wintiler {

namespace {

using json = nlohmann::json;

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

tl::expected<std::string, std::string> get_required_string(const json& obj, const char* key) {
  auto it = obj.find(key);
  if (it == obj.end() || !it->is_string()) {
    return tl::unexpected(std::string(key) + " must be a string");
  }
  return it->get<std::string>();
}

tl::expected<int, std::string> get_required_int(const json& obj, const char* key) {
  auto it = obj.find(key);
  if (it == obj.end() || !it->is_number_integer()) {
    return tl::unexpected(std::string(key) + " must be an integer");
  }
  return it->get<int>();
}

tl::expected<bool, std::string> get_optional_bool(const json& obj, const char* key,
                                                  bool default_value) {
  auto it = obj.find(key);
  if (it == obj.end()) {
    return default_value;
  }
  if (!it->is_boolean()) {
    return tl::unexpected(std::string(key) + " must be a boolean");
  }
  return it->get<bool>();
}

tl::expected<size_t, std::string> get_required_window_id(const json& obj, const char* key) {
  auto value = get_required_string(obj, key);
  if (!value.has_value()) {
    return tl::unexpected(value.error());
  }
  return parse_window_id(*value);
}

tl::expected<std::optional<size_t>, std::string> get_optional_window_id(const json& obj,
                                                                        const char* key) {
  auto it = obj.find(key);
  if (it == obj.end() || it->is_null()) {
    return std::nullopt;
  }
  if (!it->is_string()) {
    return tl::unexpected(std::string(key) + " must be a string");
  }
  auto parsed = parse_window_id(it->get<std::string>());
  if (!parsed.has_value()) {
    return tl::unexpected(parsed.error());
  }
  return *parsed;
}

json to_json_value(const AgentRect& rect) {
  return json{{"x", rect.x}, {"y", rect.y}, {"width", rect.width}, {"height", rect.height}};
}

json to_json_value(const AgentWindowSnapshot& window) {
  json value = {
      {"window_id", window.window_id},
      {"pid", window.pid.has_value() ? json(*window.pid) : json(nullptr)},
      {"process_name", window.process_name},
      {"title", window.title},
      {"class_name", window.class_name},
      {"monitor_index", window.monitor_index},
      {"cluster_index",
       window.cluster_index.has_value() ? json(*window.cluster_index) : json(nullptr)},
      {"cell_index", window.cell_index.has_value() ? json(*window.cell_index) : json(nullptr)},
      {"rect", to_json_value(window.rect)},
      {"is_managed", window.is_managed},
      {"is_foreground", window.is_foreground},
      {"is_maximized", window.is_maximized},
      {"is_fullscreen", window.is_fullscreen}};
  return value;
}

json to_json_value(const AgentSelectionSnapshot& selection) {
  return json{{"window_id", selection.window_id},
              {"cluster_index", selection.cluster_index},
              {"cell_index", selection.cell_index}};
}

json to_json_value(const AgentStateResponse& state) {
  json windows = json::array();
  for (const auto& window : state.windows) {
    windows.push_back(to_json_value(window));
  }

  json value = {
      {"desktop_id", state.desktop_id.has_value() ? json(*state.desktop_id) : json(nullptr)},
      {"split_mode", state.split_mode},
      {"windows", std::move(windows)},
      {"selection", state.selection.has_value() ? to_json_value(*state.selection) : json(nullptr)}};
  return value;
}

json to_json_value(const AgentMutationResponse& mutation) {
  json value = {{"selection_changed", mutation.selection_changed},
                {"layout_changed", mutation.layout_changed},
                {"focused_window_id", mutation.focused_window_id.has_value()
                                          ? json(*mutation.focused_window_id)
                                          : json(nullptr)},
                {"toast_message", mutation.toast_message.has_value() ? json(*mutation.toast_message)
                                                                     : json(nullptr)}};
  return value;
}

} // namespace

std::string format_window_id(size_t leaf_id) {
  std::ostringstream stream;
  stream << "hwnd:" << std::uppercase << std::hex << std::setfill('0')
         << std::setw(static_cast<int>(sizeof(size_t) * 2)) << leaf_id;
  return stream.str();
}

tl::expected<size_t, std::string> parse_window_id(std::string_view window_id) {
  constexpr std::string_view prefix = "hwnd:";
  if (!window_id.starts_with(prefix)) {
    return tl::unexpected("window_id must start with 'hwnd:'");
  }

  std::string hex_value(window_id.substr(prefix.size()));
  if (hex_value.empty()) {
    return tl::unexpected("window_id has invalid hex digits");
  }

  std::istringstream stream(hex_value);
  stream >> std::hex;

  size_t leaf_id = 0;
  stream >> leaf_id;
  if (stream.fail() || !stream.eof()) {
    return tl::unexpected("window_id has invalid hex digits");
  }

  return leaf_id;
}

tl::expected<AgentRequest, std::string> parse_agent_request(std::string_view text) {
  json request_json;
  try {
    request_json = json::parse(text);
  } catch (const json::exception& ex) {
    return tl::unexpected(std::string("invalid JSON: ") + ex.what());
  }

  if (!request_json.is_object()) {
    return tl::unexpected("request must be a JSON object");
  }

  auto id = get_required_string(request_json, "id");
  if (!id.has_value()) {
    return tl::unexpected(id.error());
  }

  auto command = get_required_string(request_json, "command");
  if (!command.has_value()) {
    return tl::unexpected(command.error());
  }

  AgentRequest request;
  request.id = *id;

  if (*command == "list_windows") {
    auto managed_only = get_optional_bool(request_json, "managed_only", false);
    if (!managed_only.has_value()) {
      return tl::unexpected(managed_only.error());
    }
    request.payload = AgentListWindowsRequest{*managed_only};
    return request;
  }

  if (*command == "get_state") {
    auto include_layout = get_optional_bool(request_json, "include_layout", true);
    if (!include_layout.has_value()) {
      return tl::unexpected(include_layout.error());
    }
    request.payload = AgentGetStateRequest{*include_layout};
    return request;
  }

  if (*command == "focus_window") {
    auto leaf_id = get_required_window_id(request_json, "window_id");
    if (!leaf_id.has_value()) {
      return tl::unexpected(leaf_id.error());
    }
    auto select = get_optional_bool(request_json, "select", true);
    if (!select.has_value()) {
      return tl::unexpected(select.error());
    }
    request.payload = AgentFocusWindowRequest{*leaf_id, *select};
    return request;
  }

  if (*command == "send_action") {
    auto action_text = get_required_string(request_json, "action");
    if (!action_text.has_value()) {
      return tl::unexpected(action_text.error());
    }
    auto action = magic_enum::enum_cast<HotkeyAction>(*action_text);
    if (!action.has_value()) {
      return tl::unexpected("action must be a valid HotkeyAction");
    }
    request.payload = AgentSendActionRequest{*action};
    return request;
  }

  if (*command == "swap_windows") {
    auto first_leaf_id = get_required_window_id(request_json, "first_window_id");
    if (!first_leaf_id.has_value()) {
      return tl::unexpected(first_leaf_id.error());
    }
    auto second_leaf_id = get_required_window_id(request_json, "second_window_id");
    if (!second_leaf_id.has_value()) {
      return tl::unexpected(second_leaf_id.error());
    }
    request.payload = AgentSwapWindowsRequest{*first_leaf_id, *second_leaf_id};
    return request;
  }

  if (*command == "move_window_to_monitor") {
    auto leaf_id = get_required_window_id(request_json, "window_id");
    if (!leaf_id.has_value()) {
      return tl::unexpected(leaf_id.error());
    }
    auto target_monitor_index = get_required_int(request_json, "target_monitor_index");
    if (!target_monitor_index.has_value()) {
      return tl::unexpected(target_monitor_index.error());
    }
    auto anchor_leaf_id = get_optional_window_id(request_json, "anchor_window_id");
    if (!anchor_leaf_id.has_value()) {
      return tl::unexpected(anchor_leaf_id.error());
    }
    request.payload =
        AgentMoveWindowToMonitorRequest{*leaf_id, *target_monitor_index, *anchor_leaf_id};
    return request;
  }

  if (*command == "retile") {
    request.payload = AgentRetileRequest{};
    return request;
  }

  return tl::unexpected("unknown command: " + *command);
}

std::string serialize_agent_response(const AgentResponse& response) {
  json result = {{"id", response.id}, {"ok", response.ok}};

  if (response.error.has_value()) {
    result["error"] = *response.error;
  }

  if (response.payload.has_value()) {
    result["result"] = std::visit(
        overloaded{[](const AgentStateResponse& state) { return to_json_value(state); },
                   [](const AgentMutationResponse& mutation) { return to_json_value(mutation); }},
        *response.payload);
  }

  return result.dump();
}

} // namespace wintiler
