@echo off
setlocal

rem Resolve repository root relative to this script
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%.." >nul
set "REPO_ROOT=%CD%"

rem Prefer a local virtual environment if available
set "PYTHON_EXE="
if exist "%REPO_ROOT%\.venv\Scripts\python.exe" (
    set "PYTHON_EXE=%REPO_ROOT%\.venv\Scripts\python.exe"
)

rem Determine which Rive file to play (accept drag-and-drop argument)
set "RIV_FILE=%~f1"
if not defined RIV_FILE (
    set "RIV_FILE=%REPO_ROOT%\examples\graphics\data\ui_elements.riv"
)

if not exist "%RIV_FILE%" (
    echo Rive animation not found: "%RIV_FILE%"
    echo.
    echo Drag and drop a .riv file onto this script or edit run_rive_demo.cmd.
    echo.
    goto :PAUSE
)

echo Launching yup Rive demo...
if defined PYTHON_EXE (
    echo Using Python interpreter: %PYTHON_EXE%
    "%PYTHON_EXE%" "%REPO_ROOT%\python\examples\run_rive_ndi.py" --name RiveDemo --width 1280 --height 720 --fps 60 --present-preview "%RIV_FILE%"
) else (
    where py >nul 2>&1
    if %errorlevel%==0 (
        echo Using Python launcher: py -3
        py -3 "%REPO_ROOT%\python\examples\run_rive_ndi.py" --name RiveDemo --width 1280 --height 720 --fps 60 --present-preview "%RIV_FILE%"
    ) else (
        echo Using system Python: python
        python "%REPO_ROOT%\python\examples\run_rive_ndi.py" --name RiveDemo --width 1280 --height 720 --fps 60 --present-preview "%RIV_FILE%"
    )
)

if %errorlevel% neq 0 (
    echo.
    echo The demo exited with an error. Review the diagnostics above.
) else (
    echo.
    echo The demo completed successfully.
)

:PAUSE
echo.
pause
popd >nul
endlocal
