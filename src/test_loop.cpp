#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>
#include <spdlog/spdlog.h>

#include <vector>

#include "loop.h"
#include "runtime_support.h"
#include "winapi.h"

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
    CHECK(classify_no_desktop_hotkey(HotkeyAction::RestartSystem) == NoDesktopHotkeyAction::Ignore);
  }

  TEST_CASE("frames without a desktop id keep window dump hotkeys immediate") {
    CHECK(classify_no_desktop_hotkey(HotkeyAction::DumpWindowManagement) ==
          NoDesktopHotkeyAction::DumpWindowManagement);
  }

  TEST_CASE("frames without a desktop id can unfloat foreground windows") {
    CHECK(classify_no_desktop_hotkey(HotkeyAction::ToggleFloating) ==
          NoDesktopHotkeyAction::ToggleFloating);
  }

  TEST_CASE("frames without a desktop id keep verbose logging hotkeys immediate") {
    CHECK(classify_no_desktop_hotkey(HotkeyAction::ToggleVerboseLogging) ==
          NoDesktopHotkeyAction::ToggleVerboseLogging);
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

  TEST_CASE("manual pause keeps the verbose logging hotkey immediate") {
    CHECK(classify_manual_pause_hotkey(HotkeyAction::ToggleVerboseLogging) ==
          ManualPauseHotkeyAction::ToggleVerboseLogging);
  }

  TEST_CASE("manual pause ignores normal tiling hotkeys") {
    CHECK(classify_manual_pause_hotkey(HotkeyAction::NavigateLeft) ==
          ManualPauseHotkeyAction::Ignore);
    CHECK(classify_manual_pause_hotkey(HotkeyAction::RestartSystem) ==
          ManualPauseHotkeyAction::Ignore);
    CHECK(classify_manual_pause_hotkey(HotkeyAction::ToggleFloating) ==
          ManualPauseHotkeyAction::Ignore);
  }

  TEST_CASE("manual pause does nothing when there is no queued hotkey") {
    CHECK(classify_manual_pause_hotkey(std::nullopt) == ManualPauseHotkeyAction::None);
  }

  TEST_CASE("mouse drag drop exchanges by default and splits with modifier") {
    CHECK(should_exchange_mouse_drag_drop(MouseDragDropAction::Exchange, false, false) == true);
    CHECK(should_exchange_mouse_drag_drop(MouseDragDropAction::Exchange, true, false) == false);
  }

  TEST_CASE("mouse drag drop split option exchanges with modifier") {
    CHECK(should_exchange_mouse_drag_drop(MouseDragDropAction::Split, false, false) == false);
    CHECK(should_exchange_mouse_drag_drop(MouseDragDropAction::Split, true, false) == true);
  }

  TEST_CASE("mouse drag drop treats the right mouse button as a modifier") {
    CHECK(should_exchange_mouse_drag_drop(MouseDragDropAction::Exchange, false, true) == false);
    CHECK(should_exchange_mouse_drag_drop(MouseDragDropAction::Split, false, true) == true);
    CHECK(should_exchange_mouse_drag_drop(MouseDragDropAction::Exchange, true, true) == false);
  }

  TEST_CASE("fullscreen topmost windows remain tiling candidates") {
    CHECK(winapi::should_ignore_topmost_window(false, false) == false);
    CHECK(winapi::should_ignore_topmost_window(true, false) == true);
    CHECK(winapi::should_ignore_topmost_window(true, true) == false);
  }

  TEST_CASE("desktop activation creates and switches multi-engine state") {
    std::vector<ctrl::ClusterInitInfo> cluster_infos = {
        {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}}};
    MultiEngine<LoopDesktopData, std::string> multi_engine;

    auto first_activation = activate_loop_desktop(multi_engine, "desktop-a", cluster_infos);

    REQUIRE(first_activation.has_value());
    CHECK(first_activation->created == true);
    CHECK(first_activation->switched == true);
    REQUIRE(multi_engine.current_id.has_value());
    CHECK(*multi_engine.current_id == "desktop-a");
    CHECK(first_activation->desktop.get().engine.system.clusters.size() == 1);

    auto second_activation = activate_loop_desktop(multi_engine, "desktop-a", cluster_infos);

    REQUIRE(second_activation.has_value());
    CHECK(second_activation->created == false);
    CHECK(second_activation->switched == false);

    auto third_activation = activate_loop_desktop(multi_engine, "desktop-b", cluster_infos);

    REQUIRE(third_activation.has_value());
    CHECK(third_activation->created == true);
    CHECK(third_activation->switched == true);
    REQUIRE(multi_engine.current_id.has_value());
    CHECK(*multi_engine.current_id == "desktop-b");
  }

  TEST_CASE("engine frame input builder normalizes loop state for the engine") {
    winapi::LoopInputState input;
    input.windows_per_monitor.resize(1);
    input.windows_per_monitor[0].push_back({reinterpret_cast<winapi::HWND_T>(7), false, true, false,
                                            winapi::WindowPosition{10, 20, 300, 400}});
    input.cursor_pos = winapi::Point{100, 200};
    input.foreground_window = reinterpret_cast<winapi::HWND_T>(7);
    input.pointer_window = reinterpret_cast<winapi::HWND_T>(7);
    input.is_ctrl_pressed = true;
    input.drag_info = winapi::DragInfo{reinterpret_cast<winapi::HWND_T>(7), true};

    LoopDesktopData desktop_data;
    desktop_data.has_completed_initial_tile_pass = true;
    desktop_data.reapply_layout_templates = true;
    std::vector<ClusterTilingOptions> cluster_options(1);
    LayoutOptions layout_options;
    EngineFrameInput frame_input;

    fill_engine_frame_input(input, desktop_data, cluster_options, true, HotkeyAction::NavigateLeft,
                            layout_options, MouseDragDropAction::Exchange, frame_input);

    REQUIRE(frame_input.cluster_updates.size() == 1);
    CHECK(frame_input.cluster_updates[0].leaf_ids == std::vector<size_t>{7});
    REQUIRE(frame_input.managed_windows.size() == 1);
    REQUIRE(frame_input.managed_windows[0].size() == 1);
    CHECK(frame_input.managed_windows[0][0].leaf_id == 7);
    CHECK(frame_input.managed_windows[0][0].is_maximized == true);
    REQUIRE(frame_input.cursor_pos.has_value());
    CHECK(frame_input.cursor_pos->x == 100);
    CHECK(frame_input.foreground_leaf_id == 7);
    CHECK(frame_input.pointer_window_id == 7);
    CHECK(frame_input.hotkey_action == HotkeyAction::NavigateLeft);
    CHECK(frame_input.auto_zen_on_maximize == true);
    CHECK(frame_input.has_completed_initial_tile_pass == true);
    CHECK(frame_input.reapply_layout_templates == true);
    REQUIRE(frame_input.completed_drag.has_value());
    CHECK(frame_input.completed_drag->leaf_id == 7);
    CHECK(frame_input.completed_drag->do_exchange == false);
    REQUIRE(frame_input.completed_drag->actual_window_rect.has_value());
    CHECK(frame_input.completed_drag->actual_window_rect->x == doctest::Approx(10.0f));
    CHECK(frame_input.completed_drag->actual_window_rect->height == doctest::Approx(400.0f));
    input.foreground_window = nullptr;
    input.pointer_window = nullptr;
    fill_engine_frame_input(input, desktop_data, cluster_options, true, std::nullopt,
                            layout_options, MouseDragDropAction::Exchange, frame_input);
    CHECK_FALSE(frame_input.foreground_leaf_id.has_value());
    CHECK_FALSE(frame_input.pointer_window_id.has_value());
  }

  TEST_CASE("minimum tracking sizes are converted to visible frame sizes") {
    winapi::LoopInputState input;
    input.windows_per_monitor.resize(1);
    winapi::ManagedWindowInfo window;
    window.handle = reinterpret_cast<winapi::HWND_T>(7);
    window.minmax_info = winapi::WindowMinMaxInfo{};
    window.minmax_info->min_track_width = 640;
    window.minmax_info->min_track_height = 480;
    window.actual_rect = winapi::WindowPosition{-1000, 20, 700, 500};
    window.outer_rect = winapi::WindowPosition{-1012, 20, 724, 512};
    window.dpi = 144;
    input.windows_per_monitor[0] = {window};
    std::vector<std::vector<ManagedWindowState>> states;
    extract_managed_window_states_from_input_into(input, states);
    CHECK(states[0][0].min_track_width == 616);
    CHECK(states[0][0].min_track_height == 468);
    CHECK(states[0][0].dpi == 144);

    // Unknown limits stay unknown; missing DWM/outer bounds retain the raw limit.
    input.windows_per_monitor[0][0].minmax_info->min_track_width = 0;
    extract_managed_window_states_from_input_into(input, states);
    CHECK(states[0][0].min_track_width == 0);
    input.windows_per_monitor[0][0].outer_rect.reset();
    extract_managed_window_states_from_input_into(input, states);
    CHECK(states[0][0].min_track_height == 480);
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
    winapi::WindowMinMaxInfo minmax_info;
    minmax_info.min_track_width = 640;
    minmax_info.min_track_height = 480;
    input.windows_per_monitor[0].push_back({reinterpret_cast<winapi::HWND_T>(7), true, false, true,
                                            winapi::WindowPosition{10, 20, 300, 400}, minmax_info});

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
    CHECK(states[0][0].min_track_width == 640);
    CHECK(states[0][0].min_track_height == 480);
  }

  TEST_CASE("window minmax formatter includes retrieved tracking limits") {
    winapi::WindowMinMaxInfo info;
    info.max_width = 1920;
    info.max_height = 1080;
    info.max_x = 0;
    info.max_y = 24;
    info.min_track_width = 640;
    info.min_track_height = 480;
    info.max_track_width = 2560;
    info.max_track_height = 1440;

    CHECK(winapi::format_window_minmax_info(info) ==
          "max_size=1920x1080, max_position=(0,24), min_track=640x480, "
          "max_track=2560x1440");
  }

  TEST_CASE("window position formatter includes coordinates and size") {
    CHECK(winapi::format_window_position(winapi::WindowPosition{10, 20, 300, 400}) ==
          "x=10, y=20, w=300, h=400");
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

  TEST_CASE("monitor enumeration uses cached snapshots until invalidated") {
    std::vector<winapi::MonitorInfo> cached_monitors = {{reinterpret_cast<winapi::HMONITOR_T>(1),
                                                         "DISPLAY1",
                                                         {0, 0, 800, 600},
                                                         {0, 0, 800, 560},
                                                         true},
                                                        {reinterpret_cast<winapi::HMONITOR_T>(2),
                                                         "DISPLAY2",
                                                         {800, 0, 1600, 600},
                                                         {800, 0, 1600, 560},
                                                         false}};

    winapi::set_monitor_cache_for_test(cached_monitors);

    std::vector<winapi::MonitorInfo> monitors;
    winapi::fill_monitors(monitors);

    CHECK(monitors.size() == cached_monitors.size());
    CHECK(winapi::monitors_equal(monitors, cached_monitors));
    CHECK_FALSE(winapi::is_monitor_cache_dirty_for_test());

    winapi::invalidate_monitor_cache();

    CHECK(winapi::is_monitor_cache_dirty_for_test());
  }

  TEST_CASE("overlay render cache skips unchanged snapshots") {
    OverlayRenderCache cache;
    OverlayRenderSnapshot snapshot;
    snapshot.rects.push_back({1.0f, 2.0f, 300.0f, 400.0f, {255, 255, 255, 100}, 3.0f});

    CHECK(should_render_overlay(cache, snapshot));
    CHECK_FALSE(should_render_overlay(cache, snapshot));

    snapshot.message = "Split mode: vertical";
    snapshot.toast_font_size = 60.0f;
    CHECK(should_render_overlay(cache, snapshot));
    CHECK_FALSE(should_render_overlay(cache, snapshot));

    snapshot.message = std::nullopt;
    snapshot.toast_font_size = 0.0f;
    CHECK(should_render_overlay(cache, snapshot));
  }

  TEST_CASE("overlay clear is needed only after visible content was presented") {
    OverlayRenderCache cache;
    OverlayRenderSnapshot snapshot;
    snapshot.rects.push_back({1.0f, 2.0f, 300.0f, 400.0f, {255, 255, 255, 100}, 3.0f});

    CHECK_FALSE(should_clear_overlay(cache));
    CHECK(should_render_overlay(cache, snapshot));
    CHECK(should_clear_overlay(cache));
    CHECK_FALSE(should_clear_overlay(cache));
  }

  TEST_CASE("overlay render snapshot captures selected and normal cell colors") {
    ctrl::System system;
    ctrl::Cluster cluster;

    ctrl::CellData parent;
    ctrl::CellData first_child;
    first_child.leaf_id = 10;
    ctrl::CellData second_child;
    second_child.leaf_id = 20;

    int parent_index = cluster.tree.add_node(parent);
    int first_index = cluster.tree.add_node(first_child);
    int second_index = cluster.tree.add_node(second_child);
    cluster.tree.set_children(parent_index, first_index, second_index);
    system.clusters.push_back(cluster);
    system.selection = ctrl::CellIndicatorByIndex{0, first_index};

    std::vector<std::vector<ctrl::Rect>> geometries = {{{0.0f, 0.0f, 800.0f, 600.0f},
                                                        {10.0f, 20.0f, 300.0f, 400.0f},
                                                        {320.0f, 20.0f, 300.0f, 400.0f}}};

    renderer::RenderOptions options;
    options.normal_color = {1, 2, 3, 4};
    options.selected_color = {5, 6, 7, 8};
    options.border_width = 4.0f;

    auto snapshot = make_overlay_render_snapshot(system, geometries, options, std::nullopt, false);

    REQUIRE(snapshot.rects.size() == 2);
    CHECK(snapshot.rects[0].x == doctest::Approx(10.0f));
    CHECK(snapshot.rects[0].color.r == 5);
    CHECK(snapshot.rects[0].color.g == 6);
    CHECK(snapshot.rects[0].border_width == doctest::Approx(4.0f));
    CHECK(snapshot.rects[1].x == doctest::Approx(320.0f));
    CHECK(snapshot.rects[1].color.r == 1);
    CHECK(snapshot.rects[1].color.g == 2);
    CHECK_FALSE(snapshot.message.has_value());
  }

  TEST_CASE("active window rectangle follows foreground across monitors independently of selection") {
    ctrl::System system;
    ctrl::Cluster cluster;
    ctrl::CellData first;
    first.leaf_id = 10;
    ctrl::CellData second;
    second.leaf_id = 20;
    int parent = cluster.tree.add_node({});
    int first_index = cluster.tree.add_node(first);
    int second_index = cluster.tree.add_node(second);
    cluster.tree.set_children(parent, first_index, second_index);
    system.clusters.push_back(cluster);
    ctrl::Cluster other_monitor;
    ctrl::CellData third;
    third.leaf_id = 30;
    other_monitor.tree.add_node(third);
    system.clusters.push_back(other_monitor);
    system.selection = ctrl::CellIndicatorByIndex{0, first_index};
    system.focused_leaf_id = 10; // Last tiled focus must not override current foreground input.

    std::vector<std::vector<ctrl::Rect>> geometries = {
        {{0.0f, 0.0f, 800.0f, 600.0f}, {10.0f, 20.0f, 300.0f, 400.0f},
         {320.0f, 20.0f, 300.0f, 400.0f}},
        {{810.0f, 20.0f, 300.0f, 400.0f}}};
    renderer::RenderOptions options;
    options.show_only_active_window = true;
    options.selected_color = {5, 6, 7, 8};
    OverlayRenderCache cache;
    auto snapshot = [&](std::optional<size_t> active) {
      return make_overlay_render_snapshot(system, geometries, options, "Toast", false, active);
    };

    auto first_snapshot = snapshot(10);
    REQUIRE(first_snapshot.rects.size() == 1);
    CHECK(first_snapshot.rects[0].x == doctest::Approx(10.0f));
    CHECK(should_render_overlay(cache, first_snapshot));
    CHECK_FALSE(should_render_overlay(cache, first_snapshot));

    auto second_snapshot = snapshot(20);
    REQUIRE(second_snapshot.rects.size() == 1);
    CHECK(second_snapshot.rects[0].x == doctest::Approx(320.0f));
    CHECK(second_snapshot.rects[0].color.r == 5);
    CHECK(second_snapshot.rects[0].color.g == 6);
    CHECK(should_render_overlay(cache, second_snapshot));

    auto other_snapshot = snapshot(30);
    REQUIRE(other_snapshot.rects.size() == 1);
    CHECK(other_snapshot.rects[0].x == doctest::Approx(810.0f));
    CHECK(should_render_overlay(cache, other_snapshot));
    CHECK(snapshot(999).rects.empty());
    CHECK(snapshot(std::nullopt).rects.empty());
    CHECK(snapshot(std::nullopt).message == std::optional<std::string>("Toast"));
    CHECK(should_render_overlay(cache, snapshot(std::nullopt)));

    options.show_only_active_window = false;
    CHECK(snapshot(10).rects.size() == 3);
    CHECK(should_render_overlay(cache, snapshot(10)));
  }

  TEST_CASE("pointer focus updates the active rectangle in the same frame") {
    Engine engine;
    engine.init({{0, 0, 800, 600, 0, 0, 800, 600, {10, 20}}});
    EngineFrameInput input;
    input.cluster_updates = {{{10, 20}, false}};
    input.has_completed_initial_tile_pass = true;
    input.foreground_leaf_id = 10;
    input.pointer_window_id = 20;
    input.cursor_pos = ctrl::Point{600, 300};
    const auto output = engine.process_frame(input);
    REQUIRE(output.focus_leaf_id == 20);

    renderer::RenderOptions options;
    options.show_only_active_window = true;
    OverlayRenderCache cache;
    CHECK(should_render_overlay(cache, make_overlay_render_snapshot(
        engine.system, output.geometries, options, std::nullopt, false,
        input.foreground_leaf_id)));

    winapi::HWND_T foreground = reinterpret_cast<winapi::HWND_T>(10);
    bool focus_applied = false;
    bool focus_succeeds = true;
    bool focus_disappears = false;
    SUBCASE("successful focus redraws immediately") {}
    SUBCASE("failed focus keeps the actual foreground border") { focus_succeeds = false; }
    SUBCASE("no foreground clears the previous border") { focus_disappears = true; }

    const auto active_leaf_id = apply_focus_and_read_foreground(
        output.focus_leaf_id,
        [&](winapi::HWND_T target) {
          CHECK(target == reinterpret_cast<winapi::HWND_T>(20));
          focus_applied = true;
          if (focus_succeeds) {
            foreground = focus_disappears ? nullptr : target;
          }
          return focus_succeeds;
        },
        [&]() {
          CHECK(focus_applied); // The read must happen after the focus request.
          return foreground;
        });
    const auto snapshot = make_overlay_render_snapshot(
        engine.system, output.geometries, options, std::nullopt, false, active_leaf_id);
    if (focus_disappears) {
      CHECK_FALSE(active_leaf_id.has_value());
      CHECK(snapshot.rects.empty());
    } else {
      REQUIRE(snapshot.rects.size() == 1);
      const size_t expected_leaf = focus_succeeds ? 20 : 10;
      CHECK(active_leaf_id == expected_leaf);
      const auto cell = engine.find_leaf(expected_leaf);
      REQUIRE(cell.has_value());
      CHECK(snapshot.rects[0].x ==
            doctest::Approx(output.geometries[0][cell->cell_index].x));
    }
    CHECK(should_render_overlay(cache, snapshot) == focus_succeeds);
    CHECK_FALSE(should_render_overlay(cache, snapshot));
    CHECK(input.foreground_leaf_id == 10); // No next input poll was needed.
  }

  TEST_CASE("frames without a focus request still read the current foreground") {
    const auto active_leaf_id = apply_focus_and_read_foreground(
        std::nullopt,
        [](winapi::HWND_T) {
          FAIL("Unexpected foreground change");
          return false;
        },
        []() { return reinterpret_cast<winapi::HWND_T>(30); });
    CHECK(active_leaf_id == 30);
  }

  TEST_CASE("active window rectangles respect zen visibility and suppression") {
    ctrl::System system;
    ctrl::Cluster cluster;
    ctrl::CellData first;
    first.leaf_id = 10;
    ctrl::CellData second;
    second.leaf_id = 20;
    int parent = cluster.tree.add_node({});
    int first_index = cluster.tree.add_node(first);
    int second_index = cluster.tree.add_node(second);
    cluster.tree.set_children(parent, first_index, second_index);
    system.clusters.push_back(cluster);
    // A second zen monitor must not receive an inactive outline.
    ctrl::Cluster other_monitor;
    ctrl::CellData other;
    other.leaf_id = 30;
    other_monitor.zen_cell_index = other_monitor.tree.add_node(other);
    system.clusters.push_back(other_monitor);
    std::vector<std::vector<ctrl::Rect>> geometries = {
        {{}, {10.0f, 20.0f, 300.0f, 400.0f}, {320.0f, 20.0f, 300.0f, 400.0f}},
        {{810.0f, 20.0f, 300.0f, 400.0f}}};
    renderer::RenderOptions options;
    options.show_only_active_window = true;
    SUBCASE("normal") {}
    SUBCASE("zen") {
      system.clusters[0].zen_cell_index = first_index;
      CHECK(make_overlay_render_snapshot(system, geometries, options, std::nullopt, false, 20)
                .rects.empty());
    }
    auto snapshot = [&](bool suppressed = false) {
      return make_overlay_render_snapshot(system, geometries, options, "Toast", suppressed, 10);
    };
    REQUIRE(snapshot().rects.size() == 1);
    CHECK(snapshot().rects[0].x == doctest::Approx(10.0f));
    CHECK(snapshot(true).rects.empty());
    CHECK(snapshot(true).message == std::optional<std::string>("Toast"));
    options.show_rectangles = false;
    CHECK(snapshot().rects.empty());
    options.show_rectangles = true;
    options.border_width = 0.0f;
    CHECK(snapshot().rects.empty());
    options.border_width = 3.0f;
    system.clusters[0].has_fullscreen_cell = true;
    CHECK(snapshot().rects.empty());
  }

  TEST_CASE("overlay render snapshot suppresses rectangles while preserving toast") {
    ctrl::System system;
    ctrl::Cluster cluster;

    ctrl::CellData cell;
    cell.leaf_id = 10;
    cluster.tree.add_node(cell);
    system.clusters.push_back(cluster);

    std::vector<std::vector<ctrl::Rect>> geometries = {{{10.0f, 20.0f, 300.0f, 400.0f}}};

    renderer::RenderOptions options;
    options.toast_font_size = 32.0f;

    auto snapshot = make_overlay_render_snapshot(system, geometries, options,
                                                 std::optional<std::string>("Paused"), true);

    CHECK(snapshot.rects.empty());
    REQUIRE(snapshot.message.has_value());
    CHECK(*snapshot.message == "Paused");
    CHECK(snapshot.toast_font_size == doctest::Approx(32.0f));
  }

  TEST_CASE("rectangle configuration changes clear and restore overlays while preserving toast") {
    ctrl::System system;
    ctrl::Cluster cluster;
    ctrl::CellData cell;
    cell.leaf_id = 10;
    int cell_index = cluster.tree.add_node(cell);
    system.clusters.push_back(cluster);
    system.selection = ctrl::CellIndicatorByIndex{0, cell_index};

    SUBCASE("normal cells") {}
    SUBCASE("zen cells") { system.clusters[0].zen_cell_index = cell_index; }

    std::vector<std::vector<ctrl::Rect>> geometries = {{{10.0f, 20.0f, 300.0f, 400.0f}}};
    renderer::RenderOptions options;
    options.toast_font_size = 32.0f;
    OverlayRenderCache cache;
    auto snapshot = [&]() {
      return make_overlay_render_snapshot(system, geometries, options,
                                          std::optional<std::string>("Paused"), false);
    };

    REQUIRE(snapshot().rects.size() == 1);
    CHECK(should_render_overlay(cache, snapshot()));

    options.show_rectangles = false;
    CHECK(snapshot().rects.empty());
    CHECK(snapshot().message == std::optional<std::string>("Paused"));
    CHECK(snapshot().toast_font_size == doctest::Approx(32.0f));
    CHECK(should_render_overlay(cache, snapshot()));
    CHECK_FALSE(should_render_overlay(cache, snapshot()));

    options.show_rectangles = true;
    REQUIRE(snapshot().rects.size() == 1);
    CHECK(should_render_overlay(cache, snapshot()));

    options.border_width = 0.0f;
    CHECK(snapshot().rects.empty());
    CHECK(snapshot().message == std::optional<std::string>("Paused"));
    CHECK(snapshot().toast_font_size == doctest::Approx(32.0f));
    CHECK(should_render_overlay(cache, snapshot()));

    options.border_width = 3.0f;
    REQUIRE(snapshot().rects.size() == 1);
    CHECK(should_render_overlay(cache, snapshot()));
    CHECK(make_overlay_render_snapshot(system, geometries, options, std::nullopt, true)
              .rects.empty());
  }

  TEST_CASE("runtime verbose logging toggles between trace and configured level") {
    auto original_level = spdlog::get_level();
    spdlog::set_level(spdlog::level::warn);
    RuntimeLoggingState logging_state{spdlog::level::warn, false};

    toggle_runtime_verbose_logging(logging_state);

    CHECK(logging_state.verbose_logging_enabled == true);
    CHECK(spdlog::get_level() == spdlog::level::trace);

    toggle_runtime_verbose_logging(logging_state);

    CHECK(logging_state.verbose_logging_enabled == false);
    CHECK(spdlog::get_level() == spdlog::level::warn);
    spdlog::set_level(original_level);
  }
}

} // namespace wintiler

#endif // !DOCTEST_CONFIG_DISABLE
