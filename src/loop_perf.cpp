#include "loop_perf.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace wintiler {
namespace {

constexpr size_t kLoopPerfSampleReserve = 512;

double duration_to_ms(std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

double percentile_ms(const std::vector<double>& samples, double percentile) {
  if (samples.empty()) {
    return 0.0;
  }

  std::vector<double> sorted = samples;
  std::sort(sorted.begin(), sorted.end());

  double scaled_rank = std::ceil(percentile * static_cast<double>(sorted.size()));
  size_t rank = static_cast<size_t>(scaled_rank > 1.0 ? scaled_rank - 1.0 : 0.0);
  if (rank >= sorted.size()) {
    rank = sorted.size() - 1;
  }
  return sorted[rank];
}

LoopPerfSnapshotStage summarize_stage(const LoopPerfStageStats& stage) {
  LoopPerfSnapshotStage summary;
  summary.sample_count = stage.samples_ms.size();
  summary.max_ms = stage.max_ms;
  if (summary.sample_count == 0) {
    return summary;
  }

  summary.average_ms = stage.total_ms / static_cast<double>(summary.sample_count);
  summary.p95_ms = percentile_ms(stage.samples_ms, 0.95);
  return summary;
}

} // namespace

void LoopPerfStageStats::reserve(size_t capacity) {
  samples_ms.reserve(capacity);
}

void LoopPerfStageStats::record(std::chrono::steady_clock::duration duration) {
  double elapsed_ms = duration_to_ms(duration);
  samples_ms.push_back(elapsed_ms);
  total_ms += elapsed_ms;
  if (elapsed_ms > max_ms) {
    max_ms = elapsed_ms;
  }
}

void LoopPerfStageStats::reset() {
  samples_ms.clear();
  total_ms = 0.0;
  max_ms = 0.0;
}

LoopPerfCollector::LoopPerfCollector(bool enabled_flag, std::chrono::seconds interval)
    : enabled(enabled_flag), window_start(std::chrono::steady_clock::now()),
      report_interval(interval) {
  if (!enabled) {
    return;
  }

  for (auto& stage : stages) {
    stage.reserve(kLoopPerfSampleReserve);
  }
}

void LoopPerfCollector::record_stage(LoopPerfStage stage,
                                     std::chrono::steady_clock::duration duration) {
  if (!enabled) {
    return;
  }

  stages[static_cast<size_t>(stage)].record(duration);
}

void LoopPerfCollector::note_active_frame() {
  if (!enabled) {
    return;
  }
  ++active_frames;
}

void LoopPerfCollector::note_drag_only_frame() {
  if (!enabled) {
    return;
  }
  ++drag_only_frames;
}

void LoopPerfCollector::note_apply_tiles_frame() {
  if (!enabled) {
    return;
  }
  ++apply_tiles_frames;
}

void LoopPerfCollector::note_topology_changed_frame() {
  if (!enabled) {
    return;
  }
  ++topology_changed_frames;
}

bool LoopPerfCollector::has_samples() const {
  if (!enabled) {
    return false;
  }
  if (active_frames > 0) {
    return true;
  }
  for (const auto& stage : stages) {
    if (!stage.samples_ms.empty()) {
      return true;
    }
  }
  return false;
}

bool LoopPerfCollector::should_report(std::chrono::steady_clock::time_point now) const {
  if (!has_samples()) {
    return false;
  }
  return now - window_start >= report_interval;
}

LoopPerfSnapshot LoopPerfCollector::snapshot(std::chrono::steady_clock::time_point now) const {
  LoopPerfSnapshot result;
  result.window_seconds = std::chrono::duration<double>(now - window_start).count();
  result.active_frames = active_frames;
  result.drag_only_frames = drag_only_frames;
  result.apply_tiles_frames = apply_tiles_frames;
  result.topology_changed_frames = topology_changed_frames;

  for (size_t index = 0; index < stages.size(); ++index) {
    result.stages[index] = summarize_stage(stages[index]);
  }

  return result;
}

std::string LoopPerfCollector::format_report(std::chrono::steady_clock::time_point now) const {
  LoopPerfSnapshot summary = snapshot(now);
  const auto& total = summary.stages[static_cast<size_t>(LoopPerfStage::ActiveTotal)];

  std::ostringstream stream;
  stream << std::fixed << std::setprecision(2);
  stream << "[perf] window=" << summary.window_seconds << "s"
         << " active_frames=" << summary.active_frames << " drag_only=" << summary.drag_only_frames
         << " apply_tiles=" << summary.apply_tiles_frames
         << " topology_changed=" << summary.topology_changed_frames;

  if (total.sample_count > 0) {
    stream << " active_avg=" << total.average_ms << "ms"
           << " active_p95=" << total.p95_ms << "ms"
           << " active_max=" << total.max_ms << "ms";
  }
  stream << '\n';

  for (LoopPerfStage stage :
       {LoopPerfStage::GatherInput, LoopPerfStage::BuildFrameInput, LoopPerfStage::ComputeGeometry,
        LoopPerfStage::Engine, LoopPerfStage::Apply, LoopPerfStage::Render}) {
    const auto& stage_summary = summary.stages[static_cast<size_t>(stage)];
    stream << "  " << loop_perf_stage_name(stage) << ": count=" << stage_summary.sample_count
           << " avg=" << stage_summary.average_ms << "ms"
           << " p95=" << stage_summary.p95_ms << "ms"
           << " max=" << stage_summary.max_ms << "ms\n";
  }

  return stream.str();
}

void LoopPerfCollector::reset_window(std::chrono::steady_clock::time_point now) {
  window_start = now;
  active_frames = 0;
  drag_only_frames = 0;
  apply_tiles_frames = 0;
  topology_changed_frames = 0;

  for (auto& stage : stages) {
    stage.reset();
  }
}

std::string_view loop_perf_stage_name(LoopPerfStage stage) {
  switch (stage) {
  case LoopPerfStage::GatherInput:
    return "gather_input";
  case LoopPerfStage::BuildFrameInput:
    return "build_frame_input";
  case LoopPerfStage::ComputeGeometry:
    return "compute_geometry";
  case LoopPerfStage::Engine:
    return "engine";
  case LoopPerfStage::Apply:
    return "apply";
  case LoopPerfStage::Render:
    return "render";
  case LoopPerfStage::ActiveTotal:
    return "active_total";
  case LoopPerfStage::Count:
    break;
  }

  return "unknown";
}

} // namespace wintiler
