# Agent Session Examples

These JSON-lines files are intended for `win-tiler agent stdio`.

- [agent_session_list_and_state.jsonl](</C:/work/win-tiler/win-tiler/docs/agent_session_list_and_state.jsonl>) captures the current managed window list and layout state.
- [agent_session_focus_and_zen.jsonl](</C:/work/win-tiler/win-tiler/docs/agent_session_focus_and_zen.jsonl>) focuses one managed window, toggles zen on, inspects state, then toggles zen off.
- [agent_session_retile_swap_move.jsonl](</C:/work/win-tiler/win-tiler/docs/agent_session_retile_swap_move.jsonl>) retiles, swaps two managed windows, moves one window to another monitor, then captures state.

Replace placeholder values such as `hwnd:REPLACE_WITH_WINDOW_ID` with real window IDs from `list_windows` or `get_state`.

`move_window_to_monitor` can target an empty monitor without an anchor. When the target monitor already contains managed windows, supply `anchor_window_id` if you want to control where the moved window is inserted.

Example usage:

```powershell
Get-Content .\docs\agent_session_list_and_state.jsonl | .\x64\Debug\win-tiler.exe agent stdio
```
