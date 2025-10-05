@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%.." >nul
set "REPO_ROOT=%CD%"

set "PYTHON_EXE="
if exist "%REPO_ROOT%\.venv\Scripts\python.exe" (
    set "PYTHON_EXE=%REPO_ROOT%\.venv\Scripts\python.exe"
)

echo Running Direct3D 11 diagnostics...
if defined PYTHON_EXE (
    echo Using Python interpreter: %PYTHON_EXE%
    "%PYTHON_EXE%" "%REPO_ROOT%\tools\check_d3d11_device.py"
) else (
    where py >nul 2>&1
    if %errorlevel%==0 (
        echo Using Python launcher: py -3
        py -3 "%REPO_ROOT%\tools\check_d3d11_device.py"
    ) else (
        echo Using system Python: python
        python "%REPO_ROOT%\tools\check_d3d11_device.py"
    )
)

if %errorlevel% neq 0 (
    echo.
    echo The diagnostics script exited with an error. See output above.
) else (
    echo.
    echo Diagnostics completed successfully.
)

echo.
pause
popd >nul
endlocal
