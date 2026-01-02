#pragma once

#include <optional>
#include <string>
#include <unordered_set>

#include "model.h"
#include "multi_cells.h"
#include "options.h"

namespace wintiler {
namespace renderer {

// Render the cell system
// - system: The multi-cluster system to render
// - config: Colors and styling
// - stored_cell: Optional stored cell (cluster_index, leafId) to highlight
// - message: Optional text to show at bottom-right of primary monitor
// - fullscreen_clusters: Set of cluster indices that have fullscreen apps (skip rendering)
void render(const cells::System& system, const RenderOptions& config,
            std::optional<StoredCell> stored_cell, const std::optional<std::string>& message,
            const std::unordered_set<size_t>& fullscreen_clusters);

} // namespace renderer
} // namespace wintiler
