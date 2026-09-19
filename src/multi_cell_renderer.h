#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine.h"
#include "options.h"

namespace wintiler {
namespace renderer {

// Render the cell system
// - system: The multi-cluster system to render
// - geometries: Precomputed geometries for all clusters (outer index = cluster, inner index = cell)
// - config: Colors and styling
// - message: Optional text to show at bottom-right of primary monitor
// Skips clusters with has_fullscreen_cell set
void render(const ctrl::System& system, const std::vector<std::vector<ctrl::Rect>>& geometries,
            const RenderOptions& config, const std::optional<std::string>& message,
            bool suppress_rectangles);

} // namespace renderer
} // namespace wintiler
