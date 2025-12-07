# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a Windows C++ application called "win-tiler" built with Visual Studio 2019 (v145 toolset) using C++20. The project uses raylib for graphics rendering and vcpkg for dependency management.

## Architecture

- **Main application**: `win-tiler.cpp` - Simple raylib-based window application
- **Project type**: Console application (Windows-only)
- **Dependencies**: Managed through vcpkg with raylib as the primary graphics library
- **Build system**: MSBuild via Visual Studio project files

## Development Commands

### Building the Project

Use the provided batch script for building:

```bash
# Build debug version
build-run.bat build --debug

# Build release version
build-run.bat build --release

# Build and run debug version
build-run.bat build-run --debug

# Build and run release version
build-run.bat build-run --release
```

### Manual MSBuild (alternative)

If you need to build manually without the batch script:

```bash
# Find MSBuild via vswhere and build
msbuild win-tiler.slnx /p:Configuration=Debug /m
msbuild win-tiler.slnx /p:Configuration=Release /m
```

### Running the Application

After building, executables are located in:
- Debug: `x64\Debug\win-tiler.exe`
- Release: `x64\Release\win-tiler.exe`

## Dependencies and Package Management

- **vcpkg**: Used for C++ package management
- **vcpkg.json**: Defines raylib as the primary dependency
- **vcpkg-configuration.json**: Configures Microsoft's vcpkg registry
- Dependencies are automatically restored during build via vcpkg manifest mode

## Visual Studio Configuration

- **Platform**: x64 and x86 support (primary target: x64)
- **Toolset**: v145 (Visual Studio 2019)
- **Language Standard**: C++20
- **Character Set**: Unicode
- **vcpkg integration**: Enabled via manifest mode

## Project Structure

- `win-tiler.cpp` - Main application source
- `win-tiler.vcxproj` - Visual Studio project file
- `win-tiler.slnx` - Solution file
- `vcpkg.json` - Package manifest
- `vcpkg-configuration.json` - vcpkg registry configuration
- `build-run.bat` - Build and run automation script
- `vcpkg_installed/` - vcpkg package installation directory

## Notes

- No testing framework is currently configured
- No linting tools are set up - code follows Visual Studio default C++ formatting
- The project is Windows-specific and requires Visual Studio build tools
- Dependencies are automatically managed through vcpkg manifest mode