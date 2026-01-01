#include "multi_cell_renderer.h"

#include "winapi.h"

namespace wintiler {
namespace renderer {

void render(const cells::System& system, const RenderOptions& config,
            std::optional<StoredCell> stored_cell, const std::optional<std::string>& message,
            const std::unordered_set<size_t>& fullscreen_clusters) {
  // Begin frame
  overlay::begin_frame();

  // Draw all leaf cells (skip clusters with zen cells or fullscreen apps)
  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    const auto& pc = system.clusters[cluster_idx];
    // Skip this cluster if it has a zen cell (will be rendered in zen loop) or fullscreen app
    if (pc.cluster.zen_cell_index.has_value() || fullscreen_clusters.contains(cluster_idx)) {
      continue;
    }

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
      if (stored_cell.has_value() && stored_cell->cluster_index == cluster_idx) {
        if (cell.leaf_id.has_value() && cell.leaf_id.value() == stored_cell->leaf_id) {
          color = config.stored_color;
        }
      }
      // Check if this is the selected cell
      else if (system.selection.has_value() && system.selection->cluster_index == cluster_idx &&
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

  // Draw zen cell overlays for each cluster (skip fullscreen clusters)
  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    const auto& pc = system.clusters[cluster_idx];
    if (!pc.cluster.zen_cell_index.has_value() || fullscreen_clusters.contains(cluster_idx)) {
      continue;
    }

    int zen_cell_index = *pc.cluster.zen_cell_index;

    // Get zen display rect (centered at percentage of cluster)
    cells::Rect zen_display_rect =
        cells::get_cell_display_rect(pc, zen_cell_index, true, config.zen_percentage);

    // Determine color based on selection state
    overlay::Color color = config.normal_color;
    if (system.selection.has_value() && system.selection->cluster_index == cluster_idx &&
        system.selection->cell_index == zen_cell_index) {
      color = config.selected_color;
    }

    // Check if zen cell is also the stored cell
    if (stored_cell.has_value() && stored_cell->cluster_index == cluster_idx) {
      const auto& cell = pc.cluster.cells[static_cast<size_t>(zen_cell_index)];
      if (cell.leaf_id.has_value() && cell.leaf_id.value() == stored_cell->leaf_id) {
        color = config.stored_color;
      }
    }

    // Draw zen rectangle
    overlay::draw_rect({
        zen_display_rect.x,
        zen_display_rect.y,
        zen_display_rect.width,
        zen_display_rect.height,
        color,
        config.border_width,
    });
  }

  // Draw message if provided
  if (message.has_value()) {
    auto monitors = winapi::get_monitors();
    for (const auto& monitor : monitors) {
      if (monitor.isPrimary) {
        // Position at bottom-right of work area with padding
        // Estimate toast width: ~0.5x font size per character + 16px padding
        float estimated_width =
            static_cast<float>(message->length()) * config.toast_font_size * 0.5f + 16.0f;
        float toast_height = config.toast_font_size * 1.5f; // Approximate height + padding
        float padding = 20.0f;

        // Position so the RIGHT edge is at workArea.right - padding
        float text_x = static_cast<float>(monitor.workArea.right) - padding - estimated_width;
        float text_y = static_cast<float>(monitor.workArea.bottom) - padding - toast_height;

        overlay::draw_toast({
            *message,
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
