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
  float x, y;       // Position in virtual screen coordinates
  Color bg_color;   // Background
  Color text_color; // Text
  float font_size;  // Font size in points
};

// Initialize the overlay system. Returns true on success.
// Creates the transparent window and D2D resources.
bool init();

// Shutdown the overlay system. Releases all resources.
void shutdown();

// Begin a new frame. Call once at start of render cycle.
// Pumps window messages, begins D2D drawing, clears to transparent.
void begin_frame();

// Draw a rectangle immediately
void draw_rect(const DrawRect& rect);

// Draw a toast message immediately (caller controls visibility/timing)
void draw_toast(const Toast& toast);

// End the frame and present. Call once at end of render cycle.
void end_frame();

// Present an empty transparent frame (clears any visible overlay content)
void clear();

// Check if overlay is initialized
bool is_initialized();

} // namespace overlay
} // namespace wintiler
