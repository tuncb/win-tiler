#include "multi_cell_renderer.h"

#include "winapi.h"

namespace wintiler {
namespace renderer {

std::optional<overlay::Color>
cell_outline_color(const ctrl::System& system, size_t cluster_index, int cell_index,
                   const RenderOptions& config, std::optional<size_t> active_leaf_id) {
  if (config.show_only_active_window) {
    const auto& tree = system.clusters[cluster_index].tree;
    if (!active_leaf_id.has_value() || !tree.is_leaf(cell_index) ||
        tree[cell_index].leaf_id != active_leaf_id) {
      return std::nullopt;
    }
    return config.selected_color;
  }

  if (system.selection.has_value() &&
      static_cast<size_t>(system.selection->cluster_index) == cluster_index &&
      system.selection->cell_index == cell_index) {
    return config.selected_color;
  }
  return config.normal_color;
}

void render(const ctrl::System& system, const std::vector<std::vector<ctrl::Rect>>& geometries,
            const RenderOptions& config, const std::optional<std::string>& message,
            bool suppress_rectangles, std::optional<size_t> active_leaf_id) {
  // Begin frame
  overlay::begin_frame();

  if (!suppress_rectangles && config.show_rectangles && config.border_width > 0.0f) {
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

        // Get precomputed rect for this cell
        if (static_cast<size_t>(i) >= rects.size()) {
          continue;
        }
        const auto& rect = rects[static_cast<size_t>(i)];

        auto color = cell_outline_color(system, cluster_idx, i, config, active_leaf_id);
        if (!color.has_value()) {
          continue;
        }

        // Draw rectangle immediately
        overlay::draw_rect({
            rect.x,
            rect.y,
            rect.width,
            rect.height,
            *color,
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

      auto color = cell_outline_color(system, cluster_idx, zen_cell_index, config, active_leaf_id);
      if (!color.has_value()) {
        continue;
      }

      // Draw zen rectangle
      overlay::draw_rect({
          zen_rect.x,
          zen_rect.y,
          zen_rect.width,
          zen_rect.height,
          *color,
          config.border_width,
      });
    }
  }

  // Draw message if provided
  if (message.has_value()) {
    std::vector<winapi::MonitorInfo> monitors;
    winapi::fill_monitors(monitors);
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
