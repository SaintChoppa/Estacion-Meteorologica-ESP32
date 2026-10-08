/* =====================================================================
 *  ESTACION METEOROLOGICA
 *  ESP32  +  Matriz de LEDs MAX7219 8x8 (modulo 4-en-1, "1088AB")
 * ---------------------------------------------------------------------
 *  Que hace:
 *    1. Se conecta al WiFi (2.4 GHz)
 *    2. Pide el clima actual a DOS APIs independientes y GRATIS:
 *         1) Open-Meteo  (principal)
 *         2) MET Norway  (respaldo automatico, gobierno noruego)
 *       Si la primera falla, prueba la segunda en la misma pasada.
 *    3. Muestra temperatura, humedad, sensacion termica y estado del
 *       cielo en la matriz de LEDs, desplazandose
 *    4. Levanta una mini pagina web para verlo desde el celular
 *
 *  Placa en el IDE:  "ESP32 Dev Module"
 *  Autor: tu asistente Neko-chan  (=^..^=)
 * =====================================================================
 */

/* ------------------------------------------------------------------
 *  IMPORTANTE: "config.h" va PRIMERO.
 *  Define SERVIDOR_WEB y demas macros que usan los #if de mas abajo.
 * ------------------------------------------------------------------ */
#include "config.h"

/* ------------------------------------------------------------------
 *  CREDENCIALES
 * ------------------------------------------------------------------
 *  Tus datos del WiFi (y el contacto para MET Norway) viven en
 *  secrets.h, que NO se sube al repositorio porque lleva la contrasena
 *  (esta en .gitignore). Aqui solo se sube secrets.example.h, que es la
 *  plantilla.
 *
 *  Si acabas de clonar el proyecto y te falta: copia secrets.example.h
 *  a secrets.h y rellena tus datos.
 * ------------------------------------------------------------------ */
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #error "Falta Firmware/EstacionMeteo/secrets.h -> copia secrets.example.h a secrets.h y rellena tu WiFi"
#endif

#include <SPI.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>      // para poder PARAR el SNTP (ver sincronizarHora)

#if SERVIDOR_WEB
  #include <WebServer.h>
#endif

#include "weather.h"

/* ------------------------------------------------------------------
 *  PILA DE MEMORIA DE LA TAREA PRINCIPAL
 * ------------------------------------------------------------------
 *  El handshake TLS con certificados RSA-4096 (los de HARICA, que usa
 *  MET Norway) gasta bastante mas pila que uno ECDSA (el de Open-Meteo,
 *  mucho mas ligero). Arduino da 8 KB por defecto y puede quedarse
 *  corto.  getArduinoLoopTaskStackSize() esta declarada como "weak" en
 *  el core, asi que la app puede sobreescribirla con este macro.
 *  TIENE QUE IR A NIVEL GLOBAL (fuera de cualquier funcion).
 * ------------------------------------------------------------------ */
SET_LOOP_TASK_STACK_SIZE(PILA_LOOP_BYTES);

/* ------------------------------------------------------------------
 *  Objetos globales
 * ------------------------------------------------------------------ */
MD_Parola pantalla(HARDWARE_TYPE, PIN_CS, NUM_MODULOS);

#if SERVIDOR_WEB
WebServer servidor(PUERTO_WEB);
#endif

Clima clima;

/* ------------------------------------------------------------------
 *  Estado del programa
 * ------------------------------------------------------------------ */
static unsigned long tUltimaConsulta  = 0;
static bool          primeraConsulta  = true;
static uint8_t       paginaActual     = 0;

