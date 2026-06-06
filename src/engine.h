#pragma once

#include <optional>
#include <string>
#include <vector>

#include "binary_tree.h"
#include "model.h"
#include "options.h"

namespace wintiler::ctrl {

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

struct Point {
  long x = 0;
  long y = 0;
};

enum class SplitDir { Vertical, Horizontal };

enum class SplitMode {
  Dwindle,
  Vertical,
  Horizontal,
};

enum class Direction { Left, Right, Up, Down };

struct CellData {
  SplitDir split_dir = SplitDir::Vertical;
  float split_ratio = 0.5f;
  std::optional<size_t> leaf_id;
};

struct Cluster {
  BinaryTree<CellData> tree;
  float window_width = 0.0f;
  float window_height = 0.0f;
  std::optional<int> zen_cell_index;
  bool has_fullscreen_cell = false;
  float global_x = 0.0f;
  float global_y = 0.0f;
  float monitor_x = 0.0f;
  float monitor_y = 0.0f;
  float monitor_width = 0.0f;
  float monitor_height = 0.0f;
  float split_width_multiplier = kDefaultSplitWidthMultiplier;
};

struct CellIndicatorByIndex {
  int cluster_index = 0;
  int cell_index = 0;
};

struct System {
  std::vector<Cluster> clusters;
  std::optional<CellIndicatorByIndex> selection;
  SplitMode split_mode = SplitMode::Dwindle;
};

struct ClusterInitInfo {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float monitor_x = 0.0f;
  float monitor_y = 0.0f;
  float monitor_width = 0.0f;
  float monitor_height = 0.0f;
  std::vector<size_t> initial_cell_ids;
  std::optional<LayoutRule> initial_layout_rule;
  float split_width_multiplier = kDefaultSplitWidthMultiplier;
};

struct ClusterCellUpdateInfo {
  std::vector<size_t> leaf_ids;
  bool has_fullscreen_cell = false;
};

struct DropMoveResult {
  Point cursor_pos;
  bool was_exchange = false;
};

} // namespace wintiler::ctrl

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
  bool dump_window_management = false;
  bool restart_system = false;
  bool toggle_floating = false;
  bool toggle_verbose_logging = false;
  std::optional<size_t> focus_leaf_id;
  std::optional<size_t> floating_leaf_id;
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
  bool is_minimized = false;
  std::optional<ctrl::Rect> actual_rect;
  int min_track_width = 0;
  int min_track_height = 0;
};

struct PlacementCorrectionTarget {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct PlacementCorrectionFailure {
  size_t leaf_id = 0;
  PlacementCorrectionTarget target;
  int attempts = 0;
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
  bool reapply_layout_templates = false;
  const LayoutOptions* layout_options = nullptr;
  std::vector<ClusterTilingOptions> cluster_options;
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
  bool dump_window_management = false;
  bool restart_system = false;
  bool toggle_floating = false;
  bool toggle_verbose_logging = false;
  bool clear_drag_ended = false;
  bool has_completed_initial_tile_pass = false;
  std::optional<size_t> focus_leaf_id;
  std::optional<size_t> floating_leaf_id;
  std::optional<ctrl::Point> cursor_pos;
  std::optional<std::string> toast_message;
  std::vector<size_t> placement_correction_leaf_ids;
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
  std::vector<std::optional<size_t>> previous_maximized_leaf_ids;
  std::vector<PlacementCorrectionFailure> placement_correction_failures;

  // Initialize engine from cluster init info
  void init(const std::vector<ctrl::ClusterInitInfo>& infos,
            ctrl::SplitMode split_mode = ctrl::SplitMode::Dwindle);

  // Compute geometry for all clusters (call once per frame)
  [[nodiscard]] std::vector<std::vector<ctrl::Rect>> compute_geometries(float gap_h, float gap_v,
                                                                        float zen_pct) const;
  [[nodiscard]] std::vector<std::vector<ctrl::Rect>>
  compute_geometries(const std::vector<ClusterTilingOptions>& cluster_options) const;

  // Get hover info from global mouse position (does not modify state)
  [[nodiscard]] HoverInfo
  get_hover_info(float global_x, float global_y,
                 const std::vector<std::vector<ctrl::Rect>>& global_geometries) const;

  // Synchronize external window state into the engine
  [[nodiscard]] UpdateResult update(const std::vector<ctrl::ClusterCellUpdateInfo>& cluster_updates,
                                    std::optional<int> redirect_cluster_index = std::nullopt,
                                    const LayoutOptions* layout_options = nullptr,
                                    bool reapply_layout_templates = false);
  [[nodiscard]] UpdateResult update(const std::vector<ctrl::ClusterCellUpdateInfo>& cluster_updates,
                                    std::optional<int> redirect_cluster_index,
                                    const std::vector<ClusterTilingOptions>& cluster_options,
                                    bool reapply_layout_templates = false);

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
  [[nodiscard]] ActionResult
  process_action(HotkeyAction action, const std::vector<std::vector<ctrl::Rect>>& global_geometries,
                 const std::vector<ClusterTilingOptions>& cluster_options);

  // Find a managed leaf by its window-backed leaf ID
  [[nodiscard]] std::optional<ctrl::CellIndicatorByIndex> find_leaf(size_t leaf_id) const;

  // Get the currently selected leaf ID, if selection exists
  [[nodiscard]] std::optional<size_t> selected_leaf_id() const;

  // Select a managed leaf by ID
  [[nodiscard]] bool select_leaf(size_t leaf_id);

  // Swap two managed leaves by ID
  [[nodiscard]] bool swap_leaves(size_t first_leaf_id, size_t second_leaf_id);

  // Move a managed leaf to a target leaf cell
  [[nodiscard]] bool move_leaf_to_cell(size_t source_leaf_id, int target_cluster_index,
                                       int target_cell_index);

  // Clear the stored cell reference
  void clear_stored_cell();
};

} // namespace wintiler
