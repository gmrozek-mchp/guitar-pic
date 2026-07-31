@echo off
setlocal

echo === Puppet Beat Visualizer ===
echo.

:: Check for Python
where python >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Python not found on PATH.
    echo Install Python 3.10+ from https://www.python.org/downloads/
    echo Make sure to check "Add Python to PATH" during install.
    pause
    exit /b 1
)

:: Create venv if it doesn't exist
if not exist "%~dp0.venv" (
    echo Creating virtual environment...
    python -m venv "%~dp0.venv"
    if %errorlevel% neq 0 (
        echo ERROR: Failed to create virtual environment.
        pause
        exit /b 1
    )
)

:: Install/update dependencies
echo Installing dependencies...
"%~dp0.venv\Scripts\pip.exe" install -q -r "%~dp0requirements.txt"
if %errorlevel% neq 0 (
    echo ERROR: Failed to install dependencies.
    pause
    exit /b 1
)

:: Run the app
echo Starting puppet.py...
echo.
"%~dp0.venv\Scripts\python.exe" "%~dp0puppet.py"

if %errorlevel% neq 0 (
    echo.
    echo App exited with an error.
    pause
)
