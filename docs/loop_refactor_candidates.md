The closest match to your target flow is the normal window-topology sync path in src/loop.cpp:

Gather input: winapi::gather_loop_input_state(...) at src/loop.cpp.
Convert input for engine: extract_window_state_from_input(input_state) plus redirect_cluster derivation at src/loop.cpp.
Send to engine: engine.update(current_state, redirect_cluster) at src/loop.cpp.
Get output: bool changed from Engine::update (src/engine.cpp).
Apply: recompute geometry, maybe move cursor, then apply_tile_positions(...) at src/loop.cpp and src/loop.cpp.
The hotkey path is also close, but only partially:

Input: check_keyboard_action() at src/loop.cpp.
Engine: engine.process_action(...) at src/loop.cpp and src/engine.cpp.
Output: ActionResult from src/engine.h.
Apply: cursor/focus changes and later tile apply in src/loop.cpp and src/loop.cpp.
Where the loop does not follow that flow cleanly:

apply_zen_to_maximized_windows(...) bypasses Engine and mutates ctrl::System directly at src/loop.cpp and src/loop.cpp.

update_selection_from_hover(...) also bypasses Engine and writes engine.system.selection directly at src/loop.cpp.

The loop inspects engine.system to decide what to apply after hotkeys, especially focus selection at src/loop.cpp. That means ActionResult is too weak; it does not carry “focus this window”, “show this toast”, “exit”, “pause”, or “apply tiles”.

Drag end handling is split across loop helpers and engine. handle_window_resize(...) and handle_mouse_drop_move(...) do input inspection, direct winapi calls, and only part of the decision goes through engine at src/loop.cpp and src/loop.cpp.

Input gathering is not unified. Hotkeys are read separately from gather_loop_input_state(...), and pause/unpause is handled before the main gather path at src/loop.cpp.

Apply is unconditional. apply_tile_positions(...) runs every iteration at src/loop.cpp, not because engine explicitly said “these positions need applying”.

perform_drop_move currently returns a cursor position based on stale/pre-recompute geometry, called out by its own comment at src/controller.cpp. That is another sign the output/apply boundary is blurry.

The short version is: update() is the cleanest existing example of your desired pipeline. process_action() is close but still leaks engine internals into the loop. Hover selection, auto-zen, focus selection, pause/exit, and drag/resize handling are the main places where the loop still mixes gather, decide, and apply.

If you want, I can turn this into a concrete refactor plan around a single LoopInput -> EngineOutput -> apply contract.
