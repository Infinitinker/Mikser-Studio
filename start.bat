@echo off
rem Starts Mikser Studio from this checkout, as the separate "Mikser Studio Dev" app, so it never
rem touches an installed copy's settings. For development: build it first with the release or debug
rem preset, and set up the AI service's ai\.venv (see docs\development.md). MSYS2 is looked for in
rem C:\msys64, or where MSYS2_ROOT points.
setlocal
cd /d "%~dp0"
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "APP=build\release\bin\mikser-studio.exe"
if not exist "%APP%" set "APP=build\debug\bin\mikser-studio.exe"
if not exist "%APP%" (
    echo Mikser Studio is not built here: build it first, as docs\development.md describes.
    pause
    exit /b 1
)
if not exist "%MSYS2_ROOT%\ucrt64\bin" (
    echo MSYS2's UCRT64 libraries were not found in %MSYS2_ROOT%\ucrt64\bin: set MSYS2_ROOT to where MSYS2 is.
    pause
    exit /b 1
)
rem The libraries Mikser Studio is built with (Qt, MLT, FFmpeg) come from MSYS2.
set "PATH=%MSYS2_ROOT%\ucrt64\bin;%PATH%"
set "MIKSERSTUDIO_INSTANCE=dev"
start "" "%APP%" %*
