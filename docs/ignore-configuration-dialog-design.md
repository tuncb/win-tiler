# Ignore configuration dialog design notes

## Native redesign — October 2026

The interactive design is in [prototypes/ignored-windows.html](prototypes/ignored-windows.html).
Open it directly in a browser; it has no dependencies or server requirement. All windows,
rules, file operations, and tiling changes are simulated in memory. The preview controls
switch dark/light appearance and demonstrate missing-config and failed-save states.
The approved item-menu design is implemented natively in `src/ignore_dialog.cpp`.
The HTML remains a standalone design reference.

### Native implementation

- Searchable **Open windows** and **Saved rules** lists, persistent row menu affordances,
  state assignment, literal value editing, details, immediate saves, and one-step Undo.
  Row menus contain only item actions; Open config file belongs to the dialog footer and tray.
  Native HMENU submenus choose the affected rule when several overlap. Built-in entries
  remain read-only; counts refer to eligible listed windows, before the search filter.
- Literal editing uses a compact native modal editor with only the fields required by
  that rule. The main dialog keeps the item-menu workflow and has no separate editor pane.
- The icon supplies the current palette: navy (`#0B1726`) and cyan (`#36C5F0`) in dark
  mode, warm ivory (`#FBFAF6`) and amber (`#FFB020`) in light mode. Amber labels use
  `#8A5500` for contrast. Primary buttons use the brighter accent with a dark label.
  The tray uses quieter navy/white surfaces and the corresponding selection tint.
- `src/ui_theme.cpp` reads `AppsUseLightTheme` for the dialog and its menus, and
  `SystemUsesLightTheme` for the tray menu. High contrast uses system colors. Open dialogs
  handle theme and DPI changes; tray colors are read on every opening. Native owner drawing
  preserves HMENU navigation and exposes menu labels through `MSAAMENUINFO`.
- The list retains native accessibility strings. Tab follows visual control order;
  Enter/Space or Shift+F10 opens row actions, Ctrl+F focuses search, F5 refreshes, and Escape
  dismisses the current menu/editor/dialog. The native minimum size is 680 × 530 logical pixels.
- Rule replacements use one validated, atomic config write. Undo applies only the inverse
  rule delta to the current config, preserving unrelated external edits. The first rule automatically
  creates a missing config by publishing a complete temporary file without replacing an existing
  destination; the new path is adopted for subsequent live reloads. Explicit Create config uses
  `CREATE_NEW`. Invalid/unsupported TOML
  rule shapes, stale rows, locked files, and replaced rules produce errors without a partial
  edit. Literal titles and executable names are read through Unicode Win32 APIs.
- Rule changes use the existing ignore-config notification and reload/retile path. No tiling
  decisions or new engine mutations are added to the message pump.

### Native verification

The Test-Debug suite covers transactions, conflicts, duplicates, Undo, Unicode and long
window titles, protected rule sources, owned-window semantics, new-config adoption,
separate app/system theme selection, high contrast, readable text contrast in both icon palettes,
and native menu letter navigation.
The final suite passes 404 test cases and 3546 assertions. Both required builds complete
without compiler warnings.

An isolated application harness also exercises the actual dialog controls and config
commands with a temporary file, without starting the tiling engine. Actual Win32 captures
were inspected for light/dark dialog and item menus, both tray themes, saved rules, the value
editor, minimum size, empty search, failed-save/Retry, and missing-config states. Process-local
registry redirection supplies theme fixtures; the user's Windows theme is unchanged.
The computer-use helper was unavailable, so this pass does not claim a manual desktop
mouse/keyboard session or screen-reader verification.

### Main workflow — revision 2: actions on each item

1. Find an item in **Open windows**. Each row shows its effective behavior (Tiled or
   Ignored), the matching scope, and a persistent three-line menu button on the right.
   Search and All/Tiled/Ignored filters remain available.
2. Open that item's menu. Show the current state with a checkmark and offer **Tile
   normally**, **Ignore app + exact title**, **Ignore the entire app**, **Ignore this
   title everywhere**, and **Ignore owned windows**. Each rule choice shows its current
   matching-window count. There is no separate editor pane or Save-rule button.
3. Selecting an ignore state creates a rule immediately when none exists, or replaces
   the selected rule when one exists. Selecting Tile normally removes that rule. Close
   the menu, update every affected row, restore focus to the item button, and offer
   **Undo** in the footer. The native persistence path must replace a rule atomically.
4. An app-wide rule can affect several rows. Show the removal count and explain that
   edits apply to all matches. Never model the effective state of each window as an
   independent persistent toggle: a narrower replacement can allow other windows to tile.
