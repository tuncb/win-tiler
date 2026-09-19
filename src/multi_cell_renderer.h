#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine.h"
#include "options.h"

namespace wintiler {
namespace renderer {

// Shared by rendering and its cache snapshot so visibility and colors stay consistent.
[[nodiscard]] std::optional<overlay::Color>
cell_outline_color(const ctrl::System& system, size_t cluster_index, int cell_index,
                   const RenderOptions& config, std::optional<size_t> active_leaf_id);

// Render the cell system
// - system: The multi-cluster system to render
// - geometries: Precomputed geometries for all clusters (outer index = cluster, inner index = cell)
// - config: Colors and styling
// - message: Optional text to show at bottom-right of primary monitor
// Skips clusters with has_fullscreen_cell set
void render(const ctrl::System& system, const std::vector<std::vector<ctrl::Rect>>& geometries,
            const RenderOptions& config, const std::optional<std::string>& message,
            bool suppress_rectangles, std::optional<size_t> active_leaf_id = std::nullopt);

} // namespace renderer
} // namespace wintiler
