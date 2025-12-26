#pragma once

#include <optional>
#include <string>
#include <utility>

#include "multi_cells.h"
#include "overlay.h"

namespace wintiler {
namespace renderer {

// Configuration for cell rendering colors
struct RenderConfig {
  overlay::Color normalColor;   // Color for normal leaf cells
  overlay::Color selectedColor; // Color for the selected cell
  overlay::Color storedColor;   // Color for the stored cell (swap/move target)
  float borderWidth;            // Border width for cell outlines
};

// Create default render configuration
RenderConfig defaultConfig();

// Render the cell system
// - system: The multi-cluster system to render
// - config: Colors and styling
// - storedCell: Optional stored cell (ClusterId, leafId) to highlight
// - message: Optional text to show at bottom-right of primary monitor
void render(const cells::System& system, const RenderConfig& config,
            std::optional<std::pair<cells::ClusterId, size_t>> storedCell,
            const std::string& message);

} // namespace renderer
} // namespace wintiler
