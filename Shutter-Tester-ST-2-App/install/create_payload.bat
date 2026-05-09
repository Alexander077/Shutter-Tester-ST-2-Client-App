@REM cd /d "%~dp0\"

@echo off
set "target_dir=%~dp0\payload"

echo Cleaning folder: %target_dir%...

if exist "%target_dir%" (
    pushd "%target_dir%" && (
        del /q /s *.*
        for /d %%i in (*) do rmdir /s /q "%%i"
        popd
        echo Done! All contents removed.
    ) || (
        echo Error: Could not access the folder.
    )
) else (
    echo Error: Folder does not exist.
)

mkdir "payload\assets"
copy "..\build\Shutter Tester ST-2 App.exe" "payload"
copy "..\assets\st2_fp_report_template.html" "payload\assets"
copy "..\assets\camera_icon_64x64.png" "payload\assets"
call "C:\Qt\6.11.0\mingw_64\bin\qtenv2.bat"
windeployqt.exe "%~dp0\payload\Shutter Tester ST-2 App.exe"
pause