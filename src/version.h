#pragma once

#include <string>

namespace wintiler {

constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 5;
constexpr const char* VERSION_LABEL = "alpha";

// Build version string from constants (single source of truth)
inline std::string get_version_string() {
  std::string version = std::to_string(VERSION_MAJOR) + "." + std::to_string(VERSION_MINOR) + "." +
                        std::to_string(VERSION_PATCH);
  if (VERSION_LABEL[0] != '\0') {
    version += "-";
    version += VERSION_LABEL;
  }
  return version;
}

} // namespace wintiler
