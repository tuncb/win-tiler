#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include "agent_protocol.h"

using namespace wintiler;

TEST_SUITE("agent_protocol") {
  TEST_CASE("window ids round trip through string format") {
    constexpr size_t leaf_id = static_cast<size_t>(0x12AB34CD);

    std::string window_id = format_window_id(leaf_id);
    auto parsed = parse_window_id(window_id);

    REQUIRE(parsed.has_value());
    CHECK(*parsed == leaf_id);
  }

  TEST_CASE("window id parsing rejects invalid prefix") {
    auto parsed = parse_window_id("window:1234");

    CHECK_FALSE(parsed.has_value());
    CHECK(parsed.error() == "window_id must start with 'hwnd:'");
  }

  TEST_CASE("parses list windows request with default managed_only flag") {
    auto parsed = parse_agent_request(R"({"id":"1","command":"list_windows"})");

    REQUIRE(parsed.has_value());
    CHECK(parsed->id == "1");
    REQUIRE(std::holds_alternative<AgentListWindowsRequest>(parsed->payload));
    CHECK_FALSE(std::get<AgentListWindowsRequest>(parsed->payload).managed_only);
  }

  TEST_CASE("parses focus window request") {
    std::string request_json =
        std::string(R"({"id":"focus-1","command":"focus_window","window_id":")") +
        format_window_id(static_cast<size_t>(0xABCD)) + R"(","select":false})";

    auto parsed = parse_agent_request(request_json);

    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<AgentFocusWindowRequest>(parsed->payload));
    const auto& request = std::get<AgentFocusWindowRequest>(parsed->payload);
    CHECK(request.leaf_id == static_cast<size_t>(0xABCD));
    CHECK_FALSE(request.select);
  }

  TEST_CASE("parses send action request") {
    auto parsed = parse_agent_request(R"({"id":"2","command":"send_action","action":"ToggleZen"})");

    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<AgentSendActionRequest>(parsed->payload));
    CHECK(std::get<AgentSendActionRequest>(parsed->payload).action == HotkeyAction::ToggleZen);
  }

  TEST_CASE("parses move window to monitor request with anchor") {
    std::string request_json =
        std::string(R"({"id":"3","command":"move_window_to_monitor","window_id":")") +
        format_window_id(static_cast<size_t>(0x1111)) +
        R"(","target_monitor_index":1,"anchor_window_id":")" +
        format_window_id(static_cast<size_t>(0x2222)) + R"("})";

    auto parsed = parse_agent_request(request_json);

    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<AgentMoveWindowToMonitorRequest>(parsed->payload));
    const auto& request = std::get<AgentMoveWindowToMonitorRequest>(parsed->payload);
    CHECK(request.leaf_id == static_cast<size_t>(0x1111));
    CHECK(request.target_monitor_index == 1);
    REQUIRE(request.anchor_leaf_id.has_value());
    CHECK(*request.anchor_leaf_id == static_cast<size_t>(0x2222));
  }

  TEST_CASE("rejects unknown command") {
    auto parsed = parse_agent_request(R"({"id":"4","command":"unknown"})");

    CHECK_FALSE(parsed.has_value());
    CHECK(parsed.error() == "unknown command: unknown");
  }

  TEST_CASE("parses retile request") {
    auto parsed = parse_agent_request(R"({"id":"retile-1","command":"retile"})");

    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<AgentRetileRequest>(parsed->payload));
  }

  TEST_CASE("rejects invalid send action values") {
    auto parsed =
        parse_agent_request(R"({"id":"bad-action","command":"send_action","action":"Nope"})");

    CHECK_FALSE(parsed.has_value());
    CHECK(parsed.error() == "action must be a valid HotkeyAction");
  }

  TEST_CASE("rejects non integer target monitor index") {
    std::string request_json =
        std::string(R"({"id":"bad-move","command":"move_window_to_monitor","window_id":")") +
        format_window_id(static_cast<size_t>(0x1111)) + R"(","target_monitor_index":"1"})";

    auto parsed = parse_agent_request(request_json);

    CHECK_FALSE(parsed.has_value());
    CHECK(parsed.error() == "target_monitor_index must be an integer");
  }

  TEST_CASE("rejects malformed anchor window ids") {
    std::string request_json =
        std::string(R"({"id":"bad-anchor","command":"move_window_to_monitor","window_id":")") +
        format_window_id(static_cast<size_t>(0x1111)) +
        R"(","target_monitor_index":1,"anchor_window_id":"bad"})";

    auto parsed = parse_agent_request(request_json);

    CHECK_FALSE(parsed.has_value());
    CHECK(parsed.error() == "window_id must start with 'hwnd:'");
  }

  TEST_CASE("serializes state response") {
    AgentStateResponse state;
    state.desktop_id = "{desktop}";
    state.split_mode = "Zigzag";
    state.windows.push_back(AgentWindowSnapshot{
        .window_id = format_window_id(static_cast<size_t>(0x9999)),
        .pid = 42,
        .process_name = "Code.exe",
        .title = "main.cpp",
        .class_name = "Chrome_WidgetWin_1",
        .monitor_index = 0,
        .cluster_index = 1,
        .cell_index = 2,
        .rect = AgentRect{10, 20, 300, 400},
        .is_managed = true,
        .is_foreground = false,
        .is_maximized = false,
        .is_fullscreen = false,
    });
    state.selection = AgentSelectionSnapshot{state.windows[0].window_id, 1, 2};

    AgentResponse response;
    response.id = "5";
    response.ok = true;
    response.payload = state;

    auto json = serialize_agent_response(response);

    CHECK(json.find("\"id\":\"5\"") != std::string::npos);
    CHECK(json.find("\"split_mode\":\"Zigzag\"") != std::string::npos);
    CHECK(json.find("\"window_id\":\"hwnd:") != std::string::npos);
    CHECK(json.find("\"monitor_index\":0") != std::string::npos);
  }

  TEST_CASE("serializes mutation response") {
    AgentMutationResponse mutation;
    mutation.selection_changed = true;
    mutation.layout_changed = true;
    mutation.focused_window_id = format_window_id(static_cast<size_t>(0x7777));
    mutation.toast_message = "Split mode: Vertical";

    AgentResponse response;
    response.id = "mutation-1";
    response.ok = true;
    response.payload = mutation;

    auto json = serialize_agent_response(response);

    CHECK(json.find("\"selection_changed\":true") != std::string::npos);
    CHECK(json.find("\"layout_changed\":true") != std::string::npos);
    CHECK(json.find("\"toast_message\":\"Split mode: Vertical\"") != std::string::npos);
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
