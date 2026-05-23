#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <chrono>
#include <string>

#include "loop.h"
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

TEST_SUITE("loop_desktop_state") {
  TEST_CASE("mark_all_desktops_for_retile clears the initial tile pass on every desktop") {
    std::vector<ctrl::ClusterInitInfo> cluster_infos = {
        {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}}};

    MultiEngine<LoopDesktopData, int> multi_engine;
    auto desktop_one = multi_engine.create_desktop(1, cluster_infos);
    auto desktop_two = multi_engine.create_desktop(2, cluster_infos);
    REQUIRE(desktop_one.has_value());
    REQUIRE(desktop_two.has_value());

    desktop_one->get().data.has_completed_initial_tile_pass = true;
    desktop_two->get().data.has_completed_initial_tile_pass = true;

    mark_all_desktops_for_retile(multi_engine);

    CHECK(desktop_one->get().data.has_completed_initial_tile_pass == false);
    CHECK(desktop_two->get().data.has_completed_initial_tile_pass == false);
  }

  TEST_CASE("reinitialize_all_desktops resets each engine and clears stored state") {
    std::vector<ctrl::ClusterInitInfo> initial_cluster_infos = {
        {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}}};
    std::vector<ctrl::ClusterInitInfo> updated_cluster_infos = {
        {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}},
        {800.0f, 0.0f, 800.0f, 600.0f, 800.0f, 0.0f, 800.0f, 600.0f, {20, 30}}};

    MultiEngine<LoopDesktopData, int> multi_engine;
    auto desktop_one = multi_engine.create_desktop(1, initial_cluster_infos);
    auto desktop_two = multi_engine.create_desktop(2, initial_cluster_infos);
    REQUIRE(desktop_one.has_value());
    REQUIRE(desktop_two.has_value());

    desktop_one->get().engine.stored_cell = StoredCell{0, 1};
    desktop_two->get().engine.stored_cell = StoredCell{0, 2};
    desktop_one->get().data.has_completed_initial_tile_pass = true;
    desktop_two->get().data.has_completed_initial_tile_pass = true;
    desktop_one->get().data.reapply_layout_templates = true;
    desktop_two->get().data.reapply_layout_templates = true;

    reinitialize_all_desktops(multi_engine, updated_cluster_infos);

    CHECK(desktop_one->get().engine.system.clusters.size() == 2);
    CHECK(desktop_two->get().engine.system.clusters.size() == 2);
    CHECK(desktop_one->get().engine.system.clusters[0].window_width == doctest::Approx(800.0f));
    CHECK(desktop_two->get().engine.system.clusters[1].global_x == doctest::Approx(800.0f));
    CHECK_FALSE(desktop_one->get().engine.stored_cell.has_value());
    CHECK_FALSE(desktop_two->get().engine.stored_cell.has_value());
    CHECK(desktop_one->get().data.has_completed_initial_tile_pass == false);
    CHECK(desktop_two->get().data.has_completed_initial_tile_pass == false);
    CHECK(desktop_one->get().data.reapply_layout_templates == false);
    CHECK(desktop_two->get().data.reapply_layout_templates == false);
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