/* ------------------------------------------------------------------
 *  SALUD DEL EQUIPO: por que se reinicio y cuantas veces seguidas
 * ------------------------------------------------------------------
 *  El ESP32 guarda el motivo del ultimo reinicio. Es LA pista que dice
 *  si el problema es la corriente (BROWNOUT), un cuelgue del programa
 *  (PANICO / WATCHDOG) o un reinicio normal. Sin esto, un reinicio es
 *  completamente ciego: solo se ve que "se reinicio", no el porque.
 *
 *  El contador de arranques seguidos NO puede vivir en la memoria RTC:
 *  en el ESP32 clasico un reinicio por EN (o por brownout) reinicia
 *  TAMBIEN el dominio RTC, asi que RTC_NOINIT_ATTR se pierde.
 *  Comprobado en la placa el 2026-10-08: se quedaba clavado en 1. Se
 *  guarda en la FLASH (NVS), que si sobrevive a todo.
 *
 *  El tope evita castigar la flash: si el equipo entra en un bucle de
 *  reinicios, como mucho escribe TOPE_ARRANQUES veces y luego deja de
 *  contar. Asi sabemos "se reinicio 30+ veces seguidas" sin desgastar
 *  nada (y sin gastar corriente escribiendo durante un brownout).
 * ------------------------------------------------------------------ */
#include <Preferences.h>      // NVS: memoria que sobrevive a los reinicios

#define TOPE_ARRANQUES 30
static uint32_t arranquesSeguidos  = 0;
static bool     enBucleDeReinicios = false;

static void contarArranque() {
  Preferences prefs;
  if (!prefs.begin("salud", false)) {     // false = tambien para escribir
    Serial.println(F("[salud] aviso: no se pudo abrir la NVS"));
    return;
  }
  arranquesSeguidos = prefs.getUInt("arranques", 0);
  if (arranquesSeguidos < TOPE_ARRANQUES) {
    arranquesSeguidos++;
    prefs.putUInt("arranques", arranquesSeguidos);
  }
  prefs.end();
  enBucleDeReinicios = (arranquesSeguidos > 3);
}

/* Se llama cuando llevamos 1 minuto entero en marcha: entonces NO
   estamos en un bucle, asi que se borra el contador de la flash. */
static void marcarArranqueSano() {
  Preferences prefs;
  if (prefs.begin("salud", false)) {
    prefs.putUInt("arranques", 0);
    prefs.end();
  }
}

static const char *motivoReinicio() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "ENCENDIDO (le llego corriente o pulsaste EN)";
    case ESP_RST_EXT:       return "BOTON EN";
    case ESP_RST_SW:        return "POR SOFTWARE";
    case ESP_RST_PANIC:     return "PANICO: fallo el programa (assert / excepcion)";
    case ESP_RST_INT_WDT:   return "WATCHDOG de interrupciones (se bloqueo la red)";
    case ESP_RST_TASK_WDT:  return "WATCHDOG de tarea (el programa se quedo colgado)";
    case ESP_RST_WDT:       return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "VUELTA DE DORMIR PROFUNDO";
    case ESP_RST_BROWNOUT:  return "*** FALTA DE CORRIENTE (brownout) ***";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "DESCONOCIDO";
  }
}

/* Buffer de texto que se manda a la matriz.
   220 caracteres sobra de sobra para cualquier mensaje. */
static char buf[220];

/* Simbolo de grado (la bolita de "24.3°C") dibujado a mano.
   La fuente de la libreria es ASCII y no trae el simbolo "°",
   asi que lo definimos nosotros y lo registramos como '~' */
static uint8_t SIMBOLO_GRADO[5] = { 0x06, 0x09, 0x09, 0x06, 0x00 };


/* =====================================================================
 *  UTILIDADES
 * ===================================================================== */

/* Formatea un float con 1 decimal SIN usar %f (que en ESP32 ocupa
   mucha memoria de programa).  22.4371  ->  "22.4"   */
static void formatear1(float v, char *salida, size_t n) {
  if (isnan(v)) { snprintf(salida, n, "--"); return; }

  int entero = (int)v;
  int dec    = (int)lroundf(fabsf(v - (float)entero) * 10.0f);

  if (dec >= 10) {                    // redondeo hacia arriba, ej. 22.97
    dec = 0;
    entero += (v >= 0 ? 1 : -1);
  }
  if (v < 0 && entero == 0) snprintf(salida, n, "-0.%d", dec);
  else                      snprintf(salida, n, "%d.%d", entero, dec);
}

