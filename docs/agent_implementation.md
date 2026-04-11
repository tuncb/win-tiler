# decisions

- Step 1: Added a separate `agent` CLI command instead of extending `loop` mode.
- Step 1: `agent` defaults to `stdio` transport when no transport argument is provided, but still accepts explicit `agent stdio`.
- Step 1: Added a stub `run_agent_mode(...)` entrypoint that logs the mode is not implemented yet so CLI plumbing can land and be validated independently before runtime work begins.
- Step 2: Extracted loop-independent runtime helpers into `runtime_support.*` instead of keeping them as anonymous-namespace functions in `loop.cpp`, so later agent mode code can reuse them without touching frame I/O structs.
- Step 3: Added `nlohmann-json` via vcpkg for protocol parsing and serialization rather than implementing a custom JSON parser.
- Step 3: Standardized protocol window identifiers as zero-padded uppercase hex strings in the form `hwnd:0000000000000000`.
- Step 4: Exposed a narrow public `Engine` API for agent work (`find_leaf`, `selected_leaf_id`, `select_leaf`, `swap_leaves`, `move_leaf_to_cell`) instead of reintroducing a separate controller layer.
- Step 5: Agent stdio mode runs as a persistent line-oriented JSON service and uses a fallback desktop ID `__agent_default__` when Windows does not report a desktop ID.
- Step 5: Malformed requests that cannot be correlated to an input request ID return an error response with an empty `id` field.
- Step 6: `list_windows` and `get_state` currently enumerate the same filtered window set produced by `gather_loop_input_state`, so `managed_only = false` does not expose additional ignored/system windows yet.
- Step 6: The protocol `rect` field uses computed tile geometry when layout is requested and the window is managed; otherwise it falls back to the live OS window rect.
- Step 7: `move_window_to_monitor` requires either an anchor window on the target monitor or at least one existing managed window there; moving directly into an empty monitor currently returns a structured error.
- Step 7: `send_action` rejects `Exit` and `TogglePause` style control-flow outcomes in agent mode instead of shutting down or pausing the service loop.
- Step 8: Added negative-path unit coverage for agent protocol validation and for the new `Engine` helper methods used by agent mode.
- Step 9: Documented `agent stdio` in the README, including the current command set, window ID format, and a minimal JSON-lines session example.
- Follow-up docs: Added reusable `.jsonl` session files under `docs/` with placeholder window IDs instead of machine-specific live IDs.
- Follow-up docs: Added `docs/Invoke-WinTilerAgent.ps1` as a small stdio wrapper for running either a session file or one-off JSON requests against `win-tiler agent stdio`.
