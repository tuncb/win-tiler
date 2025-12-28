#pragma once

#include <optional>
#include <string>
#include <utility>

#include "multi_cells.h"
#include "options.h"

namespace wintiler {
namespace renderer {

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