/* Hora local, en el formato que diga config.h (12 h por defecto).
   Devuelve false si el reloj aun no esta sincronizado. */
static bool horaLocal(char *salida, size_t n) {
  struct tm hora;
  if (!getLocalTime(&hora, 0)) { snprintf(salida, n, "--:--"); return false; }

#if HORA_24H
  snprintf(salida, n, "%d:%02d", hora.tm_hour, hora.tm_min);
#else
  int h12 = hora.tm_hour % 12;
  if (h12 == 0) h12 = 12;                 // 0 -> 12 ; 13 -> 1
  snprintf(salida, n, "%d:%02d %s", h12, hora.tm_min,
           hora.tm_hour < 12 ? "AM" : "PM");
#endif
  return true;
}

/* Cuantos minutos llevan los datos actuales sin refrescarse */
static unsigned long antiguedadMinutos() {
  return (millis() - clima.actualizado) / 60000UL;
}


/* =====================================================================
 *  PANTALLA
 * ===================================================================== */
static void configurarPantalla() {
  pantalla.begin();                       // inicia SPI y la matriz
  pantalla.setIntensity(BRILLO);          // 0..15
  pantalla.displayClear();
  pantalla.addChar('~', SIMBOLO_GRADO);   // registra nuestro simbolo de grado
}

/* Construye la "cinta continua".
   ORDEN PEDIDO POR EL USUARIO (no cambiarlo sin preguntar):
     Hora -> Temperatura -> Sensacion -> Humedad -> Viento (vel) -> Viento (dir)
   La ciudad y el estado del cielo quedan en la pagina web; aqui sobran. */
static void construirTicker() {

  // Nunca hemos conseguido ningun dato
  if (clima.fuente == FUENTE_NINGUNA) {
    snprintf(buf, sizeof(buf),
             "   %s   ESPERANDO DATOS DEL SERVIDOR...   ", CIUDAD);
    return;
  }

  char h[12], t[10], s[10], v[10];
  horaLocal(h, sizeof(h));
  formatear1(clima.temp, t, sizeof(t));
  formatear1(clima.sensacion, s, sizeof(s));
  formatear1(valorViento(clima.vientoVel), v, sizeof(v));

  // Si las dos APIs fallaron, delante sale "!!" para que se note que el
  // dato que estas viendo es el ultimo bueno, no el de ahora mismo.
  snprintf(buf, sizeof(buf),
           "   %s%s   TEMP %s~C   SENS %s~C   HUM %d%%   VIENTO %s %s %s   ",
           clima.obsoleto ? "!! " : "", h, t, s,
           (int)lroundf(clima.humedad), v, UNIDAD_VIENTO,
           direccionCardinal(clima.vientoDir));
}

/* Construye una de las paginas rotativas (mismo orden que la cinta) */
#define N_PAGINAS 6
static void construirPagina(uint8_t i) {
  char h[12] = "--:--", t[10] = "--", s[10] = "--", v[10] = "--";
  const bool hayDatos = (clima.fuente != FUENTE_NINGUNA);

  horaLocal(h, sizeof(h));
  if (hayDatos) {
    formatear1(clima.temp, t, sizeof(t));
    formatear1(clima.sensacion, s, sizeof(s));
    formatear1(valorViento(clima.vientoVel), v, sizeof(v));
  }

  switch (i) {
    case 0:  snprintf(buf, sizeof(buf), "%s", h);                           break;
    case 1:  snprintf(buf, sizeof(buf), "TEMP %s~C", t);                    break;
    case 2:  snprintf(buf, sizeof(buf), "SENS %s~C", s);                    break;
    case 3:  snprintf(buf, sizeof(buf), "HUM %d%%",
                      hayDatos ? (int)lroundf(clima.humedad) : 0);          break;
    case 4:  snprintf(buf, sizeof(buf), "VIENTO %s %s", v, UNIDAD_VIENTO);  break;
    default: snprintf(buf, sizeof(buf), "DIR %s",
                      direccionCardinal(clima.vientoDir));                  break;
  }
}

