@echo off
cd /d "%~dp0"
echo Pokrecem web aplikaciju.
echo Otvori u browseru: http://127.0.0.1:8000
echo.
node tools\web_server.js
echo.
echo Web server je zaustavljen.
pause
