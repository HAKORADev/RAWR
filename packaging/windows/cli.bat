@echo off
rem RAWR convenience wrapper for Windows.
rem Put this next to rawr.exe and call cli.bat "C:\games\Cannon Blast"
setlocal
set "DIR=%~dp0"
"%DIR%rawr.exe" %*
exit /b %ERRORLEVEL%
