#include "agent_mode.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <iostream>
#include <magic_enum/magic_enum.hpp>

#include "agent_protocol.h"
#include "multi_engine.h"
#include "runtime_support.h"
#include "winapi.h"

namespace wintiler {

namespace {

constexpr char kFallbackDesktopId[] = "__agent_default__";

struct AgentDesktopData {
  std::vector<winapi::MonitorInfo> last_known_monitors;
};

struct AgentRuntimeState {
  MultiEngine<AgentDesktopData, std::string> multi_engine;
};

struct AgentSyncSnapshot {
  winapi::LoopInputState input_state;
  std::string desktop_id;
  std::vector<ctrl::ClusterCellUpdateInfo> cluster_updates;
  UpdateResult update_result;
  Engine* engine = nullptr;
};

std::string resolve_desktop_id(const winapi::LoopInputState& input_state) {
  if (input_state.desktop_id.has_value()) {
    return *input_state.desktop_id;
  }
  return kFallbackDesktopId;
}

AgentResponse make_error_response(std::string id, std::string error) {
  AgentResponse response;
  response.id = std::move(id);
  response.ok = false;
  response.error = std::move(error);
  return response;
}

AgentRect make_agent_rect(const winapi::WindowPosition& rect) {
  return AgentRect{rect.x, rect.y, rect.width, rect.height};
}

AgentRect make_agent_rect(const ctrl::Rect& rect) {
  return AgentRect{static_cast<int>(rect.x), static_cast<int>(rect.y), static_cast<int>(rect.width),
                   static_cast<int>(rect.height)};
}

void retile_engine(const Engine& engine, const GlobalOptions& options) {
  auto geometries =
      engine.compute_geometries(options.gapOptions.horizontal, options.gapOptions.vertical,
                                options.visualizationOptions.renderOptions.zen_percentage);
  apply_tile_positions(engine.system, geometries);
}

tl::expected<void, std::string> focus_leaf_id(size_t leaf_id) {
  winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(leaf_id);
  if (!winapi::set_foreground_window(hwnd)) {
    return tl::unexpected("failed to set foreground window");
  }
  return {};
}

std::optional<int> find_first_leaf_cell_index(const ctrl::Cluster& cluster) {
  for (int cell_index = 0; cell_index < cluster.tree.size(); ++cell_index) {
    if (cluster.tree.is_leaf(cell_index) && cluster.tree[cell_index].leaf_id.has_value()) {
      return cell_index;
    }
  }
  return std::nullopt;
}

tl::expected<std::optional<int>, std::string>
resolve_target_cell_index(const Engine& engine, int target_cluster_index,
                          std::optional<size_t> anchor_leaf_id) {
  if (target_cluster_index < 0 ||
      static_cast<size_t>(target_cluster_index) >= engine.system.clusters.size()) {
    return tl::unexpected("target_monitor_index is out of range");
  }

  if (anchor_leaf_id.has_value()) {
    auto anchor_cell = engine.find_leaf(*anchor_leaf_id);
    if (!anchor_cell.has_value()) {
      return tl::unexpected("anchor_window_id was not found");
    }
    if (anchor_cell->cluster_index != target_cluster_index) {
      return tl::unexpected("anchor_window_id is not on the requested target monitor");
    }
    return anchor_cell->cell_index;
  }

  return find_first_leaf_cell_index(
      engine.system.clusters[static_cast<size_t>(target_cluster_index)]);
}

std::vector<ctrl::ClusterCellUpdateInfo>
build_move_cluster_updates(const std::vector<ctrl::ClusterCellUpdateInfo>& current_updates,
                           size_t leaf_id, int target_cluster_index) {
  std::vector<ctrl::ClusterCellUpdateInfo> moved_updates = current_updates;
  for (auto& cluster_update : moved_updates) {
    auto& leaf_ids = cluster_update.leaf_ids;
    leaf_ids.erase(std::remove(leaf_ids.begin(), leaf_ids.end(), leaf_id), leaf_ids.end());
  }

  moved_updates[static_cast<size_t>(target_cluster_index)].leaf_ids.push_back(leaf_id);
  return moved_updates;
}

AgentResponse make_success_response(std::string id, const AgentMutationResponse& mutation) {
  AgentResponse response;
  response.id = std::move(id);
  response.ok = true;
  response.payload = mutation;
  return response;
}

tl::expected<AgentMutationResponse, std::string>
apply_action_result(Engine& engine, const ActionResult& action_result,
                    const GlobalOptions& options) {
  if (!action_result.success) {
    return tl::unexpected("agent action failed");
  }

  if (action_result.control != LoopControl::Continue) {
    return tl::unexpected("agent mode does not support control-flow actions");
  }

  AgentMutationResponse mutation;
  mutation.selection_changed = action_result.selection_changed;
  mutation.layout_changed = action_result.layout_changed;
  mutation.toast_message = action_result.toast_message;

  if (action_result.cursor_pos.has_value()) {
    if (!winapi::set_cursor_pos(action_result.cursor_pos->x, action_result.cursor_pos->y)) {
      spdlog::error("Failed to set cursor position");
    }
  }

  if (action_result.focus_leaf_id.has_value()) {
    auto focus_result = focus_leaf_id(*action_result.focus_leaf_id);
    if (!focus_result.has_value()) {
      return tl::unexpected(focus_result.error());
    }
    mutation.focused_window_id = format_window_id(*action_result.focus_leaf_id);
  }

  if (action_result.apply_tiles) {
    retile_engine(engine, options);
  }

  return mutation;
}

AgentStateResponse build_state_response(const AgentSyncSnapshot& snapshot,
                                        const GlobalOptions& options, bool include_layout,
                                        bool managed_only) {
  AgentStateResponse response;
  response.desktop_id = snapshot.input_state.desktop_id;

  const auto split_mode_name = magic_enum::enum_name(snapshot.engine->system.split_mode);
  response.split_mode = std::string(split_mode_name.data(), split_mode_name.size());

  std::vector<std::vector<ctrl::Rect>> geometries;
  if (include_layout) {
    geometries = snapshot.engine->compute_geometries(
        options.gapOptions.horizontal, options.gapOptions.vertical,
        options.visualizationOptions.renderOptions.zen_percentage);
  }

  for (size_t monitor_index = 0; monitor_index < snapshot.input_state.windows_per_monitor.size();
       ++monitor_index) {
    for (const auto& managed_window : snapshot.input_state.windows_per_monitor[monitor_index]) {
      size_t leaf_id = reinterpret_cast<size_t>(managed_window.handle);
      AgentWindowSnapshot window_snapshot;
      window_snapshot.window_id = format_window_id(leaf_id);
      window_snapshot.monitor_index = static_cast<int>(monitor_index);
      window_snapshot.is_foreground =
          snapshot.input_state.foreground_window == managed_window.handle;
      window_snapshot.is_maximized = managed_window.is_maximized;
      window_snapshot.is_fullscreen = managed_window.is_fullscreen;

      auto info = winapi::get_window_info(managed_window.handle);
      window_snapshot.pid = info.pid;
      window_snapshot.process_name = std::move(info.processName);
      window_snapshot.title = std::move(info.title);
      window_snapshot.class_name = std::move(info.className);

      auto actual_rect = winapi::get_window_rect(managed_window.handle);
      if (actual_rect.has_value()) {
        window_snapshot.rect = make_agent_rect(*actual_rect);
        window_snapshot.actual_rect = window_snapshot.rect;
      }

      auto cell = snapshot.engine->find_leaf(leaf_id);
      if (cell.has_value()) {
        window_snapshot.is_managed = true;
        if (!managed_only || window_snapshot.is_managed) {
          if (include_layout) {
            window_snapshot.cluster_index = cell->cluster_index;
            window_snapshot.cell_index = cell->cell_index;
            if (cell->cluster_index >= 0 &&
                static_cast<size_t>(cell->cluster_index) < geometries.size() &&
                cell->cell_index >= 0 &&
                static_cast<size_t>(cell->cell_index) <
                    geometries[static_cast<size_t>(cell->cluster_index)].size()) {
              window_snapshot.layout_rect =
                  make_agent_rect(geometries[static_cast<size_t>(cell->cluster_index)]
                                            [static_cast<size_t>(cell->cell_index)]);
            }
          }
        }
      }

      if (managed_only && !window_snapshot.is_managed) {
        continue;
      }

      if (!actual_rect.has_value() && window_snapshot.layout_rect.has_value()) {
        window_snapshot.rect = *window_snapshot.layout_rect;
      }

      response.windows.push_back(std::move(window_snapshot));
    }
  }

  if (include_layout) {
    auto selected_leaf_id = snapshot.engine->selected_leaf_id();
    if (selected_leaf_id.has_value() && snapshot.engine->system.selection.has_value()) {
      response.selection = AgentSelectionSnapshot{format_window_id(*selected_leaf_id),
                                                  snapshot.engine->system.selection->cluster_index,
                                                  snapshot.engine->system.selection->cell_index};
    }
  }

  return response;
}

tl::expected<AgentSyncSnapshot, std::string>
sync_runtime_state(AgentRuntimeState& runtime, GlobalOptionsProvider& options_provider) {
  if (options_provider.refresh()) {
    spdlog::info("Agent mode config hot-reloaded");
  }

  AgentSyncSnapshot snapshot{
      winapi::gather_loop_input_state(options_provider.options.ignoreOptions), "", {}, {}, nullptr};
  snapshot.desktop_id = resolve_desktop_id(snapshot.input_state);

  if (!runtime.multi_engine.has_desktop(snapshot.desktop_id)) {
    auto cluster_infos =
        create_cluster_infos_from_monitors(snapshot.input_state.monitors, options_provider.options);
    auto desktop = runtime.multi_engine.create_desktop(snapshot.desktop_id, cluster_infos);
    if (!desktop.has_value()) {
      return tl::unexpected("failed to create agent desktop state");
    }
    desktop->get().data.last_known_monitors = snapshot.input_state.monitors;
  }

  if (!runtime.multi_engine.has_current() ||
      *runtime.multi_engine.current_id != snapshot.desktop_id) {
    if (!runtime.multi_engine.switch_to(snapshot.desktop_id)) {
      return tl::unexpected("failed to switch to agent desktop state");
    }
  }

  auto& current_desktop = runtime.multi_engine.current();
  if (!winapi::monitors_equal(current_desktop.data.last_known_monitors,
                              snapshot.input_state.monitors)) {
    initialize_engine_from_monitors(current_desktop.engine, snapshot.input_state.monitors,
                                    options_provider.options);
    current_desktop.engine.clear_stored_cell();
    current_desktop.data.last_known_monitors = snapshot.input_state.monitors;
  }

  snapshot.cluster_updates = extract_cluster_updates_from_input(snapshot.input_state);
  snapshot.update_result = current_desktop.engine.update(snapshot.cluster_updates);
  snapshot.engine = &current_desktop.engine;
  return snapshot;
}

AgentResponse execute_agent_request(AgentRuntimeState& runtime,
                                    GlobalOptionsProvider& options_provider,
                                    const AgentRequest& request) {
  auto sync_snapshot = sync_runtime_state(runtime, options_provider);
  if (!sync_snapshot.has_value()) {
    return make_error_response(request.id, sync_snapshot.error());
  }

  if (std::holds_alternative<AgentListWindowsRequest>(request.payload)) {
    const auto& list_windows = std::get<AgentListWindowsRequest>(request.payload);
    AgentResponse response;
    response.id = request.id;
    response.ok = true;
    response.payload = build_state_response(*sync_snapshot, options_provider.options, true,
                                            list_windows.managed_only);
    return response;
  }

  if (std::holds_alternative<AgentGetStateRequest>(request.payload)) {
    const auto& get_state = std::get<AgentGetStateRequest>(request.payload);
    AgentResponse response;
    response.id = request.id;
    response.ok = true;
    response.payload = build_state_response(*sync_snapshot, options_provider.options,
                                            get_state.include_layout, false);
    return response;
  }

  if (std::holds_alternative<AgentFocusWindowRequest>(request.payload)) {
    const auto& focus_window = std::get<AgentFocusWindowRequest>(request.payload);
    auto previous_selection = sync_snapshot->engine->selected_leaf_id();

    if (focus_window.select && !sync_snapshot->engine->select_leaf(focus_window.leaf_id)) {
      return make_error_response(request.id, "window_id was not found in the managed layout");
    }

    auto focus_result = focus_leaf_id(focus_window.leaf_id);
    if (!focus_result.has_value()) {
      return make_error_response(request.id, focus_result.error());
    }

    AgentMutationResponse mutation;
    mutation.selection_changed = previous_selection != sync_snapshot->engine->selected_leaf_id();
    mutation.focused_window_id = format_window_id(focus_window.leaf_id);
    return make_success_response(request.id, mutation);
  }

  if (std::holds_alternative<AgentSendActionRequest>(request.payload)) {
    const auto& send_action = std::get<AgentSendActionRequest>(request.payload);
    auto geometries = sync_snapshot->engine->compute_geometries(
        options_provider.options.gapOptions.horizontal,
        options_provider.options.gapOptions.vertical,
        options_provider.options.visualizationOptions.renderOptions.zen_percentage);
    auto action_result = sync_snapshot->engine->process_action(
        send_action.action, geometries, options_provider.options.gapOptions.horizontal,
        options_provider.options.gapOptions.vertical,
        options_provider.options.visualizationOptions.renderOptions.zen_percentage);
    auto mutation =
        apply_action_result(*sync_snapshot->engine, action_result, options_provider.options);
    if (!mutation.has_value()) {
      return make_error_response(request.id, mutation.error());
    }
    return make_success_response(request.id, *mutation);
  }

  if (std::holds_alternative<AgentSwapWindowsRequest>(request.payload)) {
    const auto& swap_windows = std::get<AgentSwapWindowsRequest>(request.payload);
    auto previous_selection = sync_snapshot->engine->selected_leaf_id();
    if (!sync_snapshot->engine->swap_leaves(swap_windows.first_leaf_id,
                                            swap_windows.second_leaf_id)) {
      return make_error_response(request.id, "failed to swap windows");
    }

    retile_engine(*sync_snapshot->engine, options_provider.options);

    AgentMutationResponse mutation;
    mutation.selection_changed = previous_selection != sync_snapshot->engine->selected_leaf_id();
    mutation.layout_changed = true;
    return make_success_response(request.id, mutation);
  }

  if (std::holds_alternative<AgentMoveWindowToMonitorRequest>(request.payload)) {
    const auto& move_window = std::get<AgentMoveWindowToMonitorRequest>(request.payload);
    auto target_cell_index = resolve_target_cell_index(
        *sync_snapshot->engine, move_window.target_monitor_index, move_window.anchor_leaf_id);
    if (!target_cell_index.has_value()) {
      return make_error_response(request.id, target_cell_index.error());
    }

    auto previous_selection = sync_snapshot->engine->selected_leaf_id();
    if (target_cell_index->has_value()) {
      if (!sync_snapshot->engine->move_leaf_to_cell(
              move_window.leaf_id, move_window.target_monitor_index, **target_cell_index)) {
        return make_error_response(request.id, "failed to move window to target monitor");
      }
    } else {
      auto source_cell = sync_snapshot->engine->find_leaf(move_window.leaf_id);
      if (!source_cell.has_value()) {
        return make_error_response(request.id, "window_id was not found in the managed layout");
      }

      auto moved_updates = build_move_cluster_updates(
          sync_snapshot->cluster_updates, move_window.leaf_id, move_window.target_monitor_index);
      UpdateResult move_result = sync_snapshot->engine->update(moved_updates);
      if (!move_result.topology_changed) {
        return make_error_response(request.id, "failed to move window to target monitor");
      }
    }

    retile_engine(*sync_snapshot->engine, options_provider.options);

    AgentMutationResponse mutation;
    mutation.selection_changed = previous_selection != sync_snapshot->engine->selected_leaf_id();
    mutation.layout_changed = true;
    return make_success_response(request.id, mutation);
  }

  if (std::holds_alternative<AgentRetileRequest>(request.payload)) {
    retile_engine(*sync_snapshot->engine, options_provider.options);

    AgentMutationResponse mutation;
    mutation.layout_changed = true;
    return make_success_response(request.id, mutation);
  }

  return make_error_response(request.id, "agent command is not implemented yet");
}

void run_agent_stdio_mode(GlobalOptionsProvider& optionsProvider) {
  AgentRuntimeState runtime;
  std::string line;

  winapi::register_virtual_desktop_notifications();

  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      continue;
    }

    auto request = parse_agent_request(line);
    AgentResponse response;
    if (!request.has_value()) {
      response = make_error_response("", request.error());
    } else {
      response = execute_agent_request(runtime, optionsProvider, *request);
    }

    std::cout << serialize_agent_response(response) << '\n' << std::flush;
    if (!std::cout.good()) {
      spdlog::error("Failed to write agent response to stdout");
      break;
    }
  }

  winapi::unregister_virtual_desktop_notifications();
}

} // namespace

void run_agent_mode(GlobalOptionsProvider& optionsProvider, const AgentCommand& command) {
  if (!optionsProvider.configPath.has_value()) {
    spdlog::debug("Agent mode started without an explicit config path");
  }

  switch (command.transport) {
  case AgentTransport::Stdio:
    run_agent_stdio_mode(optionsProvider);
    break;
  }
}

} // namespace wintiler
