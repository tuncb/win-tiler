@echo off
setlocal ENABLEDELAYEDEXPANSION

REM Default config
set CONFIG=Debug
set ACTION=

REM Parse arguments
:parse_args
if "%~1"=="" goto after_parse

if /I "%~1"=="build" (
    set ACTION=build
) else if /I "%~1"=="build-run" (
    set ACTION=build-run
) else if /I "%~1"=="--debug" (
    set CONFIG=Debug
) else if /I "%~1"=="--release" (
    set CONFIG=Release
) else (
    echo Unknown argument: %~1
    echo Usage: %~nx0 [build^|build-run] [--debug^|--release]
    exit /b 1
)
shift
goto parse_args

:after_parse
if "%ACTION%"=="" (
    echo You must specify "build" or "build-run".
    echo Usage: %~nx0 [build^|build-run] [--debug^|--release]
    exit /b 1
)

echo Action: %ACTION%
echo Configuration: %CONFIG%
echo.

REM Make sure vswhere exists (typical VS install path)
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    echo vswhere.exe not found. Make sure Visual Studio is installed.
    exit /b 1
)

REM Find latest VS with MSBuild and get MSBuild path
for /f "usebackq tokens=* delims=" %%i in (`%VSWHERE% -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set MSBUILD=%%i
)

if "%MSBUILD%"=="" (
    echo MSBuild not found via vswhere.
    exit /b 1
)

echo Using MSBuild: %MSBUILD%
echo.

REM Build the solution
"%MSBUILD%" "win-tiler.slnx" /p:Configuration=%CONFIG% /m
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo Build succeeded.

if /I "%ACTION%"=="build" (
    exit /b 0
)

REM Determine output EXE path (adjust if your project path/config differs)
set OUTDIR=
if /I "%CONFIG%"=="Debug" (
    set OUTDIR=x64\Debug
) else (
    set OUTDIR=x64\Release
)

set EXE_PATH=%OUTDIR%\win-tiler.exe

if not exist "%EXE_PATH%" (
    echo Executable not found at "%EXE_PATH%".
    exit /b 1
)

echo Running "%EXE_PATH%" ...
echo.
"%EXE_PATH%"
set RUN_EXITCODE=%ERRORLEVEL%

echo.
echo Program exited with code %RUN_EXITCODE%.
exit /b %RUN_EXITCODE%