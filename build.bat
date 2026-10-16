@echo off
setlocal
if not defined ZIG set ZIG=zig
REM ZIG may hold a path and not a name: run it rather than looking it up on PATH
"%ZIG%" version >nul 2>nul || (echo Zig not found: set ZIG=^<path^> or put it on PATH && exit /b 1)

if not exist bin mkdir bin

REM icon.res goes into every exe, cubolt.res (ComCtl32 v6 manifest) into the GUI only.
"%ZIG%" rc /fo bin\icon.res res\icon.rc
if errorlevel 1 (echo Build failed: res\icon.rc && exit /b 1)
"%ZIG%" rc -Iinclude /fo bin\cubolt.res  res\cubolt.rc
if errorlevel 1 (echo Build failed: res\cubolt.rc && exit /b 1)

REM -lwininet is the update check in include/update.h.
REM -Wall -Wextra -Wpedantic -Werror: warnings are build failures, not printouts.
"%ZIG%" cc -target x86_64-windows-gnu -O3 -Iinclude -Wall -Wextra -Wpedantic -Werror -o bin\search.exe   src\search.c   bin\icon.res -lwininet
if errorlevel 1 (echo Build failed: src\search.c && exit /b 1)
"%ZIG%" cc -target x86_64-windows-gnu -O3 -Iinclude -Wall -Wextra -Wpedantic -Werror -o bin\mktables.exe src\mktables.c bin\icon.res
if errorlevel 1 (echo Build failed: src\mktables.c && exit /b 1)
"%ZIG%" cc -target x86_64-windows-gnu -O3 -Iinclude -Wall -Wextra -Wpedantic -Werror -Wl,--subsystem,windows -lgdi32 -luser32 -lcomdlg32 -lcomctl32 -lshell32 -lole32 -lwininet -o bin\cubolt.exe src\gui.c bin\icon.res bin\cubolt.res
if errorlevel 1 (echo Build failed: src\gui.c && exit /b 1)

copy /y res\cubolt.ico bin\cubolt.ico >nul
if errorlevel 1 (echo Build failed: could not copy res\cubolt.ico && exit /b 1)

echo.
echo Build done.  The binaries are in bin\.
echo   Generate the solver tables once; mktables.exe writes them beside itself, wherever it runs:
echo       bin\mktables.exe
echo   Then start the GUI:  bin\cubolt.exe
