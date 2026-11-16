@echo off
rem Installs the Release build for this user: copies Nightshift.exe to
rem %LOCALAPPDATA%\Programs\Nightshift and adds NightShift shortcuts to the Start menu and desktop
rem (tools\shortcuts.ps1, which also makes Explorer show a new icon at once).
rem Usage: install.cmd [/uninstall]   (settings, kit and projects are left alone)
rem The exe keeps its first name: Windows Security's Controlled folder access allows it by that path.
setlocal
set SRC=%~dp0build\Release\Nightshift_artefacts\Release\Nightshift.exe
set ICO=%~dp0build\Release\Nightshift_artefacts\JuceLibraryCode\icon.ico
set DEST=%LOCALAPPDATA%\Programs\Nightshift
set SHORTCUTS=powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\shortcuts.ps1"

if /i "%~1"=="/uninstall" (
    %SHORTCUTS% -Remove
    rmdir /s /q "%DEST%" 2>nul
    if exist "%DEST%" (echo Close NightShift first, then run this again & exit /b 1)
    echo Removed NightShift.
    exit /b 0
)

if not exist "%SRC%" (echo Not built yet - run build.cmd first & exit /b 1)
if not exist "%DEST%" mkdir "%DEST%" || exit /b 1

rem A running NightShift can't be overwritten, but it can be renamed out of the way;
rem the old copy is deleted on the next install once it has closed.
del /q "%DEST%\Nightshift.old*.exe" 2>nul
copy /y "%SRC%" "%DEST%\Nightshift.exe" >nul 2>&1 || (
    ren "%DEST%\Nightshift.exe" "Nightshift.old%RANDOM%.exe" || exit /b 1
    copy /y "%SRC%" "%DEST%\Nightshift.exe" >nul || exit /b 1
)

rem The GPL's text goes with it (NightShift is GPLv3-or-later; About's Licence... opens it)
copy /y "%~dp0LICENSE" "%DEST%\LICENSE.txt" >nul || exit /b 1

%SHORTCUTS% "%DEST%\Nightshift.exe" "%ICO%" || exit /b 1
echo Installed: %DEST%\Nightshift.exe
echo Shortcuts: NightShift, in the Start menu and on the desktop
