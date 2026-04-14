#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include "loop.h"

namespace wintiler {

TEST_SUITE("loop") {
  TEST_CASE("skipped frames keep exit hotkeys immediate") {
    CHECK(classify_skipped_frame_hotkey(HotkeyAction::Exit) == SkippedFrameHotkeyAction::Exit);
  }

  TEST_CASE("skipped frames keep pause hotkeys immediate") {
    CHECK(classify_skipped_frame_hotkey(HotkeyAction::TogglePause) ==
          SkippedFrameHotkeyAction::EnterManualPause);
  }

  TEST_CASE("skipped frames discard non control hotkeys instead of deferring them") {
    CHECK(classify_skipped_frame_hotkey(HotkeyAction::NavigateLeft) ==
          SkippedFrameHotkeyAction::Ignore);
  }

  TEST_CASE("skipped frames do nothing when there is no queued hotkey") {
    CHECK(classify_skipped_frame_hotkey(std::nullopt) == SkippedFrameHotkeyAction::None);
  }
}

} // namespace wintiler

#endif // !DOCTEST_CONFIG_DISABLE
