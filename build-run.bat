@echo off
setlocal ENABLEDELAYEDEXPANSION

REM Default config
set CONFIG=Debug
set ACTION=
set APP_ARGS=

REM Parse arguments
:parse_args
if "%~1"=="" goto after_parse

set ARG=%~1
if /I "!ARG!"=="build" (
    set ACTION=build
) else if /I "!ARG!"=="build-run" (
    set ACTION=build-run
) else if "!ARG:~0,2!"=="--" (
    set CONFIG=!ARG:~2!
) else (
    REM Collect remaining arguments for the application
    :collect_app_args
    if "%~1"=="" goto after_parse
    set APP_ARGS=!APP_ARGS! %1
    shift
    goto collect_app_args
)
shift
goto parse_args

:after_parse
if "%ACTION%"=="" (
    echo You must specify "build" or "build-run".
    echo Usage: %~nx0 [build^|build-run] [--Configuration] [app arguments...]
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

REM Determine output path
set OUTDIR=x64\%CONFIG%

REM Copy required DLLs to output directory
echo.
echo Copying DLLs to %OUTDIR%...
copy /Y ".dll\*.dll" "%OUTDIR%\" >nul
if errorlevel 1 (
    echo Warning: Failed to copy some DLLs.
) else (
    echo DLLs copied successfully.
)

if /I "%ACTION%"=="build" (
    exit /b 0
)

set EXE_PATH=%OUTDIR%\win-tiler.exe

if not exist "%EXE_PATH%" (
    echo Executable not found at "%EXE_PATH%".
    exit /b 1
)

echo Running "%EXE_PATH%"%APP_ARGS% ...
echo.
"%EXE_PATH%"%APP_ARGS%
set RUN_EXITCODE=%ERRORLEVEL%

echo.
echo Program exited with code %RUN_EXITCODE%.
exit /b %RUN_EXITCODE%