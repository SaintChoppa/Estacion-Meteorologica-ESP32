@echo off
setlocal
call "%~dp0_entorno.cmd"

echo ======================================================
echo   INFORMACION DEL CHIP ESP32
echo ======================================================
echo.

"%ESPTOOL%" --chip esp32 --port %PUERTO% --before default-reset --after hard-reset flash-id

echo.
echo ======================================================
echo   Si falla, mantén BOOT pulsado, pulsa y suelta EN,
echo   y vuelve a ejecutar este script.
echo ======================================================
pause
