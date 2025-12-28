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
  // Begin frame
  overlay::begin_frame();

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

      // Check if this is the stored cell (operation) - prioritize over selection
      if (storedCell.has_value() && storedCell->first == pc.id) {
        if (cell.leafId.has_value() && cell.leafId.value() == storedCell->second) {
          color = config.storedColor;
        }
      }
      // Check if this is the selected cell
      else if (system.selection.has_value() && system.selection->clusterId == pc.id &&
               system.selection->cellIndex == i) {
        color = config.selectedColor;
      }

      // Draw rectangle immediately
      overlay::draw_rect({
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
        // Estimate toast width: ~30px per character (60pt font) + 16px padding
        float estimatedWidth = static_cast<float>(message.length()) * 30.0f + 16.0f;
        float toastHeight = 90.0f; // Approximate height for 60pt font + padding
        float padding = 20.0f;

        // Position so the RIGHT edge is at workArea.right - padding
        float textX = static_cast<float>(monitor.workArea.right) - padding - estimatedWidth;
        float textY = static_cast<float>(monitor.workArea.bottom) - padding - toastHeight;

        overlay::draw_toast({
            message,
            textX,
            textY,
            {40, 40, 40, 220},    // Dark background
            {255, 255, 255, 255}, // White text
            60.0f,                // Font size
        });
        break;
      }
    }
  }

  // End frame and present
  overlay::end_frame();
}

} // namespace renderer
} // namespace wintiler