/* Manda a la matriz el texto que corresponda segun el modo */
static void mostrarActual() {
#if MODO_PAGINAS
  construirPagina(paginaActual);
  paginaActual = (paginaActual + 1) % N_PAGINAS;
#else
  construirTicker();
#endif
  pantalla.displayText(buf, PA_CENTER, VELOCIDAD_SCROLL, PAUSA_TEXTO,
                       PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

/* Espera a que acabe la animacion, pero COMO MAXIMO esos milisegundos.
   Por que el tope: si el MAX7219 no responde (cable flojo, chip tocado),
   displayAnimate() podria no terminar NUNCA y el arranque se quedaria
   colgado para siempre. Con el tope, el equipo sigue adelante igual. */
static void esperarAnimacion(unsigned long topeMs) {
  const unsigned long t0 = millis();
  while (!pantalla.displayAnimate() && (millis() - t0) < topeMs) {
    delay(5);
  }
}

/* Muestra un texto y espera (bloqueando) a que termine la animacion.
   Solo se usa al arrancar, nunca dentro del loop. */
static void mostrarYEsperar(const char *txt) {
  pantalla.displayText(txt, PA_CENTER, 35, 400, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  esperarAnimacion(30000UL);
}


/* =====================================================================
 *  WIFI
 * ===================================================================== */
static void conectarWiFi() {
  Serial.printf("\n[wifi] Conectando a \"%s\" ...\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
#if WIFI_SIN_DORMIR
  WiFi.setSleep(false);           // siempre despierto: mas consumo, menos latencia
#else
  WiFi.setSleep(true);            // duerme entre balizas: ~5x menos consumo
                                  // (con muchos LEDs esto estabiliza la alimentacion)
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - inicio) < WIFI_TIMEOUT_MS) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] Conectado.  IP: %s   Senal: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println(F("[wifi] ERROR: no se pudo conectar. Revisa secrets.h"));
  }
}

/* =====================================================================
 *  RELOJ POR INTERNET (NTP)
 * ---------------------------------------------------------------------
 *  ¡OJO, AQUI ESTABA EL REINICIO EN BUCLE! No tocar el orden sin leer esto.
 *
 *  Arduino resuelve nombres con NetworkManager::hostByName(), que en su
 *  primera resolucion despues de obtener IP (cuando cambia el estado de
 *  direcciones globales) llama a dns_clear_cache(). Esa funcion dispara
 *  los callbacks de TODAS las consultas DNS pendientes, y si SNTP tiene
 *  una a medias, se reentra en dns_gethostbyname DESDE el callback. lwIP
 *  no lo soporta y aborta:
 *
 *      assert failed: udp_new_ip_type
 *      (Required to lock TCPIP core functionality!)
 *      rst:0xc (SW_CPU_RESET)   <-- se reinicia en bucle
 *
 *  La solucion tiene dos partes:
 *    1) Resolver un nombre A PROPOSITO *antes* de arrancar SNTP: asi esa
 *       "limpieza de cache" ocurre cuando no hay nada pendiente (inofensiva),
 *       y ya no volvera a pasar en toda la sesion.
 *    2) Esperar DE VERDAD a que NTP termine (con delay, cediendo el turno a
 *       lwIP) y DESPUES parar el SNTP, para que nunca quede una consulta a
 *       medias cuando despues resolvamos api.open-meteo.com o api.met.no.
 *
 *  Parar el SNTP no afecta al reloj: la hora ya esta puesta en el RTC del
 *  ESP32. Solo se pierde la resincronizacion automatica, y por eso se
 *  vuelve a pedir cada RESINCRONIZAR_HORAS desde el loop.
 * ===================================================================== */
