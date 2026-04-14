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

  TEST_CASE("frames without a desktop id do nothing when there is no queued hotkey") {
    CHECK(classify_no_desktop_hotkey(std::nullopt) == NoDesktopHotkeyAction::None);
  }
}

} // namespace wintiler

#endif // !DOCTEST_CONFIG_DISABLE
