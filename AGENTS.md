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
- Run pre-commit hooks, all hooks should pass
```
pre-commit run --all-files         # Run all pre-commit hooks
```

# Coding Rules

 - Do not use (void), log error for handling return statements from [[nodiscard]] functions.
 - Do not use private members, all members should be public.

# General Rules

- Do not create a file called `nul`. We target windows and file named `nul` cannot be deleted later.
