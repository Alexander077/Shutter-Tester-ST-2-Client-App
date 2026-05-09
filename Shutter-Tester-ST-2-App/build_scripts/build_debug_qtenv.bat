@REM @echo off
@REM echo Build with Qt environment...
call "C:\Qt\6.11.0\mingw_64\bin\qtenv2.bat"
cd /d "%~dp0\.."
cmake --build build --config Debug --target all
@REM pause
