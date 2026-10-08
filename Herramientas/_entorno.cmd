@echo off
REM =====================================================================
REM  _entorno.cmd  -  Rutas compartidas por los scripts de esta carpeta
REM
REM  La toolchain (arduino-cli + core del ESP32) se busca por defecto en
REM  %USERPROFILE%\AppData\Local\ESP32-Toolchain, que es donde la deja el
REM  README. Si la tienes en otro sitio, define la variable de entorno
REM  TOOL antes de llamar a estos scripts, o cambia la linea de abajo.
REM =====================================================================

if not defined TOOL set "TOOL=%USERPROFILE%\AppData\Local\ESP32-Toolchain"
set "ARDUINO_CLI=%TOOL%\arduino-cli.exe"

REM La version 5.3.1 es la del core esp32 3.3.x. Si actualizas el core,
REM comprueba el nombre real con:  dir "%TOOL%\arduino-data\packages\esp32\tools\esptool_py"
set "ESPTOOL=%TOOL%\arduino-data\packages\esp32\tools\esptool_py\5.3.1\esptool.exe"

REM %~dp0 = carpeta de este script.  ..\Firmware\EstacionMeteo = el sketch
set "SKETCH=%~dp0..\Firmware\EstacionMeteo"
set "BIN=%SKETCH%\build\esp32.esp32.esp32\EstacionMeteo.ino.merged.bin"

REM ⚠️ El puerto COM puede cambiar si enchufas la placa en otro USB.
REM    Para ver el tuyo:  4-INFO-CHIP.cmd  o el Administrador de dispositivos
set "PUERTO=COM5"
set "FQBN=esp32:esp32:esp32"
set "BAUDIOS=115200"
