# Task compilation requirements

- If the task changes behavior add a unit test.
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

# Dependencies

- **raylib** - Graphics/UI visualization
- **doctest** - Unit testing framework
- **spdlog** - Structured logging
- **tomlplusplus** - TOML configuration parsing
- **magic-enum** - Enum reflection
- **tl-expected** - Expected/Result type
- **Windows API** - Dwmapi.lib, Psapi.lib for window management

# General Rules

- Do not create a file called `nul`. We target windows and file named `nul` cannot be deleted later.