5. When several rules apply, show a **Rule to change** selector and name the removal
   action **Remove this rule**. Preserve all other rules. Built-in rules remain locked.
   Owned-window rules remain accessible from their app's main window even when that
   main window is tiled; the row says **Owned windows ignored**.

**Saved rules** keeps rules accessible when their app is closed. Its row menus offer
the same state changes, **Remove rule**, and **Edit matching values…**. The prototype edits values inside the item popover; the native version uses a compact modal
editor with an explicit Save values action. A state requiring
an unknown process or title is disabled with an explanation; values can be filled in
before switching. The demo retains known optional values between conversions, while
the exported config includes only values used by the selected scope. These extra values
are editing context, not new configuration options. New rules begin at a window's menu.

The first menu is open initially to make the interaction discoverable in the design
preview. The native dialog should start with its menus closed.

### Layout and visual language

- Use a single full-width list, with title and process on the left and effective state,
  matching scope, and item menu on the right. Technical metadata and Copy details live
  in the menu's Window details disclosure.
- Place Refresh next to search. Keep the config-file link, change feedback, and Undo
  at the bottom left, with Done at the bottom right. No global create/edit action is
  needed for the window-driven workflow.
- Draw visual cues from the local T3 Code source: `T3_CHAT_THEME` in
  `packages/shared/src/themePalettes.ts`, semantic surface layering and 8px control
  corners in `apps/web/src/index.css`, and the compact control hierarchy in
  `apps/web/src/components/ui/button.tsx`. The prototype retains that layout language with the selected icon-based dark cyan and
  light amber palettes; it does not depend on T3 or copy its React components.
- Use text labels with status colors, checked menu items, visible keyboard focus,
  and status announcements. Support arrow-key menu navigation, Enter, and Escape with
  focus returned to the trigger. Keep item actions available without hovering. At narrow
  preview widths, expand the menu directly below its row. Wider menus open above or
  below as space allows. Menus must stay reachable without covering the footer.

### Persistence and edge cases

- Missing config: keep rule assignments enabled when the target path is known. The first
  assignment creates the complete config and starts live monitoring. Create config is also
  available as an explicit action. Invalid existing configs remain protected until corrected.
- Write failure: retain the previous rules and window states; keep the attempted values
  visible and offer Retry. Success feedback appears only after a successful write/reload.
- Duplicate: explain that the rule already exists without adding another entry.
- Built-in rules: never suggest that removing a user entry overrides a default.
- Exact title is a matching value, not a window identity. Process and app/title pair
  matching are case-insensitive; title-only matching is case-sensitive, following the
  current application. Owned-window matching applies to the window's own process when
  it has a parent or owner. Do not reinterpret it as matching the owner's executable.
- Native implementation must revalidate window identity and current config before
  writes, handle external config changes, and implement Undo as an inverse edit against
  current config. The demo's in-memory snapshot restore is not a persistence strategy.
- The sample covers list rules and one real built-in pair (`SystemSettings.exe`,
  `Settings`). A native inspector must also explain global small-window-barrier matches
  and runtime filters without presenting them as removable per-window rules.

### Verification of this concept

The prototype was visually reviewed through T3 HTML preview and isolated Microsoft Edge.
Interaction checks cover immediate creation, state replacement, removal, undo, broad-rule
impact, owned-window rules on the main window, editing values, overlapping-rule targeting,
duplicate merging, built-in restrictions, search/filter empty states, missing config,
failed writes, keyboard interaction, and closing/reopening. First- and last-row menus
were checked at six widths from 320px to 1000px in both themes (24 layout checks), with
no horizontal overflow or footer overlap. The native unit-test and Debug builds
also pass with zero compiler warnings: 380 tests, 3288 assertions.

The sections below are historical notes from the original dialog. The native redesign above
supersedes their proposed Add/Remove button layout.

## Goal

Add a notification-area menu item that opens a dialog for reviewing windows managed by win-tiler and windows ignored by user-facing ignore configuration. From that dialog, users should be able to add or remove persistent ignore rules without manually editing the TOML file for common cases.

## Relevant current code

- `src/options.h` defines `IgnoreOptions`.
- `src/options.cpp` parses and writes `[ignore]` TOML configuration and generated config comments.
- `src/winapi.cpp` owns notification-area menu construction and command handling.
- `src/winapi.cpp` already has private `WindowManagementState` inspection used by `dump_window_management_state`.
- `src/loop.cpp` consumes ignore options during WinAPI input gathering and already supports session-only floating through `ToggleFloating`.
- `src/ignore_config.cpp` updates ignore rules while preserving unrelated TOML configuration.

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

Use the config-preserving helpers in `src/ignore_config.cpp`:

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
- Process and process/title pair matching are case-insensitive; title-only matching is case-sensitive.
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
