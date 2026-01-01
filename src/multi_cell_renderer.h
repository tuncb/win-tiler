#pragma once

#include <optional>
#include <string>
#include <unordered_set>

#include "model.h"
#include "multi_cells.h"
#include "overlay.h"

namespace wintiler {
namespace renderer {

// Render-specific options (subset of VisualizationOptions without timing)
struct RenderOptions {
  overlay::Color normal_color{255, 255, 255, 100};
  overlay::Color selected_color{0, 120, 255, 200};
  overlay::Color stored_color{255, 180, 0, 200};
  float border_width = 3.0f;
  float toast_font_size = 60.0f;
  float zen_percentage = 0.85f; // Zen cell size as percentage of cluster (0.0-1.0)
};

// Render the cell system
// - system: The multi-cluster system to render
// - config: Colors and styling
// - stored_cell: Optional stored cell (cluster_index, leafId) to highlight
// - message: Optional text to show at bottom-right of primary monitor
// - fullscreen_clusters: Set of cluster indices that have fullscreen apps (skip rendering)
void render(const cells::System& system, const RenderOptions& config,
            std::optional<StoredCell> stored_cell, const std::string& message,
            const std::unordered_set<size_t>& fullscreen_clusters);

} // namespace renderer
} // namespace wintiler
