@echo off
rem Builds NightShift's setup program, build\installer\NightShift-Setup-<version>.exe, with Inno Setup 6
rem (after build.cmd Release). Run it on any PC: it installs NightShift for that user, no admin needed.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\build_setup.ps1"
