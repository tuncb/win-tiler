#pragma once

#include <optional>
#include <string>
#include <utility>

#include "multi_cells.h"
#include "overlay.h"

namespace wintiler {
namespace renderer {

// Render-specific options (subset of VisualizationOptions without timing)
struct RenderOptions {
  overlay::Color normal_color{255, 255, 255, 100};
  overlay::Color selected_color{0, 120, 255, 200};
  overlay::Color stored_color{255, 180, 0, 200};
  overlay::Color zen_color{255, 215, 0, 200}; // Gold color for zen cells
  float border_width = 3.0f;
  float toast_font_size = 60.0f;
  float zen_percentage = 0.85f; // Zen cell size as percentage of cluster (0.0-1.0)
};

// Render the cell system
// - system: The multi-cluster system to render
// - config: Colors and styling
// - stored_cell: Optional stored cell (ClusterId, leafId) to highlight
// - message: Optional text to show at bottom-right of primary monitor
void render(const cells::System& system, const RenderOptions& config,
            std::optional<std::pair<cells::ClusterId, size_t>> stored_cell,
            const std::string& message);

} // namespace renderer
} // namespace wintiler
