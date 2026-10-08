@echo off
setlocal
call "%~dp0_entorno.cmd"

echo ======================================================
echo   COMPILAR  -  Estacion Meteorologica
echo ======================================================
echo   Sketch : %SKETCH%
echo   Placa  : %FQBN%
echo ======================================================
echo.

"%ARDUINO_CLI%" compile --fqbn %FQBN% --export-binaries "%SKETCH%"

if errorlevel 1 (
  echo.
  echo **************************************************
  echo   ***  ERROR AL COMPILAR  ***
  echo **************************************************
  pause
  exit /b 1
)

echo.
echo **************************************************
echo   ***  COMPILADO CORRECTAMENTE  ***
echo **************************************************
echo.
echo   Binario listo en:
echo   %BIN%
echo.
pause
