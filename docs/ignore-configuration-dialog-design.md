# Ignore configuration dialog design notes

## Goal

Add a notification-area menu item that opens a dialog for reviewing windows managed by win-tiler and windows ignored by user-facing ignore configuration. From that dialog, users should be able to add or remove persistent ignore rules without manually editing the TOML file for common cases.

## Relevant current code

- `src/options.h` defines `IgnoreOptions`.
- `src/options.cpp` parses and writes `[ignore]` TOML configuration and generated config comments.
- `src/winapi.cpp` owns notification-area menu construction and command handling.
- `src/winapi.cpp` already has private `WindowManagementState` inspection used by `dump_window_management_state`.
- `src/loop.cpp` consumes ignore options during WinAPI input gathering and already supports session-only floating through `ToggleFloating`.
- `src/save_layout.cpp` has a config-preserving TOML update pattern that is a better model for dialog edits than rewriting the whole config with `write_options_toml`.

## Recommended implementation shape

Add a tray item named something like `Manage ignored windows...`.

Keep the main list scoped to:

- currently managed windows
- windows ignored by user-facing ignore configuration

Do not show every rejected system/tool/cloaked/no-title window by default. Those windows are useful for diagnostics, but they are noisy and usually not actionable. An advanced diagnostic toggle could show them later.

Expose a public inspection API from `winapi` instead of duplicating matching logic:

- promote the useful parts of private `WindowManagementState` into a public snapshot type, for example `WindowManagementSnapshot`
- expose `gather_window_management_snapshots(const IgnoreOptions&)`
- keep `dump_window_management_state` backed by the same snapshot path

Add a modeless Win32 dialog or window with a `SysListView32` table. Suggested columns:

- status: managed, ignored, rejected
- process
- title
- class
- monitor
- size
- ignore reason
- rule source: user config, built-in default, runtime/system filter

Actions should operate on selected rows:

- ignore process
- ignore exact title
- ignore process and title
- ignore child/owned windows for this process
- remove matching user ignore rule
- refresh
- copy details
- open config file

If a persistent config file is not active, persistent edit actions should be disabled or the dialog should offer to create/open the default config first.

## Config options suitable for user control

Safe and useful from the dialog:

- `[ignore].processes`
  - Best default action when the user wants to ignore all windows from an app.
  - Risk: broad; can hide windows from apps that host multiple unrelated UI surfaces.

- `[ignore].window_titles`
  - Useful for stable one-off popups.
  - Risk: titles are often duplicated across apps or change with document names.

- `[ignore].process_title_pairs`
  - Best default persistent action for a single current window.
  - Safer than title-only and less broad than process-only.

- `[ignore].ignore_children_of_processes`
  - Useful for apps that spawn transient owned windows that should never tile.
  - Should be labeled clearly because it can hide dialogs users may care about.

- `[ignore].small_window_barrier`
  - Useful as a global setting, not a per-window toggle.
  - The dialog can show when a window is ignored because of the barrier and allow global width/height edit or enable/disable.

Merge flags need careful handling:

- `merge_processes_with_defaults`
- `merge_window_titles_with_defaults`
- `merge_process_title_pairs_with_defaults`
- `merge_ignore_children_of_processes_with_defaults`

When a rule comes from built-in defaults, the dialog should not pretend it can remove that single rule from user config. Either disable removal for built-in rules or expose an advanced action that turns off the relevant merge flag and writes the full desired user list explicitly.

## Persistence approach

Avoid using `write_options_toml()` for single dialog edits because it rewrites the whole config file with generated comments and serialized current options.

Prefer a config-preserving helper similar to `save_monitor_layout_rules_to_config`:

1. Read the existing config text.
2. Parse it with toml++ to validate it before editing.
3. Update or create only the `[ignore]` section and the relevant key.
4. Validate the updated TOML.
5. Write atomically.
6. Reload options immediately and request layout reapply/retile.

This helper should have focused unit tests for adding, removing, duplicate handling, creating a missing `[ignore]` section, preserving unrelated sections, and rejecting invalid TOML without modifying the file.

## Loop and architecture notes

Keep tiling decisions out of `src/loop.cpp`.

Recommended flow:

- tray menu command opens the dialog on the notification window thread
- dialog writes persistent ignore config through a config helper
- dialog or tray code records a pending "ignore config changed" request
- loop consumes the request, reloads `GlobalOptionsProvider`, marks desktops for layout reapply, and shows a toast

For session-only actions, keep them separate from persistent config edits. Existing `ToggleFloating` behavior already acts as a temporary per-window ignore. The dialog can expose a clearly labeled temporary action such as `Float for this session`, but it should not be mixed with persistent ignore rules.

## Corner cases

- No config path is active.
- Config file is missing, read-only, locked, invalid TOML, or externally changed while the dialog is open.
- A selected window closes before the user applies an action.
- HWND values can be reused, so actions should be based on current process/title data and should revalidate the selected HWND if needed.
- Built-in default ignore rules cannot be removed from user config while merge flags stay enabled.
- Process matching is currently not fully consistent: process ignore matching is case-sensitive, while process/title pair matching is case-insensitive.
- Titles can be empty, localized, duplicated, or frequently changing.
- Some apps use host processes or launcher processes, especially UWP/WebView/packaged apps.
- Child/owned-window ignore rules can accidentally hide important modal dialogs.
- Small-window barrier is global and can catch legitimate small utilities.
- Topmost, tool window, no-activate, transparent, cloaked, hung, and owned `#32770` dialog filters are runtime/system filters, not user config rules.
- Virtual desktop state may be unavailable if all visible windows are ignored.
- Retiling after an ignore change can remove a window from the engine; selection and stored-cell references must tolerate that.
- Dialog refresh should not block on hung windows; reuse existing timeout-safe metadata paths where possible.

## Suggested first increment

1. Expose a public window-management snapshot API backed by the existing dump inspection logic.
2. Add a tray item and a read-only dialog/list with refresh and copy details.
3. Add config-preserving helpers for user ignore rules with tests.
4. Enable add/remove actions for `process_title_pairs` first.
5. Add broader actions for process, title, child windows, and small-window barrier after the persistence path is proven.
