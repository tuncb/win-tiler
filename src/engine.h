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

  // Process a hotkey action
  [[nodiscard]] ActionResult
  process_action(HotkeyAction action, const std::vector<std::vector<ctrl::Rect>>& global_geometries,
                 float gap_h, float gap_v, float zen_pct);

  // Store the currently selected cell for swap/move operations
  void store_selected_cell();

  // Clear the stored cell reference
  void clear_stored_cell();

  // Get the sibling index of the currently selected cell (if any)
  [[nodiscard]] std::optional<int> get_selected_sibling_index() const;

  // Get the sibling leaf_id of the currently selected cell (if any)
  [[nodiscard]] std::optional<size_t> get_selected_sibling_leaf_id() const;

  // Perform a drag-drop move or exchange operation
  [[nodiscard]] std::optional<ctrl::DropMoveResult>
  perform_drop_move(size_t source_leaf_id, float cursor_x, float cursor_y,
                    const std::vector<std::vector<ctrl::Rect>>& geometries, bool do_exchange);

  // Handle window resize to update split ratio
  [[nodiscard]] bool handle_resize(int cluster_index, size_t leaf_id, const ctrl::Rect& actual_rect,
                                   const std::vector<ctrl::Rect>& cluster_geometry);

  // Get center of currently selected cell from geometries
  [[nodiscard]] std::optional<ctrl::Point>
  get_selected_center(const std::vector<std::vector<ctrl::Rect>>& geometries) const;
};

} // namespace wintiler
