#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include "loop.h"

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
}

} // namespace wintiler

#endif // !DOCTEST_CONFIG_DISABLE
