@echo off
rem Build one xmake framework checkout (ArchiveXL, TweakXL, Codeware, CET) with the VS 18 toolchain.
rem   tools\patchday\xmake_build.cmd <checkout dir> [log file] [jobs]
rem Run from cmd, not git-bash: git-bash exports MSYSTEM and xmake then picks mingw.
rem A failed build is repeated once, serial and with -v: xmake's own summary names a file but not the message
rem ("error: > in lib\Core\Runtime\HostImage.cpp" was really "cannot get program for mrc" from a parallel job).
setlocal
set "SRC=%~1"
set "LOG=%~2"
set "JOBS=%~3"
if "%LOG%"=="" set "LOG=%SRC%\xmake_build.log"
if "%JOBS%"=="" set "JOBS=4"
set MSYSTEM=
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set "XMAKE=%~dp0..\..\build\patchday\tools\xmake\xmake.exe"
rem Never let xmake run outside a framework checkout: without an xmake.lua it "trybuilds" whatever it finds
rem (2026-10-22 it ran cmake configure + build on the port's own build\ from the repo root).
if not exist "%SRC%\xmake.lua" (
    echo no xmake.lua in "%SRC%" 1>&2
    exit /b 2
)
cd /d "%SRC%" || exit /b 2
"%XMAKE%" f -c -p windows -a x64 -m release --toolchain=msvc -y > "%LOG%" 2>&1
if not %errorlevel% equ 0 exit /b 1
"%XMAKE%" build -y -j %JOBS% >> "%LOG%" 2>&1
rem xmake fails with -1, which `if not errorlevel 1` takes for success
if %errorlevel% equ 0 exit /b 0
rem TweakXL loses its resource compiler only in a parallel build; serial with -v also shows a real compiler error
echo === retry serial, verbose >> "%LOG%"
"%XMAKE%" build -y -v -j 1 >> "%LOG%" 2>&1
if %errorlevel% equ 0 exit /b 0
exit /b 1
