@echo off
title MONITEUR RADAR 433 - SNIFFER RAUS
cd /d "%~dp0"
if exist ".venv\Scripts\python.exe" (
    ".venv\Scripts\python.exe" -u "moniteur_serie.py"
) else (
    echo Le venv n'existe pas, essai avec python du systeme...
    python -u "moniteur_serie.py"
)
echo.
echo --- Moniteur arrete ---
pause
