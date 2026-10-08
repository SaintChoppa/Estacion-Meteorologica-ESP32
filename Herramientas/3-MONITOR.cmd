@echo off
setlocal
call "%~dp0_entorno.cmd"

echo ======================================================
echo   MONITOR SERIE  -  Ctrl+C para salir
echo ======================================================
echo   Puerto: %PUERTO%   a  %BAUDIOS% baudios
echo.
echo   Aqui veras los mensajes del ESP32: IP, clima, errores...
echo ======================================================
echo.

"%ARDUINO_CLI%" monitor -p %PUERTO% -c baudrate=%BAUDIOS%
