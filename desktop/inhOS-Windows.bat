@echo off
chcp 65001 >nul
cd /d "%~dp0"
if not exist ".venv\Scripts\python.exe" (
  py -3 -m venv .venv
  if errorlevel 1 goto fail
)
".venv\Scripts\python.exe" -c "import serial, PIL" >nul 2>&1
if errorlevel 1 (
  ".venv\Scripts\python.exe" -m pip install -r requirements.txt
  if errorlevel 1 goto fail
)
".venv\Scripts\python.exe" windows.py
if errorlevel 1 goto fail
exit /b 0
:fail
 echo Ошибка запуска. Нужен Python 3.10+ с python.org, Tcl/Tk и драйвер CP210x.
 pause
 exit /b 1
