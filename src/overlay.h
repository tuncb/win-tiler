#pragma once

#include <cstdint>
#include <string>

namespace wintiler {
namespace overlay {

// RGBA color (0-255 per channel)
struct Color {
  uint8_t r, g, b, a;
};

// Rectangle to draw (screen coordinates)
struct DrawRect {
  float x, y, width, height;
  Color color;
  float border_width; // 0 for filled, >0 for outline only
};

// Toast message (temporary text display)
struct Toast {
  std::string text;
  float x, y;        // Position in virtual screen coordinates
  Color bg_color;    // Background
  Color text_color;  // Text
  float duration_ms; // How long to show
};

// Initialize the overlay system. Returns true on success.
// Creates the transparent window and D2D resources.
bool init();

// Shutdown the overlay system. Releases all resources.
void shutdown();

// Clear all rectangles
void clear_rects();

// Add a rectangle to draw
void add_rect(const DrawRect& rect);

// Show a toast message (will auto-hide after duration)
void show_toast(const Toast& toast);

// Clear all active toasts
void clear_toasts();

// Render one frame. Call this each iteration of the main loop.
// Handles message pump for overlay window, draws all rects and active toasts.
void render();

// Check if overlay is initialized
bool is_initialized();

} // namespace overlay
} // namespace wintiler
