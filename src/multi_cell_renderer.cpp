#include "multi_cell_renderer.h"

#include "winapi.h"

namespace wintiler {
namespace renderer {

void render(const ctrl::System& system, const std::vector<std::vector<ctrl::Rect>>& geometries,
            const RenderOptions& config, std::optional<StoredCell> stored_cell,
            const std::optional<std::string>& message) {
  // Begin frame
  overlay::begin_frame();

  // Draw all leaf cells (skip clusters with zen cells or fullscreen apps)
  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    const auto& cluster = system.clusters[cluster_idx];

    // Skip this cluster if it has a zen cell (will be rendered in zen loop) or fullscreen app
    if (cluster.zen_cell_index.has_value() || cluster.has_fullscreen_cell) {
      continue;
    }

    // Safety check for geometry bounds
    if (cluster_idx >= geometries.size()) {
      continue;
    }
    const auto& rects = geometries[cluster_idx];

    for (int i = 0; i < cluster.tree.size(); ++i) {
      // Skip non-leaf cells
      if (!cluster.tree.is_leaf(i)) {
        continue;
      }

      const auto& cell_data = cluster.tree[i];

      // Get precomputed rect for this cell
      if (static_cast<size_t>(i) >= rects.size()) {
        continue;
      }
      const auto& rect = rects[static_cast<size_t>(i)];

      // Determine color based on selection/stored state
      overlay::Color color = config.normal_color;

      // Check if this is the selected cell
      if (system.selection.has_value() &&
          static_cast<size_t>(system.selection->cluster_index) == cluster_idx &&
          system.selection->cell_index == i) {
        color = config.selected_color;
      }

      // Check if this is the stored cell (operation) - overrides selection color
      if (stored_cell.has_value() && stored_cell->cluster_index == cluster_idx) {
        if (cell_data.leaf_id.has_value() && cell_data.leaf_id.value() == stored_cell->leaf_id) {
          color = config.stored_color;
        }
      }

      // Draw rectangle immediately
      overlay::draw_rect({
          rect.x,
          rect.y,
          rect.width,
          rect.height,
          color,
          config.border_width,
      });
    }
  }

  // Draw zen cell overlays for each cluster (skip fullscreen clusters)
  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    const auto& cluster = system.clusters[cluster_idx];
    if (!cluster.zen_cell_index.has_value() || cluster.has_fullscreen_cell) {
      continue;
    }

    int zen_cell_index = *cluster.zen_cell_index;

    // Safety check for geometry bounds
    if (cluster_idx >= geometries.size()) {
      continue;
    }
    const auto& rects = geometries[cluster_idx];
    if (static_cast<size_t>(zen_cell_index) >= rects.size()) {
      continue;
    }

    // Get precomputed zen rect (already computed with zen_percentage in compute_cluster_geometry)
    const auto& zen_rect = rects[static_cast<size_t>(zen_cell_index)];

    // Determine color based on selection state
    overlay::Color color = config.normal_color;
    if (system.selection.has_value() &&
        static_cast<size_t>(system.selection->cluster_index) == cluster_idx &&
        system.selection->cell_index == zen_cell_index) {
      color = config.selected_color;
    }

    // Check if zen cell is also the stored cell
    if (stored_cell.has_value() && stored_cell->cluster_index == cluster_idx) {
      const auto& cell_data = cluster.tree[zen_cell_index];
      if (cell_data.leaf_id.has_value() && cell_data.leaf_id.value() == stored_cell->leaf_id) {
        color = config.stored_color;
      }
    }

    // Draw zen rectangle
    overlay::draw_rect({
        zen_rect.x,
        zen_rect.y,
        zen_rect.width,
        zen_rect.height,
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
        // Estimate toast width: ~0.6x font size per character + 32px padding
        float estimated_width =
            static_cast<float>(message->length()) * config.toast_font_size * 0.6f + 32.0f;
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
