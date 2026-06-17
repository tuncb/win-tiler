#pragma once

#include <string>

namespace wintiler {

constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 10;
constexpr int VERSION_PATCH = 3;

// Build version string from constants (single source of truth)
inline std::string get_version_string() {
  return std::to_string(VERSION_MAJOR) + "." + std::to_string(VERSION_MINOR) + "." +
         std::to_string(VERSION_PATCH);
}

} // namespace wintiler
