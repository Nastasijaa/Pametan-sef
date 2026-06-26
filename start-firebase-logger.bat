@echo off
cd /d "%~dp0"
echo Pokrecem Firebase logger.
echo Log fajl ce biti u: C:\Users\dsusi\OneDrive\Desktop\pametni-sef\PAMETAN_SEF_LOG.txt
echo.
node tools\firebase_logger.js
echo.
echo Logger je zaustavljen.
pause
