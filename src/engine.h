#pragma once

#include <optional>
#include <string>
#include <vector>

#include "controller.h"
#include "model.h"
#include "options.h"

namespace wintiler {

enum class LoopControl {
  Continue,
  Exit,
  EnterManualPause,
};

// Result of processing an action
struct ActionResult {
  bool success = false;
  bool layout_changed = false;
  bool selection_changed = false;
  bool apply_tiles = false;
  std::optional<size_t> focus_leaf_id;
  std::optional<ctrl::Point> cursor_pos;
  std::optional<std::string> toast_message;
  LoopControl control = LoopControl::Continue;
};

// Result of synchronizing external window state into the engine
struct UpdateResult {
  bool topology_changed = false;
  bool selection_changed = false;
  bool layout_changed = false;
  bool apply_tiles = false;
  std::optional<ctrl::Point> cursor_pos;
};

struct HoverSelectionResult {
  bool selection_changed = false;
};

struct ManagedWindowState {
  size_t leaf_id = 0;
  bool is_fullscreen = false;
  bool is_maximized = false;
};

struct CompletedDragRequest {
  size_t leaf_id = 0;
  std::optional<ctrl::Point> cursor_pos;
  std::optional<ctrl::Rect> actual_window_rect;
  bool do_exchange = false;
};

struct EngineFrameInput {
  std::vector<ctrl::ClusterCellUpdateInfo> cluster_updates;
  std::vector<std::vector<ManagedWindowState>> managed_windows;
  std::optional<HotkeyAction> hotkey_action;
  std::optional<ctrl::Point> cursor_pos;
  std::optional<CompletedDragRequest> completed_drag;
  bool auto_zen_on_maximize = false;
  bool update_hover_selection = true;
  bool has_completed_initial_tile_pass = false;
  float gap_h = 0.0f;
  float gap_v = 0.0f;
  float zen_pct = 0.0f;
};

struct EngineFrameOutput {
  LoopControl control = LoopControl::Continue;
  bool topology_changed = false;
  bool selection_changed = false;
  bool layout_changed = false;
  bool apply_tiles = false;
  bool clear_drag_ended = false;
  bool has_completed_initial_tile_pass = false;
  std::optional<size_t> focus_leaf_id;
  std::optional<ctrl::Point> cursor_pos;
  std::optional<std::string> toast_message;
  std::vector<std::vector<ctrl::Rect>> geometries;
};

// Information about what the mouse is hovering over
struct HoverInfo {
  std::optional<size_t> cluster_index;            // Which cluster mouse is over (even if empty)
  std::optional<ctrl::CellIndicatorByIndex> cell; // Specific cell if over a leaf
};

// Engine manages application state and processes actions
// All members are public for easy access
struct Engine {
  ctrl::System system;
  std::optional<StoredCell> stored_cell;

  // Initialize engine from cluster init info
  void init(const std::vector<ctrl::ClusterInitInfo>& infos);

  // Compute geometry for all clusters (call once per frame)
  [[nodiscard]] std::vector<std::vector<ctrl::Rect>> compute_geometries(float gap_h, float gap_v,
                                                                        float zen_pct) const;

  // Get hover info from global mouse position (does not modify state)
  [[nodiscard]] HoverInfo
  get_hover_info(float global_x, float global_y,
                 const std::vector<std::vector<ctrl::Rect>>& global_geometries) const;

  // Update system state - wraps ctrl::update()
  [[nodiscard]] UpdateResult update(const std::vector<ctrl::ClusterCellUpdateInfo>& cluster_updates,
                                    std::optional<int> redirect_cluster_index = std::nullopt);

  // Update selection based on hover location
  [[nodiscard]] HoverSelectionResult
  update_selection_from_hover(float global_x, float global_y,
                              const std::vector<std::vector<ctrl::Rect>>& global_geometries);

  // Process a full frame of input and return explicit output for the loop to apply
  [[nodiscard]] EngineFrameOutput process_frame(const EngineFrameInput& input);

  // Process a hotkey action
  [[nodiscard]] ActionResult
  process_action(HotkeyAction action, const std::vector<std::vector<ctrl::Rect>>& global_geometries,
                 float gap_h, float gap_v, float zen_pct);

  // Clear the stored cell reference
  void clear_stored_cell();
};

} // namespace wintiler
