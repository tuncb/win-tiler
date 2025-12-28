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
  overlay::Color normalColor{255, 255, 255, 100};
  overlay::Color selectedColor{0, 120, 255, 200};
  overlay::Color storedColor{255, 180, 0, 200};
  float borderWidth = 3.0f;
  float toastFontSize = 60.0f;
};

// Render the cell system
// - system: The multi-cluster system to render
// - config: Colors and styling
// - storedCell: Optional stored cell (ClusterId, leafId) to highlight
// - message: Optional text to show at bottom-right of primary monitor
void render(const cells::System& system, const RenderOptions& config,
            std::optional<std::pair<cells::ClusterId, size_t>> storedCell,
            const std::string& message);

} // namespace renderer
} // namespace wintiler
