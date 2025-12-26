#include "multi_cell_renderer.h"

#include "winapi.h"

namespace wintiler {
namespace renderer {

RenderConfig defaultConfig() {
  return RenderConfig{
      .normalColor = {255, 255, 255, 100}, // Semi-transparent white
      .selectedColor = {0, 120, 255, 200}, // Blue
      .storedColor = {255, 180, 0, 200},   // Orange
      .borderWidth = 3.0f,
  };
}

void render(const cells::System& system, const RenderConfig& config,
            std::optional<std::pair<cells::ClusterId, size_t>> storedCell,
            const std::string& message) {
  // Clear previous frame
  overlay::clear_rects();
  overlay::clear_toasts();

  // Draw all leaf cells
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      const auto& cell = pc.cluster.cells[i];

      // Skip non-leaf and dead cells
      if (cell.isDead || cell.firstChild.has_value() || cell.secondChild.has_value()) {
        continue;
      }

      // Get global rect for this cell
      cells::Rect globalRect = cells::getCellGlobalRect(pc, i);

      // Determine color based on selection/stored state
      overlay::Color color = config.normalColor;

      // Check if this is the selected cell
      if (system.selection.has_value() && system.selection->clusterId == pc.id &&
          system.selection->cellIndex == i) {
        color = config.selectedColor;
      }
      // Check if this is the stored cell
      else if (storedCell.has_value() && storedCell->first == pc.id) {
        if (cell.leafId.has_value() && cell.leafId.value() == storedCell->second) {
          color = config.storedColor;
        }
      }

      // Add rectangle to overlay
      overlay::add_rect({
          globalRect.x,
          globalRect.y,
          globalRect.width,
          globalRect.height,
          color,
          config.borderWidth,
      });
    }
  }

  // Draw message if provided
  if (!message.empty()) {
    auto monitors = winapi::get_monitors();
    for (const auto& monitor : monitors) {
      if (monitor.isPrimary) {
        // Position at bottom-right of work area with padding
        float padding = 20.0f;
        float textX = static_cast<float>(monitor.workArea.right) - padding;
        float textY = static_cast<float>(monitor.workArea.bottom) - padding - 30.0f;

        overlay::show_toast({
            message,
            textX,
            textY,
            {40, 40, 40, 200},    // Dark background
            {255, 255, 255, 255}, // White text
            200.0f,               // Short duration, refreshed each frame
        });
        break;
      }
    }
  }

  // Render the frame
  overlay::render();
}

} // namespace renderer
} // namespace wintiler