static bool sincronizarHora() {

  // La hora NO se pide aparte por defecto: viene en la cabecera "Date" de
  // la respuesta del clima, que ya descargamos cada 10 min (ver
  // ponerHoraDesdeHTTP() en weather.h). Con HORA_POR_NTP 0 esta funcion no
  // hace nada: ni SNTP, ni consulta DNS extra, ni el riesgo de reentrada de
  // lwIP que traia SNTP. Si algun dia la quieres por NTP, pon HORA_POR_NTP 1.
  if (HORA_POR_NTP == 0) return false;

  // --- Paso 1: resolver un nombre ANTES de arrancar SNTP -------------
  {
    IPAddress ip;
    WiFi.hostByName("api.open-meteo.com", ip);   // a proposito, ver arriba
  }

  // --- Paso 2: arrancar SNTP ----------------------------------------
  configTzTime(TZ_POSIX, SERVIDOR_NTP_1, SERVIDOR_NTP_2);

  // --- Paso 3: esperar de verdad (el delay le cede el turno a lwIP) --
  const unsigned long limite = millis() + ESPERA_NTP_MS;
  struct tm hora;
  bool sincronizado = false;

  while (millis() < limite) {
    if (getLocalTime(&hora, 0)) { sincronizado = true; break; }
    pantalla.displayAnimate();      // que la matriz no se congele mientras tanto
    delay(250);
  }

  // --- Paso 4: dejar cerrar las DNS pendientes y PARAR SNTP ---------
  delay(400);
  esp_sntp_stop();

  if (sincronizado) {
    Serial.printf("[hora] Sincronizada: %02d:%02d:%02d  (SNTP detenido a proposito)\n",
                  hora.tm_hour, hora.tm_min, hora.tm_sec);
  } else {
    Serial.println(F("[hora] NTP no respondio. Se usara el reloj interno."));
  }
  return sincronizado;
}


/* =====================================================================
 *  SERVIDOR WEB  (opcional, se activa con SERVIDOR_WEB en config.h)
 * ===================================================================== */
#if SERVIDOR_WEB

