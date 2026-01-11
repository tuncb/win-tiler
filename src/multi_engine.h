#pragma once

#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

#include "controller.h"
#include "engine.h"

namespace wintiler {

// Generic per-engine state - pairs Engine with system-specific data
template <typename ExtraData>
struct PerEngineState {
  Engine engine;
  ExtraData data;
};

// MultiEngine: manages multiple desktops/engines indexed by DesktopId
// Template parameters:
//   ExtraData - System-specific data stored alongside each Engine
//   DesktopId - Identifier type (default size_t, can be GUID for Windows virtual desktops)
template <typename ExtraData, typename DesktopId = size_t>
class MultiEngine {
public:
  using Desktop = PerEngineState<ExtraData>;

  // Create a new desktop with given ID
  // Initializes the engine with provided cluster infos
  // Returns reference to created desktop, or nullopt if ID already exists
  std::optional<std::reference_wrapper<Desktop>>
  create_desktop(DesktopId id, const std::vector<ctrl::ClusterInitInfo>& infos) {
    if (desktops.contains(id)) {
      return std::nullopt;
    }

    auto [it, inserted] = desktops.emplace(id, Desktop{});
    if (!inserted) {
      return std::nullopt;
    }

    it->second.engine.init(infos);
    return std::ref(it->second);
  }

  // Remove a desktop by ID
  // Returns true if desktop was removed, false if it didn't exist
  // Note: Cannot remove the current desktop
  bool remove_desktop(DesktopId id) {
    if (current_id.has_value() && *current_id == id) {
      return false; // Cannot remove current desktop
    }
    return desktops.erase(id) > 0;
  }

  // Switch current desktop to given ID
  // Returns false if ID doesn't exist
  bool switch_to(DesktopId id) {
    if (!desktops.contains(id)) {
      return false;
    }
    current_id = id;
    return true;
  }

  // Get current desktop
  // Throws std::runtime_error if no current desktop is set
  Desktop& current() {
    if (!current_id.has_value()) {
      throw std::runtime_error("MultiEngine: no current desktop set");
    }
    auto it = desktops.find(*current_id);
    if (it == desktops.end()) {
      throw std::runtime_error("MultiEngine: current desktop not found");
    }
    return it->second;
  }

  const Desktop& current() const {
    if (!current_id.has_value()) {
      throw std::runtime_error("MultiEngine: no current desktop set");
    }
    auto it = desktops.find(*current_id);
    if (it == desktops.end()) {
      throw std::runtime_error("MultiEngine: current desktop not found");
    }
    return it->second;
  }

  // Check if current desktop is set
  bool has_current() const {
    return current_id.has_value();
  }

  // Check if a desktop with given ID exists
  bool has_desktop(DesktopId id) const {
    return desktops.contains(id);
  }

  // Get desktop by ID (may not exist)
  // Returns nullptr if desktop doesn't exist
  Desktop* get(DesktopId id) {
    auto it = desktops.find(id);
    return it != desktops.end() ? &it->second : nullptr;
  }

  const Desktop* get(DesktopId id) const {
    auto it = desktops.find(id);
    return it != desktops.end() ? &it->second : nullptr;
  }

  // Get number of desktops
  size_t desktop_count() const {
    return desktops.size();
  }

  // Get all desktop IDs (useful for iteration/navigation)
  std::vector<DesktopId> desktop_ids() const {
    std::vector<DesktopId> ids;
    ids.reserve(desktops.size());
    for (const auto& [id, _] : desktops) {
      ids.push_back(id);
    }
    return ids;
  }

  // Public members for direct access
  std::map<DesktopId, Desktop> desktops;
  std::optional<DesktopId> current_id;
};

} // namespace wintiler
