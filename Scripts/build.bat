@echo off
rem Build MKJamStarter from the command line (Docs/Development.md, first section).
rem Double-click it, or run from cmd / PowerShell:
rem   Scripts\build.bat                    editor target MKJamStarterEditor, Development
rem   Scripts\build.bat game               game target MKJamStarter (before a cook)
rem   Scripts\build.bat editor DebugGame   any of Development, DebugGame, Shipping
rem   Scripts\build.bat -nopause           no "press any key" at the end (for other scripts)
rem The engine is %UE_ROOT% if set, otherwise the .uproject's EngineAssociation looked up in the
rem registry (launcher install or a registered source build), otherwise Program Files\Epic Games\UE_<ver>.
rem Exit code is the one from UnrealBuildTool.
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "UPROJECT=%ROOT%\MKJamStarter.uproject"
set "TARGET=MKJamStarterEditor"
set "CONFIG=Development"
set "NOPAUSE="

:args
if "%~1"=="" goto args_done
if /i "%~1"=="editor" (set "TARGET=MKJamStarterEditor"
) else if /i "%~1"=="game" (set "TARGET=MKJamStarter"
) else if /i "%~1"=="Development" (set "CONFIG=Development"
) else if /i "%~1"=="DebugGame" (set "CONFIG=DebugGame"
) else if /i "%~1"=="Shipping" (set "CONFIG=Shipping"
) else if /i "%~1"=="-nopause" (set "NOPAUSE=1"
) else (
    echo Unknown argument "%~1". Usage: build.bat [editor^|game] [Development^|DebugGame^|Shipping] [-nopause]
    set "RC=2"
    goto done
)
shift
goto args
:args_done

if not exist "%UPROJECT%" (
    echo Not found: "%UPROJECT%"
    set "RC=1"
    goto done
)

if defined UE_ROOT goto engine_found

rem "5.8" for a launcher install, a {GUID} for a source build.
set "ASSOC="
for /f "usebackq delims=" %%A in (`powershell -NoProfile -Command "(Get-Content -Raw -LiteralPath $env:UPROJECT | ConvertFrom-Json).EngineAssociation"`) do set "ASSOC=%%A"
if not defined ASSOC (
    echo Could not read EngineAssociation from "%UPROJECT%". Set UE_ROOT to the engine folder.
    set "RC=1"
    goto done
)

for %%K in ("HKLM\SOFTWARE\EpicGames\Unreal Engine\%ASSOC%" "HKLM\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\%ASSOC%") do (
    if not defined UE_ROOT for /f "tokens=2,*" %%A in ('reg query %%K /v InstalledDirectory 2^>nul ^| find "REG_SZ"') do set "UE_ROOT=%%B"
)
if not defined UE_ROOT for /f "tokens=2,*" %%A in ('reg query "HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds" /v "%ASSOC%" 2^>nul ^| find "REG_SZ"') do set "UE_ROOT=%%B"
if not defined UE_ROOT if exist "%ProgramFiles%\Epic Games\UE_%ASSOC%\Engine" set "UE_ROOT=%ProgramFiles%\Epic Games\UE_%ASSOC%"
if not defined UE_ROOT (
    echo Unreal Engine "%ASSOC%" not found in the registry or Program Files. Set UE_ROOT to the engine folder.
    set "RC=1"
    goto done
)

:engine_found
set "BUILD_BAT=%UE_ROOT%\Engine\Build\BatchFiles\Build.bat"
if not exist "%BUILD_BAT%" (
    echo Not found: "%BUILD_BAT%". UE_ROOT is "%UE_ROOT%".
    set "RC=1"
    goto done
)

echo Engine:  %UE_ROOT%
echo Project: %UPROJECT%
echo Target:  %TARGET% Win64 %CONFIG%
echo Started: %TIME%
echo.
call "%BUILD_BAT%" %TARGET% Win64 %CONFIG% "-Project=%UPROJECT%" -WaitMutex -NoHotReloadFromIDE
set "RC=%ERRORLEVEL%"
echo.
echo Finished: %TIME%

:done
echo.
if "%RC%"=="0" (
    echo BUILD OK
) else (
    echo BUILD FAILED, exit code %RC%
    if /i "%TARGET%"=="MKJamStarterEditor" echo If the log says Live Coding is active: close the editor of this checkout or press Ctrl+Alt+F11 in it.
)
if not defined NOPAUSE pause
exit /b %RC%
