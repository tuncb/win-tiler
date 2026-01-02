#pragma once

#include <spdlog/spdlog.h>

#include <chrono>

namespace wintiler {

// Helper to time a function and log its duration at trace level
template <typename F>
auto timed(const char* name, F&& func) {
  auto start = std::chrono::high_resolution_clock::now();
  auto result = func();
  auto end = std::chrono::high_resolution_clock::now();
  spdlog::trace("{}: {}us", name,
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return result;
}

// Helper for void functions
template <typename F>
void timed_void(const char* name, F&& func) {
  auto start = std::chrono::high_resolution_clock::now();
  func();
  auto end = std::chrono::high_resolution_clock::now();
  spdlog::trace("{}: {}us", name,
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

} // namespace wintiler
