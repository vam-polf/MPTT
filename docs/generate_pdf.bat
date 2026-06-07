@echo off
chcp 65001 >nul
cls
echo ============================================
echo    Real-time Intercom Schematic PDF Generator
echo ============================================
echo.

REM Set Python environment
set PYTHON_HOME=C:\Users\shiti\AppData\Local\Programs\Python\Python312
set PATH=%PYTHON_HOME%;%PYTHON_HOME%\Scripts;%PATH%

echo [1/3] Checking Python...
python --version
if errorlevel 1 (
    echo [ERROR] Python not found
    pause
    exit /b 1
)
echo [OK] Python ready
echo.

REM Install reportlab
echo [2/3] Installing reportlab...
pip install reportlab -q
echo [OK] reportlab installed
echo.

REM Generate PDF
echo [3/3] Generating PDF...
python "%~dp0generate_pdf.py"
if errorlevel 1 (
    echo [ERROR] PDF generation failed
    pause
    exit /b 1
)

echo.
echo ============================================
echo [OK] PDF generated successfully!
echo ============================================
echo.
echo Output: %~dp0intercom_schematic.pdf
echo.
pause
