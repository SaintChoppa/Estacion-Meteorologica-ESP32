@echo off
setlocal enabledelayedexpansion
call "%~dp0_entorno.cmd"

echo ======================================================================
echo   SUBIR EL FIRMWARE AL ESP32
echo ======================================================================
echo.
echo   TU PLACA NO TIENE AUTO-RESET, asi que hay que ayudarla a mano.
echo   Sigue estos pasos AL PIE DE LA LETRA:
echo.
echo     1) MANTEN PULSADO el boton BOOT  (a veces llamado IO0/BOOT)
echo        y NO LO SUELTES TODAVIA.
echo.
echo     2) Sin soltar BOOT, pulsa y suelta UNA VEZ el boton EN (o RST).
echo.
echo     3) Sigue sin soltar BOOT y pulsa una tecla para continuar aqui.
echo.
echo     4) Cuando empieces a ver "Writing at 0x..." YA PUEDES SOLTAR BOOT.
echo.
echo ======================================================================
echo.

if not exist "%BIN%" (
  echo ERROR: no encuentro el binario:
  echo   %BIN%
  echo.
  echo Primero ejecuta  1-COMPILAR.cmd
  pause
  exit /b 1
)

pause

echo.
echo --- Intento 1: con reset automatico (funciona si mantienes BOOT) ---
"%ESPTOOL%" --chip esp32 --port %PUERTO% --baud 460800 ^
  --before default-reset --after hard-reset ^
  write-flash -z 0x0 "%BIN%"

if not errorlevel 1 goto exito

echo.
echo --- El primer intento fallo. Probando SIN tocar el reset (--before no-reset) ---
echo.
echo     AHORA: suelta BOOT, pulsa y suelta EN una vez para dejar el chip
echo     en modo descarga, luego pulsa una tecla.
echo.
pause

"%ESPTOOL%" --chip esp32 --port %PUERTO% --baud 460800 ^
  --before no-reset --after hard-reset ^
  write-flash -z 0x0 "%BIN%"

if errorlevel 1 (
  echo.
  echo **************************************************
  echo   ***  NO SE PUDO SUBIR  ***
  echo **************************************************
  echo   Revisa que el puerto sea el correcto (ahora es %PUERTO%)
  echo   y que el cable sea de DATOS (no solo de carga).
  pause
  exit /b 1
)

:exito
echo.
echo **************************************************
echo   ***  FIRMWARE SUBIDO CORRECTAMENTE  ***
echo **************************************************
echo.
echo   Ya puedes soltar BOOT.
echo   Si la placa no arranca sola, pulsa y suelta EN una vez.
echo.
pause
