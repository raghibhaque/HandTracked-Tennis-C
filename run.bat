@echo off
setlocal

cd /d "%~dp0"
set "PATH=C:\msys64\ucrt64\bin;%PATH%"

if not exist "build\hand_tennis.exe" (
    echo build\hand_tennis.exe was not found.
    echo Run CMake first from this folder.
    exit /b 1
)

"build\hand_tennis.exe" %*
