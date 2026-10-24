@echo off
rem Build MusicUnlock with MinGW-w64 (gcc must be on PATH).
rem NOTE: keep this file ASCII-only; cmd.exe reads .bat in the OEM codepage.
setlocal
cd /d "%~dp0"

if not exist build mkdir build

echo [1/3] resource
windres res\app.rc -O coff -o build\app.res
if errorlevel 1 goto err

rem ld prints "rsrc merge failure: multiple non-default manifests" because
rem MinGW's built-in manifest collides with ours. It is harmless: the manifest
rem actually embedded is res\app.manifest (comctl32 v6 + dpiAware verified).
rem Only that single line is filtered out; every other warning still shows.

echo [2/3] GUI
gcc -O2 -s -static -municode -mwindows -Wall -Wextra ^
    -finput-charset=UTF-8 -fexec-charset=UTF-8 ^
    -o MusicUnlock.exe ^
    src\main.c src\convert.c src\dec_ncm.c src\dec_kgm.c ^
    src\audio.c src\id3.c src\util.c src\aes.c build\app.res ^
    -lcomctl32 -lshell32 -lshlwapi -lole32 -luuid -lcomdlg32 -lgdi32 -luser32 > build\gui.log 2>&1
if errorlevel 1 goto err_gui
findstr /v /c:"rsrc merge failure" build\gui.log

echo [3/3] CLI
gcc -O2 -s -static -municode -Wall -Wextra ^
    -finput-charset=UTF-8 -fexec-charset=UTF-8 ^
    -o unlock-cli.exe ^
    src\cli.c src\convert.c src\dec_ncm.c src\dec_kgm.c ^
    src\audio.c src\id3.c src\util.c src\aes.c > build\cli.log 2>&1
if errorlevel 1 goto err_cli
findstr /v /c:"rsrc merge failure" build\cli.log

echo.
echo done: MusicUnlock.exe  unlock-cli.exe
goto :eof

:err_gui
type build\gui.log
goto err
:err_cli
type build\cli.log
goto err
:err
echo.
echo BUILD FAILED
exit /b 1
