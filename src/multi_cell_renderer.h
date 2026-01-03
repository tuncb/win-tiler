#pragma once

#include <optional>
#include <string>

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
// Skips clusters with has_fullscreen_cell set
void render(const cells::System& system, const RenderOptions& config,
            std::optional<StoredCell> stored_cell, const std::optional<std::string>& message);

} // namespace renderer
} // namespace wintiler
