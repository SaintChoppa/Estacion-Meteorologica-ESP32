/* ======================================================================
 *  secrets.example.h   ->   PLANTILLA
 * ======================================================================
 *
 *  CÓMO USARLA (2 pasos):
 *
 *    1) Copia este fichero y llámalo  secrets.h   (en esta misma carpeta)
 *    2) Rellena tus datos de verdad en el secrets.h que acabas de crear
 *
 *  Por qué existe: el fichero que usas de verdad, `secrets.h`, contiene la
 *  contraseña de tu WiFi, así que está en el `.gitignore` y NUNCA se sube
 *  al repositorio. Este, en cambio, es la plantilla pública.
 *
 *  ⚠️  No le cambies el nombre a este fichero: tiene que seguir siendo
 *      `secrets.example.h`. El que el programa busca es `secrets.h`.
 * ====================================================================== */

#pragma once

/* ----------------------------------------------------------------------
 *  1. TU RED WiFi
 * ----------------------------------------------------------------------
 *  ⚠️  TIENE que ser una red de **2,4 GHz**. El ESP32 no ve las de 5 GHz,
 *      y ese es el motivo nº1 de "no se pudo conectar".
 * -------------------------------------------------------------------- */
#define WIFI_SSID "NOMBRE_DE_TU_WIFI"
#define WIFI_PASS "TU_CONTRASENA"

/* ----------------------------------------------------------------------
 *  2. CONTACTO PARA MET NORWAY  (la API de respaldo)
 * ----------------------------------------------------------------------
 *  El clima lo da Open-Meteo; si falla, se usa MET Norway (del gobierno
 *  noruego). Sus condiciones de uso exigen identificarse con un contacto
 *  REAL, y lo dicen así de claro:
 *
 *      "Do not fake this or you are likely to be permanently blacklisted!"
 *
 *  O sea: si pones un texto genérico, pueden bloquear tu IP PARA SIEMPRE
 *  y sin avisar. Pon algo que exista de verdad: tu correo o tu usuario de
 *  GitHub. Ejemplo:
 *
 *      #define METNO_USER_AGENT "EstacionMeteoESP32/1.0 github.com/TU_USUARIO"
 * -------------------------------------------------------------------- */
#define METNO_USER_AGENT "EstacionMeteoESP32/1.0 github.com/CAMBIA_ESTO"
