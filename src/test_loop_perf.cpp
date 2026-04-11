#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <chrono>
#include <string>

#include "loop_perf.h"

using namespace wintiler;

TEST_SUITE("loop_perf") {
  TEST_CASE("collector snapshot summarizes stage samples and counters") {
    LoopPerfCollector collector(true);
    auto base = std::chrono::steady_clock::time_point{};
    collector.window_start = base;

    collector.record_stage(LoopPerfStage::GatherInput, std::chrono::milliseconds(1));
    collector.record_stage(LoopPerfStage::GatherInput, std::chrono::milliseconds(3));
    collector.record_stage(LoopPerfStage::GatherInput, std::chrono::milliseconds(5));
    collector.record_stage(LoopPerfStage::ActiveTotal, std::chrono::milliseconds(4));
    collector.record_stage(LoopPerfStage::ActiveTotal, std::chrono::milliseconds(6));
    collector.note_active_frame();
    collector.note_active_frame();
    collector.note_drag_only_frame();
    collector.note_apply_tiles_frame();

    auto summary = collector.snapshot(base + std::chrono::seconds(5));
    const auto& gather = summary.stages[static_cast<size_t>(LoopPerfStage::GatherInput)];
    const auto& total = summary.stages[static_cast<size_t>(LoopPerfStage::ActiveTotal)];

    CHECK(summary.window_seconds == doctest::Approx(5.0));
    CHECK(summary.active_frames == 2);
    CHECK(summary.drag_only_frames == 1);
    CHECK(summary.apply_tiles_frames == 1);
    CHECK(summary.topology_changed_frames == 0);
    CHECK(gather.sample_count == 3);
    CHECK(gather.average_ms == doctest::Approx(3.0));
    CHECK(gather.p95_ms == doctest::Approx(5.0));
    CHECK(gather.max_ms == doctest::Approx(5.0));
    CHECK(total.sample_count == 2);
    CHECK(total.average_ms == doctest::Approx(5.0));
    CHECK(total.p95_ms == doctest::Approx(6.0));
    CHECK(total.max_ms == doctest::Approx(6.0));
  }

  TEST_CASE("format report includes summary and named stages") {
    LoopPerfCollector collector(true);
    auto base = std::chrono::steady_clock::time_point{};
    collector.window_start = base;

    collector.record_stage(LoopPerfStage::GatherInput, std::chrono::milliseconds(2));
    collector.record_stage(LoopPerfStage::Render, std::chrono::milliseconds(1));
    collector.record_stage(LoopPerfStage::ActiveTotal, std::chrono::milliseconds(5));
    collector.note_active_frame();
    collector.note_topology_changed_frame();

    std::string report = collector.format_report(base + std::chrono::seconds(5));

    CHECK(report.find("[perf] window=5.00s") != std::string::npos);
    CHECK(report.find("active_frames=1") != std::string::npos);
    CHECK(report.find("topology_changed=1") != std::string::npos);
    CHECK(report.find("gather_input: count=1") != std::string::npos);
    CHECK(report.find("render: count=1") != std::string::npos);
    CHECK(report.find("build_frame_input: count=0") != std::string::npos);
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
