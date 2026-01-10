#pragma once

#include <optional>
#include <vector>

#include "controller.h"
#include "model.h"
#include "options.h"

namespace wintiler {

// Result of processing an action
struct ActionResult {
  bool success = false;
  bool selection_changed = false;
  std::optional<ctrl::Rect> new_selection_rect;
};

// Cached geometry for all clusters
struct GeometryCache {
  std::vector<std::vector<ctrl::Rect>> local;
  std::vector<std::vector<ctrl::Rect>> global;
};

// Engine manages application state and processes actions
// All members are public for easy access
struct Engine {
  ctrl::System system;
  std::vector<std::vector<size_t>> leaf_ids_per_cluster;
  std::optional<size_t> hovered_cluster_index;
  std::optional<StoredCell> stored_cell;
  size_t next_process_id = 10;

  // Initialize engine from cluster init info
  void init(const std::vector<ctrl::ClusterInitInfo>& infos);

  // Compute geometry for all clusters (call once per frame)
  [[nodiscard]] GeometryCache compute_geometries(float gap_h, float gap_v, float zen_pct) const;

  // Update hover state and selection from global mouse position
  void update_hover(float global_x, float global_y, const GeometryCache& geom);

  // Update system state - wraps ctrl::update()
  [[nodiscard]] bool update(const std::vector<ctrl::ClusterCellUpdateInfo>& cluster_updates,
                            std::optional<int> redirect_cluster_index = std::nullopt);

  // Process a hotkey action
  [[nodiscard]] ActionResult process_action(HotkeyAction action, const GeometryCache& geom,
                                            float gap_h, float gap_v, float zen_pct);

  // Store the currently selected cell for swap/move operations
  void store_selected_cell();

  // Clear the stored cell reference
  void clear_stored_cell();

  // Get the currently selected cell's rect (if any)
  [[nodiscard]] std::optional<ctrl::Rect> get_selected_rect(const GeometryCache& geom) const;

  // Get the sibling index of the currently selected cell (if any)
  [[nodiscard]] std::optional<int> get_selected_sibling_index() const;
};

} // namespace wintiler