static void manejarRaiz() {
  const bool hayDatos = (clima.fuente != FUENTE_NINGUNA);

  char t[10], s[10], h[12], hum[8], v[10];
  formatear1(clima.temp, t, sizeof(t));
  formatear1(clima.sensacion, s, sizeof(s));
  formatear1(valorViento(clima.vientoVel), v, sizeof(v));
  horaLocal(h, sizeof(h));
  snprintf(hum, sizeof(hum), "%d", hayDatos ? (int)lroundf(clima.humedad) : 0);

  unsigned long seg = millis() / 1000UL;
  char uptime[32];
  snprintf(uptime, sizeof(uptime), "%luh %lum", seg / 3600UL, (seg % 3600UL) / 60UL);

  // --- Como de fresco es el dato que estamos mostrando ---
  const char *colorEstado;
  String textoEstado;
  if (!hayDatos) {
    colorEstado = "#e5484d";
    textoEstado = "sin conexion con las APIs del clima";
  } else if (clima.obsoleto) {
    colorEstado = "#f59e0b";
    textoEstado = "datos de hace " + String(antiguedadMinutos()) + " min (reintentando)";
  } else {
    colorEstado = "#22c55e";
    textoEstado = String("actualizado con ") + clima.proveedor;
  }

  String p;
  p.reserve(3000);
  p += F("<!DOCTYPE html><html lang=\"es\"><head><meta charset=\"utf-8\">");
  p += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  p += F("<meta http-equiv=\"refresh\" content=\"20\">");
  p += F("<title>Estacion Meteorologica</title><style>");
  p += F("*{box-sizing:border-box}"
         "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#0f1115;"
         "color:#e8ecf1;margin:0;display:flex;flex-direction:column;align-items:center;"
         "padding:32px 16px;min-height:100vh}"
         "h1{font-size:.85rem;font-weight:600;letter-spacing:.22em;color:#8b98a8;margin:0 0 28px}"
         ".rej{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));"
         "gap:12px;width:100%;max-width:520px}"
         ".c{background:#171a21;border:1px solid #232833;border-radius:16px;padding:20px;text-align:center}"
         ".v{font-size:2.6rem;font-weight:700;line-height:1.05;font-variant-numeric:tabular-nums}"
         ".l{font-size:.68rem;letter-spacing:.16em;color:#8b98a8;margin-top:8px}"
         ".est{margin-top:24px;display:flex;align-items:center;gap:8px;font-size:.75rem}"
         ".pt{width:8px;height:8px;border-radius:50%;flex:0 0 auto}"
         ".pie{margin-top:14px;font-size:.72rem;color:#5f6b7a;text-align:center;line-height:1.8}");
  p += F("</style></head><body><h1>ESTACION METEOROLOGICA</h1><div class=\"rej\">");

  p += "<div class=\"c\"><div class=\"v\">" + String(h) + "</div><div class=\"l\">HORA</div></div>";
  p += "<div class=\"c\"><div class=\"v\">" + String(t) + "&deg;C</div><div class=\"l\">TEMPERATURA</div></div>";
  p += "<div class=\"c\"><div class=\"v\">" + String(s) + "&deg;C</div><div class=\"l\">SENSACION</div></div>";
  p += "<div class=\"c\"><div class=\"v\">" + String(hum) + "%</div><div class=\"l\">HUMEDAD</div></div>";
  p += "<div class=\"c\"><div class=\"v\" style=\"font-size:1.7rem\">" + String(v) +
       " <span style=\"font-size:.85rem\">" + String(UNIDAD_VIENTO) +
       "</span></div><div class=\"l\">VIENTO</div></div>";
  p += "<div class=\"c\"><div class=\"v\" style=\"font-size:1.7rem\">" +
       String(direccionCardinal(clima.vientoDir)) + "</div><div class=\"l\">DIRECCION</div></div>";
  p += "<div class=\"c\"><div class=\"v\" style=\"font-size:1.15rem\">" +
       String(hayDatos ? clima.descripcion : "SIN DATOS") + "</div><div class=\"l\">CIELO</div></div>";

  p += F("</div>");
  p += "<div class=\"est\"><span class=\"pt\" style=\"background:" + String(colorEstado) +
       "\"></span><span>" + textoEstado + "</span></div>";

  p += F("<div class=\"pie\">");
  p += String(CIUDAD) + " &middot; " + String(LATITUD) + ", " + String(LONGITUD) + "<br>";
  p += "IP " + WiFi.localIP().toString() + " &middot; senal " + String(WiFi.RSSI()) + " dBm";
  p += " &middot; encendido " + String(uptime);
  p += F("<br>APIs: Open-Meteo + MET Norway (respaldo)");
  p += F("</div></body></html>");

  servidor.send(200, "text/html; charset=utf-8", p);
}

