# Task compilation requirements

- If the task changes behavior add a unit test.
- Run pre-commit hooks, all hooks should pass
```
pre-commit run --all-files         # Run all pre-commit hooks
```
- Compile and run unit tests, all tests should pass. Compilation should be successful and not have warnings
```
build-run.bat build-run --Test-Debug  # Build and run unit tests
```
- Compile the application itself, compilation should be successful and not have warnings
```
build-run.bat build --Debug           # Build debug version
```

# Coding Rules

 - Do not use (void), log error for handling return statements from [[nodiscard]] functions.
 - Do not use private members, all members should be public.

# Architecture Preference

- Prefer the `EngineFrameInput -> Engine::process_frame(...) -> EngineFrameOutput` flow for new loop-related work.
- Keep `src/loop.cpp` focused on gathering input, calling the engine, applying returned effects, and rendering.
- Put tiling and state-transition decisions in `src/engine.*`, not in the loop.
- Do not introduce new direct mutations of `engine.system` or new `ctrl::*` mutator calls in `src/loop.cpp`.
- When adding loop-side effects, prefer extending the explicit frame output/apply path instead of adding inline decision logic in the loop.

# General Rules

- Do not create a file called `nul`. We target windows and file named `nul` cannot be deleted later.
