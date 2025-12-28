#include "multi_cell_renderer.h"

#include "winapi.h"

namespace wintiler {
namespace renderer {

void render(const cells::System& system, const RenderOptions& config,
            std::optional<std::pair<cells::ClusterId, size_t>> stored_cell,
            const std::string& message) {
  // Begin frame
  overlay::begin_frame();

  // Draw all leaf cells
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      const auto& cell = pc.cluster.cells[i];

      // Skip non-leaf and dead cells
      if (cell.is_dead || cell.first_child.has_value() || cell.second_child.has_value()) {
        continue;
      }

      // Get global rect for this cell
      cells::Rect global_rect = cells::get_cell_global_rect(pc, i);

      // Determine color based on selection/stored state
      overlay::Color color = config.normal_color;

      // Check if this is the stored cell (operation) - prioritize over selection
      if (stored_cell.has_value() && stored_cell->first == pc.id) {
        if (cell.leaf_id.has_value() && cell.leaf_id.value() == stored_cell->second) {
          color = config.stored_color;
        }
      }
      // Check if this is the selected cell
      else if (system.selection.has_value() && system.selection->cluster_id == pc.id &&
               system.selection->cell_index == i) {
        color = config.selected_color;
      }

      // Draw rectangle immediately
      overlay::draw_rect({
          global_rect.x,
          global_rect.y,
          global_rect.width,
          global_rect.height,
          color,
          config.border_width,
      });
    }
  }

  // Draw message if provided
  if (!message.empty()) {
    auto monitors = winapi::get_monitors();
    for (const auto& monitor : monitors) {
      if (monitor.isPrimary) {
        // Position at bottom-right of work area with padding
        // Estimate toast width: ~0.5x font size per character + 16px padding
        float estimated_width =
            static_cast<float>(message.length()) * config.toast_font_size * 0.5f + 16.0f;
        float toast_height = config.toast_font_size * 1.5f; // Approximate height + padding
        float padding = 20.0f;

        // Position so the RIGHT edge is at workArea.right - padding
        float text_x = static_cast<float>(monitor.workArea.right) - padding - estimated_width;
        float text_y = static_cast<float>(monitor.workArea.bottom) - padding - toast_height;

        overlay::draw_toast({
            message,
            text_x,
            text_y,
            {40, 40, 40, 220},    // Dark background
            {255, 255, 255, 255}, // White text
            config.toast_font_size,
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
