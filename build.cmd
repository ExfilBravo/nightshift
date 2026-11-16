@echo off
rem Builds Nightshift with the MSVC toolchain. Usage: build.cmd [Release^|Debug]
setlocal
set CFG=%~1
if "%CFG%"=="" set CFG=Release
set "BUILDDIR=%~dp0build\%CFG%"

rem The newest Visual Studio with its C++ tools and CMake
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath`) do set "VSDIR=%%i"
if "%VSDIR%"=="" (echo Visual Studio C++ build tools with CMake not found & exit /b 1)

rem vcvars runs vswhere by name: make sure its folder is on PATH (it drops off now and then)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

rem Another Visual Studio than last time: configure afresh so CMake picks up its compiler
set "STAMP=%BUILDDIR%\visualstudio.txt"
set "LASTVS="
if exist "%STAMP%" set /p LASTVS=<"%STAMP%"
if /i "%LASTVS%"=="%VSDIR%" goto configured
echo Building with %VSDIR% (was: %LASTVS%), configuring afresh
if exist "%BUILDDIR%\CMakeCache.txt" del /q "%BUILDDIR%\CMakeCache.txt"
if exist "%BUILDDIR%\CMakeFiles" rmdir /s /q "%BUILDDIR%\CMakeFiles"
rem JUCE builds its juceaide tool in a CMake build of its own, which remembers the compiler too
if exist "%BUILDDIR%\juce\tools" rmdir /s /q "%BUILDDIR%\juce\tools"
:configured

cmake -S "%~dp0." -B "%BUILDDIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% || exit /b 1
> "%STAMP%" echo %VSDIR%
cmake --build "%BUILDDIR%" || exit /b 1
echo Built: %BUILDDIR%\Nightshift_artefacts\%CFG%\Nightshift.exe