static void manejarJson() {
  const bool hayDatos = (clima.fuente != FUENTE_NINGUNA);

  char horaTxt[12];
  horaLocal(horaTxt, sizeof(horaTxt));

  String j = F("{");
  j += "\"ciudad\":\"" + String(CIUDAD) + "\",";
  j += "\"latitud\":" + String(LATITUD) + ",";
  j += "\"longitud\":" + String(LONGITUD) + ",";
  j += "\"hora\":\"" + String(horaTxt) + "\",";
  j += "\"ok\":" + String(hayDatos ? "true" : "false") + ",";
  j += "\"obsoleto\":" + String(clima.obsoleto ? "true" : "false") + ",";
  j += "\"proveedor\":\"" + String(clima.proveedor) + "\",";
  j += "\"antiguedad_min\":" + String(hayDatos ? antiguedadMinutos() : 0UL) + ",";
  j += "\"temperatura\":" + String(hayDatos ? clima.temp : NAN, 1) + ",";
  j += "\"humedad\":" + String(hayDatos ? clima.humedad : NAN, 0) + ",";
  j += "\"sensacion\":" + String(hayDatos ? clima.sensacion : NAN, 1) + ",";
  j += "\"viento_kmh\":" + String(hayDatos ? clima.vientoVel : NAN, 1) + ",";
  j += "\"viento_unidad\":\"" + String(UNIDAD_VIENTO) + "\",";
  j += "\"viento_dir_grados\":" + String(hayDatos ? clima.vientoDir : NAN, 0) + ",";
  j += "\"viento_dir\":\"" + String(direccionCardinal(clima.vientoDir)) + "\",";
  j += "\"codigo_wmo\":" + String(clima.codigo) + ",";
  j += "\"descripcion\":\"" + String(clima.descripcion) + "\",";
  j += "\"senal_dbm\":" + String(WiFi.RSSI()) + ",";
  j += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  j += "\"uptime_s\":" + String(millis() / 1000UL);
  j += F("}");
  servidor.send(200, "application/json", j);
}

static void configurarWeb() {
  servidor.on("/", manejarRaiz);
  servidor.on("/json", manejarJson);
  servidor.onNotFound([]() { servidor.send(404, "text/plain", "No encontrado"); });
  servidor.begin();
  Serial.print(F("[web] Servidor listo en http://"));
  Serial.println(WiFi.localIP());
}

#endif  // SERVIDOR_WEB


/* =====================================================================
 *  SETUP
 * ===================================================================== */
