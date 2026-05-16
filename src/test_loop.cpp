#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <vector>

#include "loop.h"
#include "runtime_support.h"

namespace wintiler {

TEST_SUITE("loop") {
  TEST_CASE("frames without a desktop id keep exit hotkeys immediate") {
    CHECK(classify_no_desktop_hotkey(HotkeyAction::Exit) == NoDesktopHotkeyAction::Exit);
  }

  TEST_CASE("frames without a desktop id keep pause hotkeys immediate") {
    CHECK(classify_no_desktop_hotkey(HotkeyAction::TogglePause) ==
          NoDesktopHotkeyAction::EnterManualPause);
  }

  TEST_CASE("frames without a desktop id discard non control hotkeys instead of deferring them") {
    CHECK(classify_no_desktop_hotkey(HotkeyAction::NavigateLeft) == NoDesktopHotkeyAction::Ignore);
  }

  TEST_CASE("frames without a desktop id keep window dump hotkeys immediate") {
    CHECK(classify_no_desktop_hotkey(HotkeyAction::DumpWindowManagement) ==
          NoDesktopHotkeyAction::DumpWindowManagement);
  }

  TEST_CASE("frames without a desktop id do nothing when there is no queued hotkey") {
    CHECK(classify_no_desktop_hotkey(std::nullopt) == NoDesktopHotkeyAction::None);
  }

  TEST_CASE("manual pause only resumes on the pause hotkey") {
    CHECK(classify_manual_pause_hotkey(HotkeyAction::TogglePause) ==
          ManualPauseHotkeyAction::Resume);
  }

  TEST_CASE("manual pause keeps the window dump hotkey immediate") {
    CHECK(classify_manual_pause_hotkey(HotkeyAction::DumpWindowManagement) ==
          ManualPauseHotkeyAction::DumpWindowManagement);
  }

  TEST_CASE("manual pause ignores normal tiling hotkeys") {
    CHECK(classify_manual_pause_hotkey(HotkeyAction::NavigateLeft) ==
          ManualPauseHotkeyAction::Ignore);
  }

  TEST_CASE("manual pause does nothing when there is no queued hotkey") {
    CHECK(classify_manual_pause_hotkey(std::nullopt) == ManualPauseHotkeyAction::None);
  }

  TEST_CASE("cluster update extraction reuses retained leaf buffers") {
    winapi::LoopInputState input;
    input.windows_per_monitor.resize(1);
    input.windows_per_monitor[0].push_back(
        {reinterpret_cast<winapi::HWND_T>(1), false, false, false, std::nullopt});
    input.windows_per_monitor[0].push_back(
        {reinterpret_cast<winapi::HWND_T>(2), false, false, false, std::nullopt});

    std::vector<ctrl::ClusterCellUpdateInfo> updates(1);
    updates[0].leaf_ids.reserve(8);
    const auto retained_capacity = updates[0].leaf_ids.capacity();
    updates[0].leaf_ids.push_back(99);
    updates[0].has_fullscreen_cell = true;

    extract_cluster_updates_from_input_into(input, updates);

    const std::vector<size_t> expected_leaf_ids{1, 2};
    REQUIRE(updates.size() == 1);
    CHECK(updates[0].leaf_ids.capacity() >= retained_capacity);
    CHECK(updates[0].leaf_ids == expected_leaf_ids);
    CHECK(updates[0].has_fullscreen_cell == false);
  }

  TEST_CASE("managed window extraction clears stale state and reuses monitor buffers") {
    winapi::LoopInputState input;
    input.windows_per_monitor.resize(1);
    input.windows_per_monitor[0].push_back(
        {nullptr, false, false, false, winapi::WindowPosition{1, 2, 3, 4}});
    input.windows_per_monitor[0].push_back({reinterpret_cast<winapi::HWND_T>(7), true, false, true,
                                            winapi::WindowPosition{10, 20, 300, 400}});

    std::vector<std::vector<ManagedWindowState>> states(1);
    states[0].reserve(4);
    const auto retained_capacity = states[0].capacity();
    states[0].push_back({99, false, false, false, std::nullopt});

    extract_managed_window_states_from_input_into(input, states);

    REQUIRE(states.size() == 1);
    CHECK(states[0].capacity() >= retained_capacity);
    REQUIRE(states[0].size() == 1);
    CHECK(states[0][0].leaf_id == 7);
    CHECK(states[0][0].is_fullscreen == true);
    CHECK(states[0][0].is_minimized == true);
    REQUIRE(states[0][0].actual_rect.has_value());
    CHECK(states[0][0].actual_rect->x == doctest::Approx(10.0f));
    CHECK(states[0][0].actual_rect->height == doctest::Approx(400.0f));
  }

  TEST_CASE("cluster option resolution reuses retained option buffer capacity") {
    std::vector<winapi::MonitorInfo> monitors = {{reinterpret_cast<winapi::HMONITOR_T>(1),
                                                  "DISPLAY1",
                                                  {0, 0, 800, 600},
                                                  {0, 0, 800, 560},
                                                  true},
                                                 {reinterpret_cast<winapi::HMONITOR_T>(2),
                                                  "DISPLAY2",
                                                  {800, 0, 1600, 600},
                                                  {800, 0, 1600, 560},
                                                  false}};
    GlobalOptions options;
    std::vector<ClusterTilingOptions> cluster_options;
    cluster_options.reserve(8);
    const auto retained_capacity = cluster_options.capacity();

    resolve_cluster_tiling_options_into(monitors, options, cluster_options);

    CHECK(cluster_options.capacity() >= retained_capacity);
    CHECK(cluster_options.size() == monitors.size());
  }
}

} // namespace wintiler

#endif // !DOCTEST_CONFIG_DISABLE
