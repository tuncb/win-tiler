#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wintiler {

enum class LoopPerfStage : size_t {
  GatherInput = 0,
  BuildFrameInput,
  ComputeGeometry,
  Engine,
  Apply,
  Render,
  ActiveTotal,
  Count,
};

struct LoopPerfStageStats {
  std::vector<double> samples_ms;
  double total_ms = 0.0;
  double max_ms = 0.0;

  void reserve(size_t capacity);
  void record(std::chrono::steady_clock::duration duration);
  void reset();
};

struct LoopPerfSnapshotStage {
  size_t sample_count = 0;
  double average_ms = 0.0;
  double p95_ms = 0.0;
  double max_ms = 0.0;
};

struct LoopPerfSnapshot {
  double window_seconds = 0.0;
  uint64_t active_frames = 0;
  uint64_t drag_only_frames = 0;
  uint64_t apply_tiles_frames = 0;
  uint64_t topology_changed_frames = 0;
  std::array<LoopPerfSnapshotStage, static_cast<size_t>(LoopPerfStage::Count)> stages;
};

struct LoopPerfCollector {
  bool enabled = false;
  std::chrono::steady_clock::time_point window_start = {};
  std::chrono::seconds report_interval = std::chrono::seconds(5);
  uint64_t active_frames = 0;
  uint64_t drag_only_frames = 0;
  uint64_t apply_tiles_frames = 0;
  uint64_t topology_changed_frames = 0;
  std::array<LoopPerfStageStats, static_cast<size_t>(LoopPerfStage::Count)> stages;

  LoopPerfCollector() = default;
  explicit LoopPerfCollector(bool enabled_flag,
                             std::chrono::seconds interval = std::chrono::seconds(5));

  void record_stage(LoopPerfStage stage, std::chrono::steady_clock::duration duration);
  void note_active_frame();
  void note_drag_only_frame();
  void note_apply_tiles_frame();
  void note_topology_changed_frame();
  [[nodiscard]] bool has_samples() const;
  [[nodiscard]] bool should_report(std::chrono::steady_clock::time_point now) const;
  [[nodiscard]] LoopPerfSnapshot snapshot(std::chrono::steady_clock::time_point now) const;
  [[nodiscard]] std::string format_report(std::chrono::steady_clock::time_point now) const;
  void reset_window(std::chrono::steady_clock::time_point now);
};

[[nodiscard]] std::string_view loop_perf_stage_name(LoopPerfStage stage);

} // namespace wintiler