void setup() {
  Serial.begin(BAUDIOS);
  delay(400);
  /* --- Salud del equipo, ANTES de nada ------------------------------ */
  contarArranque();

  Serial.println(F("\n\n=== ESTACION METEOROLOGICA - ESP32 + MAX7219 ==="));
  Serial.printf("=== Ciudad: %s  (lat %s, lon %s) ===\n", CIUDAD, LATITUD, LONGITUD);
  Serial.println(F("=== APIs: Open-Meteo (principal) + MET Norway (respaldo) ==="));
  Serial.printf("[salud] Motivo del ultimo reinicio : %s\n", motivoReinicio());
  Serial.printf("[salud] Arranques seguidos        : %u%s\n",
                (unsigned)arranquesSeguidos,
                enBucleDeReinicios ? "   <<<< BUCLE DE REINICIOS" : "   (normal)");
  if (esp_reset_reason() == ESP_RST_BROWNOUT) {
    Serial.println(F("[salud] CONFIRMADO falta de corriente. Soluciones:"));
    Serial.println(F("[salud]   1) fuente de 5 V / 2 A (no el USB del PC, da ~500 mA)"));
    Serial.println(F("[salud]   2) condensador de 470-1000 uF entre 5V y GND"));
    Serial.println(F("[salud]   3) bajar BRILLO"));
  }
  Serial.println(F("=== nya~ (=^..^=) ===\n"));

  configurarPantalla();

  /* Arrancar con la matriz CASI APAGADA (ver BRILLO_ARRANQUE en config.h).
     Si venimos de un brownout, esto baja muchisimo el consumo durante el
     arranque y le deja salir solo del bucle de reinicios. */
  pantalla.setIntensity(BRILLO_ARRANQUE);

  mostrarYEsperar("ESTACION METEOROLOGICA");
  pantalla.displayText("CONECTANDO WIFI...", PA_CENTER, 35, 400, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  conectarWiFi();
  esperarAnimacion(30000UL);

  if (WiFi.status() == WL_CONNECTED) {
    sincronizarHora();

    char ipTxt[48];
    snprintf(ipTxt, sizeof(ipTxt), "IP %s", WiFi.localIP().toString().c_str());
    mostrarYEsperar(ipTxt);
  } else {
    mostrarYEsperar("SIN WIFI - REVISA secrets.h");
  }

  // Primera consulta al clima
  pantalla.displayText("PIDIENDO EL CLIMA...", PA_CENTER, 35, 400, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  obtenerClima(clima);
  esperarAnimacion(30000UL);

#if SERVIDOR_WEB
  if (WiFi.status() == WL_CONNECTED) configurarWeb();
#endif

  /* Ya arrancado y con el WiFi tranquilo: AHORA el brillo de verdad.
     Este es el momento de menos corriente de todo el ciclo. */
  pantalla.setIntensity(BRILLO);

  tUltimaConsulta = millis();
  primeraConsulta = false;
  mostrarActual();
}


/* =====================================================================
 *  LOOP
 * ===================================================================== */
void loop() {

  /* ---- 0. ¿Llevamos ya un minuto entero en marcha? -----------------
   *     Entonces NO estamos en un bucle de reinicios, asi que se limpia
   *     el contador (que vive en la memoria RTC y sobrevive a los
   *     reinicios). Es lo que distingue "se reinicio una vez" de
   *     "lleva media hora reiniciandose sin parar".
   */
  if (arranquesSeguidos != 0 && millis() > 60000UL) {
    marcarArranqueSano();       // 1 min bien: se borra el contador de la flash
    arranquesSeguidos = 0;
  }

#if SERVIDOR_WEB
  servidor.handleClient();
#endif

  /* ---- 1. Reconectar el WiFi si se cayo ---- */
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long tReintento = 0;
    if (millis() - tReintento > 15000UL) {
      tReintento = millis();
      Serial.println(F("[wifi] Conexion perdida, reintentando..."));
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }

  /* ---- 1b. La hora ----
   *     sincronizarHora() PARA el SNTP despues de cada sincronizacion (es
   *     obligatorio, ver el comentario de alli), asi que hay que reponer
   *     la hora de vez en cuando.
   *
   *     Con red floja el NTP puede fallar justo en el arranque. En ese caso
   *     NO queremos esperar 24 h con el reloj sin poner: mientras no haya
   *     una hora valida se reintenta cada RESINCRONIZAR_FALLO_MIN minutos.
   *     (horaLocal() devuelve false si el reloj aun no esta puesto.)
   */
  {
    char pruebaHora[12];
    const bool yaTenemosHora = horaLocal(pruebaHora, sizeof(pruebaHora));

    static unsigned long tUltimoNtp = 0;
    const unsigned long esperaNtp = yaTenemosHora
        ? (unsigned long)RESINCRONIZAR_HORAS * 3600000UL
        : (unsigned long)RESINCRONIZAR_FALLO_MIN * 60000UL;

    if (WiFi.status() == WL_CONNECTED && (millis() - tUltimoNtp) >= esperaNtp) {
      tUltimoNtp = millis();
      sincronizarHora();
    }
  }

  /* ---- 2. Pedir datos nuevos ----
   *     Cuanto esperar lo decide weather.h en planificarProximaConsulta():
   *     usa INTERVALO_MIN si todo va bien e INTERVALO_ERROR_MIN si algo
   *     fallo, y ademas le suma el desfase aleatorio (jitter) y el castigo
   *     por 429 que piden las condiciones de uso de MET Norway.
   */
  bool datosNuevos = false;
  if (primeraConsulta || (millis() - tUltimaConsulta) >= esperaProximaMs()) {
    if (WiFi.status() == WL_CONNECTED) {
      tUltimaConsulta = millis();
      primeraConsulta = false;
      datosNuevos = obtenerClima(clima);
    }
  }

  /* ---- 3. Animar la matriz ---- */
  if (datosNuevos) {
    mostrarActual();                 // refresca ya mismo con los datos nuevos
  } else if (pantalla.displayAnimate()) {
    mostrarActual();                 // se acabo una vuelta: empieza otra
  }

  delay(2);                          // deja respirar al procesador
}
