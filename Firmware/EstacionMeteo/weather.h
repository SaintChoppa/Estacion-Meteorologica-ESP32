/* =====================================================================
 *  weather.h  -  Clima con RESPALDO ENTRE DOS PROVEEDORES
 * ---------------------------------------------------------------------
 *    1) Open-Meteo   (principal)  - sin API key, ~0.35 s
 *    2) MET Norway   (respaldo)   - gobierno noruego, ~1.1 s
 *
 *  Datos: temperatura, humedad, sensacion termica, viento (velocidad y
 *         direccion) y estado del cielo.
 *
 *  ------------------------------------------------------------------
 *  BUG ARREGLADO (2026-10-08) - IMPORTANTE, no volver a cometerlo:
 *
 *  Open-Meteo responde con "Transfer-Encoding: chunked", pero
 *  HTTPClient SOLO decodifica el troceado en writeToStream(), o sea en
 *  getString(). getStreamPtr() devuelve el socket TCP CRUDO, asi que
 *  ArduinoJson recibia los marcadores de troceado ("1f0\r\n{...}\r\n0")
 *  y producia un documento valido pero SIN los datos: el sintoma era
 *  "falta el bloque 'current'" sin ningun error de parseo.
 *
 *  REGLA: si hay Content-Length (getSize() >= 0) se puede leer el
 *  stream crudo (ahorra memoria, MET Norway manda 39 KB). Si la
 *  respuesta es chunked (getSize() == -1) HAY QUE usar getString().
 *  ------------------------------------------------------------------
 *
 *  OJO CON LAS UNIDADES DEL VIENTO:
 *    - Open-Meteo lo da en km/h por defecto.
 *    - MET Norway lo da en m/s.
 *  Internamente SIEMPRE se guarda en km/h (campo vientoVel) y se
 *  convierte solo al mostrarlo (valorViento()).
 * =====================================================================
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "secrets.h"
#include "root_ca.h"

/* ------------------------------------------------------------------
 *  De donde vino el dato
 * ------------------------------------------------------------------ */
enum FuenteClima : uint8_t {
  FUENTE_NINGUNA = 0,
  FUENTE_OPEN_METEO,
  FUENTE_MET_NO
};

/* ------------------------------------------------------------------
 *  Resultado de UNA consulta a UN proveedor
 * ------------------------------------------------------------------ */
enum ResultadoClima : int8_t {
  RES_FALLO       = -1,
  RES_SIN_CAMBIOS =  0,   // 304: los datos siguen valiendo, no se rebajan
  RES_NUEVO       =  1
};

/* ------------------------------------------------------------------
 *  Ultima lectura del clima
 * ------------------------------------------------------------------ */
struct Clima {
  bool          ok           = false;
  bool          obsoleto     = false;
  float         temp         = NAN;     // grados Celsius
  float         humedad      = NAN;     // porcentaje 0-100
  float         sensacion    = NAN;     // grados Celsius
  float         vientoVel    = NAN;     // SIEMPRE en km/h (ver cabecera)
  float         vientoDir    = NAN;     // grados, 0 = Norte, 90 = Este
  int           codigo       = -1;      // codigo WMO (solo Open-Meteo)
  const char   *descripcion  = "SIN DATOS";
  const char   *proveedor    = "---";
  FuenteClima   fuente       = FUENTE_NINGUNA;
  unsigned long actualizado  = 0;
};

/* ------------------------------------------------------------------
 *  Viento: la unidad que se muestra la decide config.h
 * ------------------------------------------------------------------ */
#if VIENTO_EN_KMH
  #define UNIDAD_VIENTO "KM/H"
  inline float valorViento(float kmh) { return kmh; }        // ya viene en km/h
#else
  #define UNIDAD_VIENTO "M/S"
  inline float valorViento(float kmh) { return kmh / 3.6f; } // de km/h a m/s
#endif


/* ==================================================================
 *  ESTADO INTERNO (funciones con variable estatica: seguro en C++11+)
 * ================================================================== */
inline unsigned long &esperaProximaMs()      { static unsigned long v = 1000UL; return v; }
inline unsigned long &castigoPor429Ms()      { static unsigned long v = 0;      return v; }
inline String        &metnoLastModified()    { static String v;                 return v; }
inline bool          &semillaAleatoriaLista(){ static bool v = false;           return v; }


/* ==================================================================
 *  DIAGNOSTICO
 * ================================================================== */
inline void logHeap(const char *etiqueta) {
  Serial.printf("[heap] %-24s libre=%6u  minimo=%6u  bloque_mayor=%6u\n",
                etiqueta,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMinFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
}


/* ==================================================================
 *  TRADUCTORES
 * ================================================================== */

/* Codigos WMO que usa Open-Meteo */
inline const char *describirWMO(int c) {
  switch (c) {
    case 0:            return "DESPEJADO";
    case 1:            return "MAYORMENTE DESPEJADO";
    case 2:            return "PARCIALMENTE NUBLADO";
    case 3:            return "NUBLADO";
    case 45:           return "NIEBLA";
    case 48:           return "NIEBLA CON ESCARCHA";
    case 51:           return "LLOVIZNA LEVE";
    case 53:           return "LLOVIZNA";
    case 55:           return "LLOVIZNA FUERTE";
    case 56:
    case 57:           return "LLOVIZNA HELADA";
    case 61:           return "LLUVIA LEVE";
    case 63:           return "LLUVIA";
    case 65:           return "LLUVIA FUERTE";
    case 66:
    case 67:           return "LLUVIA HELADA";
    case 71:           return "NIEVE LEVE";
    case 73:           return "NIEVE";
    case 75:           return "NIEVE FUERTE";
    case 77:           return "GRANOS DE NIEVE";
    case 80:           return "CHUBASCOS LEVES";
    case 81:           return "CHUBASCOS";
    case 82:           return "CHUBASCOS FUERTES";
    case 85:
    case 86:           return "CHUBASCOS DE NIEVE";
    case 95:           return "TORMENTA";
    case 96:
    case 99:           return "TORMENTA CON GRANIZADO";
    default:           return "CLIMA DESCONOCIDO";
  }
}

/* ------------------------------------------------------------------
 *  Grados (0 = Norte, 90 = Este) -> punto cardinal en espanol.
 *  Se usan 8 rumbos, que es lo legible en una pantalla de 8 pixeles.
 * ------------------------------------------------------------------ */
inline const char *direccionCardinal(float grados) {
  if (isnan(grados)) return "--";

  static const char *RUMBOS[8] = { "N", "NE", "E", "SE", "S", "SO", "O", "NO" };

  int i = (int)((grados + 22.5f) / 45.0f);   // +22.5 centra cada sector
  i %= 8;
  if (i < 0) i += 8;
  return RUMBOS[i];
}

/* ------------------------------------------------------------------
 *  Codigos "symbol_code" de MET Norway -> espanol.
 *  OJO CON EL ORDEN: se compara con strstr(), asi que las claves
 *  LARGAS van PRIMERO. "lightrainshowers" contiene "rainshowers", y
 *  "rainshowers" contiene "rain": si "rain" fuese antes, todo saldria
 *  como "LLUVIA".  Verificado con una prueba de 23 casos: 0 fallos.
 * ------------------------------------------------------------------ */
inline const char *describirMETNO(const char *simbolo) {
  if (simbolo == nullptr) return "VARIABLE";

  static const struct { const char *clave; const char *texto; } TABLA[] = {
    { "heavyrainandthunder",   "TORMENTA Y LLUVIA FUERTE" },
    { "rainandthunder",        "TORMENTA Y LLUVIA"        },
    { "heavysnowandthunder",   "TORMENTA Y NIEVE"         },
    { "snowandthunder",        "TORMENTA Y NIEVE"         },
    { "heavyrainshowers",      "CHUBASCOS FUERTES"        },
    { "lightrainshowers",      "CHUBASCOS LEVES"          },
    { "rainshowers",           "CHUBASCOS"                },
    { "heavyrainsleetshowers", "AGUANIEVE FUERTE"         },
    { "lightsleetshowers",     "AGUANIEVE LEVE"           },
    { "sleetshowers",          "AGUANIEVE"                },
    { "heavysnowshowers",      "NIEVE FUERTE"             },
    { "lightsnowshowers",      "NIEVE LEVE"               },
    { "snowshowers",           "NIEVE"                    },
    { "heavyrain",             "LLUVIA FUERTE"            },
    { "lightrain",             "LLUVIA LEVE"              },
    { "rain",                  "LLUVIA"                   },
    { "heavysleet",            "AGUANIEVE FUERTE"         },
    { "lightsleet",            "AGUANIEVE LEVE"           },
    { "sleet",                 "AGUANIEVE"                },
    { "heavysnow",             "NIEVE FUERTE"             },
    { "lightsnow",             "NIEVE LEVE"               },
    { "snow",                  "NIEVE"                    },
    { "partlycloudy",          "PARCIALMENTE NUBLADO"     },
    { "clearsky",              "DESPEJADO"                },
    { "cloudy",                "NUBLADO"                  },
    { "fair",                  "POCO NUBLADO"             },
    { "fog",                   "NIEBLA"                   },
    { "thunderstorm",          "TORMENTA"                 },
  };

  for (const auto &fila : TABLA) {
    if (strstr(simbolo, fila.clave) != nullptr) return fila.texto;
  }
  return "VARIABLE";
}

/* ------------------------------------------------------------------
 *  Sensacion termica (indice de calor, formula de Rothfusz).
 *  Importa mucho en Cartagena: 31 C con 80 % de humedad -> 40.9 C.
 * ------------------------------------------------------------------ */
inline float sensacionTermica(float tempC, float humedadPct) {
  if (isnan(tempC) || isnan(humedadPct)) return tempC;

  const float T = tempC * 9.0f / 5.0f + 32.0f;   // a Fahrenheit
  const float R = humedadPct;

  float hi;
  if (T >= 80.0f) {
    hi = -42.379f
         + 2.04901523f * T
         + 10.14333127f * R
         - 0.22475541f * T * R
         - 6.83783e-3f * T * T
         - 5.481717e-2f * R * R
         + 1.22874e-3f * T * T * R
         + 8.5282e-4f * T * R * R
         - 1.99e-6f * T * T * R * R;
  } else {
    hi = 0.5f * (T + 61.0f + ((T - 68.0f) * 1.2f) + (R * 0.094f));
  }
  return (hi - 32.0f) * 5.0f / 9.0f;
}


/* ==================================================================
 *  LA HORA, SIN PEDIRLA APARTE
 * ------------------------------------------------------------------
 *  Toda respuesta HTTP trae una cabecera "Date" con la hora UTC del
 *  servidor, precisa al segundo (los servidores van sincronizados por
 *  NTP). Como ya estamos descargando el clima, aprovecharla sale gratis:
 *
 *    - cero peticiones extra (ni SNTP, ni otro DNS, ni otro socket)
 *    - se refresca en CADA consulta, o sea cada 10 minutos
 *    - funciona aunque la red bloquee el NTP (UDP 123), cosa tipica en
 *      algunos puntos de acceso moviles
 *
 *  Y de paso se elimina SNTP, que era lo que provocaba el reinicio en
 *  bucle por reentrada en lwIP (ver el comentario de arriba del todo).
 * ================================================================== */
#include <sys/time.h>      // settimeofday()

/* "Thu, 08 Oct 2026 20:11:32 GMT"  ->  segundos desde 1970 (UTC).
   Se calcula a mano (algoritmo "dias-desde-civil") para no depender de
   timegm(), que no esta en todas las librerias. */
inline bool epochDesdeFechaHTTP(const char *fecha, time_t &epoch) {
  if (fecha == nullptr) return false;

  char mes[8] = {0};
  int dia = 0, anio = 0, hh = 0, mi = 0, ss = 0;
  if (sscanf(fecha, "%*3s, %d %3s %d %d:%d:%d",
             &dia, mes, &anio, &hh, &mi, &ss) != 6) {
    return false;
  }

  static const char *MESES = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *p = strstr(MESES, mes);
  if (p == nullptr) return false;
  const int m = (int)((p - MESES) / 3) + 1;          // 1 = enero
  if (m < 1 || m > 12 || dia < 1 || dia > 31) return false;

  int y = anio;
  y -= (m <= 2);
  const int era  = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u
                       + (unsigned)dia - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  const long dias = (long)era * 146097L + (long)doe - 719468L;

  epoch = (time_t)dias * 86400L + (long)hh * 3600L + (long)mi * 60L + ss;
  return true;
}

/* Aplica la hora de la cabecera Date. Devuelve true si se pudo poner. */
inline bool ponerHoraDesdeHTTP(HTTPClient &https) {
  if (!https.hasHeader("Date")) return false;

  time_t epoch;
  if (!epochDesdeFechaHTTP(https.header("Date").c_str(), epoch)) return false;

  // Descartar valores absurdos (servidor sin hora, proxy raro...)
  if (epoch < 1600000000L || epoch > 4102444800L) return false;   // 2020..2100

  // La zona horaria hay que fijarla ANTES de usar la hora local.
  // configTzTime() tambien la fija, pero ademas arranca SNTP, que es
  // justo lo que estamos evitando; aqui la fijamos a mano una sola vez.
  static bool zonaLista = false;
  if (!zonaLista) {
    setenv("TZ", TZ_POSIX, 1);
    tzset();
    zonaLista = true;
  }

  struct timeval tv;
  tv.tv_sec  = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  char texto[24];
  struct tm local;
  localtime_r(&epoch, &local);
  strftime(texto, sizeof(texto), "%d/%m %H:%M:%S", &local);
  Serial.printf("[hora] Puesta desde la respuesta HTTP: %s  (UTC epoch %ld)\n",
                texto, (long)epoch);
  return true;
}


/* ==================================================================
 *  LECTURA DEL CUERPO DE LA RESPUESTA
 * ------------------------------------------------------------------
 *  Aqui esta el arreglo del bug del "chunked". Ver la cabecera.
 * ================================================================== */
inline DeserializationError traerJSON(HTTPClient &https, JsonDocument &doc,
                                      JsonDocument &filtro, String &cuerpo,
                                      bool &porFlujo) {
  // La hora sale gratis de esta MISMA respuesta (cabecera Date).
  // Se hace aqui para cubrir los dos proveedores de una sola vez.
  ponerHoraDesdeHTTP(https);

  const int tamano = https.getSize();      // -1 = no hay Content-Length = chunked

  if (tamano >= 0) {
    WiFiClient *flujo = https.getStreamPtr();
    if (flujo != nullptr) {
      porFlujo = true;
      return deserializeJson(doc, *flujo, DeserializationOption::Filter(filtro));
    }
  }

  porFlujo = false;
  cuerpo = https.getString();              // esta SI decodifica el chunked
  return deserializeJson(doc, cuerpo, DeserializationOption::Filter(filtro));
}

/* Avisa del 429 y baja el ritmo (lo piden las condiciones de uso) */
inline void comprobar429(int codigoHTTP, const char *quien) {
  if (codigoHTTP == 429) {
    castigoPor429Ms() = (unsigned long)INTERVALO_429_MIN * 60000UL;
    Serial.printf("[clima] *** %s respondio 429 (demasiadas peticiones). "
                  "Bajo el ritmo %d min. ***\n", quien, INTERVALO_429_MIN);
  }
}

/* Deja apuntado cuanto hay que esperar para la proxima consulta, ya con
   el desfase aleatorio y el castigo por 429 aplicados. */
inline void planificarProximaConsulta(bool fueBien) {
  if (!semillaAleatoriaLista()) {          // el jitter necesita azar de verdad
    randomSeed(esp_random());
    semillaAleatoriaLista() = true;
  }
  if (fueBien) castigoPor429Ms() = 0;      // todo va bien: quitar el castigo

  unsigned long espera = (fueBien ? (unsigned long)INTERVALO_MIN
                                  : (unsigned long)INTERVALO_ERROR_MIN) * 60000UL;
  espera += castigoPor429Ms();

  const long rango = (long)JITTER_MAX_S * 1000L;
  if (rango > 0) espera += (unsigned long)random(0, rango);

  esperaProximaMs() = espera;
  Serial.printf("[clima] Proxima consulta en %lu min %02lu s\n",
                espera / 60000UL, (espera % 60000UL) / 1000UL);
}


/* ==================================================================
 *  PROVEEDOR 1 : OPEN-METEO
 * ------------------------------------------------------------------
 *  intentarOpenMeteo() hace UNA consulta; obtenerOpenMeteo() (mas abajo)
 *  la reintenta una vez. Importa mucho en una red floja: Open-Meteo manda
 *  solo ~600 bytes, asi que reintentarlo es baratisimo comparado con bajar
 *  los 40 KB del respaldo. Un fallo de TLS pasajero no debe mandarnos
 *  directos al respaldo (que es lo que paso el 2026-10-08 con -72 dBm).
 * ================================================================== */
inline ResultadoClima intentarOpenMeteo(Clima &c) {
  WiFiClientSecure cliente;
#if TLS_INSEGURO
  cliente.setInsecure();
#else
  cliente.setCACert(CA_OPEN_METEO);
#endif
  cliente.setHandshakeTimeout(20);

  // Ojo: Open-Meteo da el viento en km/h por defecto (que es justo lo que
  // guardamos nosotros), asi que no hace falta pedirle unidades.
  String url = String(F("https://api.open-meteo.com/v1/forecast?latitude=")) + LATITUD +
               F("&longitude=") + LONGITUD +
               F("&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
                 "weather_code,wind_speed_10m,wind_direction_10m") +
               F("&timezone=auto");

  HTTPClient https;
  https.setConnectTimeout(12000);   // red floja: mejor esperar que fallar
  https.setTimeout(20000);

  // TRAMPA del core 3.x: _currentHeaders nace VACIO, y en el parseo
  // (HTTPClient.cpp:1322) solo se rellenan las claves que esten en esa lista.
  // O sea: hasHeader("Date") da SIEMPRE false si no has pedido "Date" tu.
  // (En el core 2.x habia una lista por defecto; en el 3.x ya no.)
  // No rompe la deteccion del chunked: Content-Length va a _size y
  // Transfer-Encoding a otra variable, aparte de _currentHeaders.
  static const char *cabecerasOM[] = { "Date" };
  https.collectHeaders(cabecerasOM, 1);

  if (!https.begin(cliente, url)) {
    Serial.println(F("[clima] Open-Meteo: no se pudo iniciar la peticion."));
    return RES_FALLO;
  }

  const int codigoHTTP = https.GET();
  comprobar429(codigoHTTP, "Open-Meteo");

  if (codigoHTTP != HTTP_CODE_OK) {
    char err[160] = {0};
    cliente.lastError(err, sizeof(err));
    Serial.printf("[clima] Open-Meteo HTTP %d  (TLS: %s)\n",
                  codigoHTTP, err[0] ? err : "-");
    https.end();
    return RES_FALLO;
  }

  JsonDocument filtro;
  JsonObject fActual = filtro["current"].to<JsonObject>();
  fActual["temperature_2m"]       = true;
  fActual["relative_humidity_2m"] = true;
  fActual["apparent_temperature"] = true;
  fActual["weather_code"]         = true;
  fActual["wind_speed_10m"]       = true;
  fActual["wind_direction_10m"]   = true;

  JsonDocument doc;
  String cuerpo;
  bool porFlujo = false;
  DeserializationError error = traerJSON(https, doc, filtro, cuerpo, porFlujo);

  Serial.printf("[clima] Open-Meteo: %s, %d bytes\n",
                porFlujo ? "Content-Length (stream)" : "chunked (getString)",
                porFlujo ? https.getSize() : (int)cuerpo.length());

  https.end();

  if (error) {
    Serial.printf("[clima] Open-Meteo: JSON invalido (%s)\n", error.c_str());
    if (!porFlujo && cuerpo.length() > 0) {
      Serial.printf("[clima]   respuesta: %s\n", cuerpo.substring(0, 200).c_str());
    }
    if (error == DeserializationError::NoMemory) logHeap("tras NoMemory");
    return RES_FALLO;
  }

  JsonObject actual = doc["current"];
  if (actual.isNull()) {
    Serial.println(F("[clima] Open-Meteo: falta el bloque 'current'."));
    if (!porFlujo && cuerpo.length() > 0) {
      Serial.printf("[clima]   respuesta: %s\n", cuerpo.substring(0, 200).c_str());
    }
    return RES_FALLO;
  }

  c.temp        = actual["temperature_2m"].as<float>();
  c.humedad     = actual["relative_humidity_2m"].as<float>();
  c.sensacion   = actual["apparent_temperature"].as<float>();
  c.vientoVel   = actual["wind_speed_10m"].as<float>();        // ya en km/h
  c.vientoDir   = actual["wind_direction_10m"].as<float>();    // grados
  c.codigo      = actual["weather_code"].as<int>();
  c.descripcion = describirWMO(c.codigo);
  c.proveedor   = "Open-Meteo";
  c.fuente      = FUENTE_OPEN_METEO;
  return RES_NUEVO;
}


/* Reintenta una vez: en una red floja un fallo de TLS es de lo mas normal,
   y Open-Meteo solo manda ~600 bytes, asi que insistir sale casi gratis. */
inline ResultadoClima obtenerOpenMeteo(Clima &c) {
  for (int intento = 1; intento <= 2; ++intento) {
    const ResultadoClima r = intentarOpenMeteo(c);
    if (r != RES_FALLO) return r;

    if (intento < 2) {
      Serial.println(F("[clima] Open-Meteo fallo. Reintento en 2 s..."));
      delay(2000);
      yield();
    }
  }
  return RES_FALLO;
}


/* ==================================================================
 *  PROVEEDOR 2 : MET NORWAY  (respaldo)
 * ------------------------------------------------------------------
 *  Cumple sus condiciones de uso:
 *    - User-Agent con contacto real (METNO_USER_AGENT en secrets.h)
 *    - If-Modified-Since con el Last-Modified anterior -> 304 barato
 *    - HTTPS obligatorio (ya)
 *    - Coordenadas a 4 decimales maximo (ya)
 *
 *  intentarMetNo() hace UNA consulta; obtenerMetNo() (justo debajo) la
 *  reintenta una vez, porque los fallos de handshake TLS suelen ser
 *  transitorios.
 * ================================================================== */
inline ResultadoClima intentarMetNo(Clima &c) {
  WiFiClientSecure cliente;
#if TLS_INSEGURO
  cliente.setInsecure();
#else
  cliente.setCACert(CA_MET_NO);        // HARICA, NO Let's Encrypt
#endif
  cliente.setHandshakeTimeout(25);

  // Comprobar el DNS ANTES de conectar: si el nombre no se resuelve, el
  // error que se ve despues ("conexion rechazada") es enganoso.
  {
    IPAddress ip;
    if (WiFi.hostByName("api.met.no", ip)) {
      Serial.printf("[clima] DNS api.met.no -> %s\n", ip.toString().c_str());
    } else {
      Serial.println(F("[clima] DNS: no se pudo resolver api.met.no"));
      return RES_FALLO;
    }
  }

  String url = String(F("https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=")) +
               LATITUD + F("&lon=") + LONGITUD;

  HTTPClient https;
  https.setConnectTimeout(12000);
  https.setTimeout(30000);   // 40 KB por una red de -72 dBm: hay que dar margen
  https.setUserAgent(METNO_USER_AGENT);
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // lo exige su ToS

  // Nos hace falta leer dos cabeceras de su respuesta (por defecto HTTPClient
  // no las recoge). "Date" es para sacar la hora de aqui mismo en vez de
  // arrancar SNTP. Content-Length y Transfer-Encoding se leen aparte, asi que
  // esto NO rompe la deteccion del chunked.
  static const char *cabeceras[] = { "Last-Modified", "Date" };
  https.collectHeaders(cabeceras, 2);

  // "Cache data locally and use the If-Modified-Since request header to
  //  avoid repeatedly downloading the same data"  (condiciones de uso)
  if (metnoLastModified().length() > 0) {
    https.addHeader("If-Modified-Since", metnoLastModified());
    Serial.printf("[clima] MET Norway: If-Modified-Since %s\n",
                  metnoLastModified().c_str());
  }

  if (!https.begin(cliente, url)) {
    Serial.println(F("[clima] MET Norway: no se pudo iniciar la peticion."));
    return RES_FALLO;
  }

  const int codigoHTTP = https.GET();
  comprobar429(codigoHTTP, "MET Norway");

  if (codigoHTTP == HTTP_CODE_NOT_MODIFIED) {          // 304
    https.end();
    Serial.println(F("[clima] MET Norway: 304, los datos siguen valiendo."));
    return RES_SIN_CAMBIOS;
  }

  if (codigoHTTP != HTTP_CODE_OK) {
    char err[160] = {0};
    cliente.lastError(err, sizeof(err));
    Serial.printf("[clima] MET Norway HTTP %d  (TLS: %s)\n",
                  codigoHTTP, err[0] ? err : "-");
    https.end();
    return RES_FALLO;
  }

  if (https.hasHeader("Last-Modified")) {
    metnoLastModified() = https.header("Last-Modified");
  }

  JsonDocument filtro;
  JsonObject fEntrada = filtro["properties"]["timeseries"][0].to<JsonObject>();
  JsonObject fDetalle = fEntrada["data"]["instant"]["details"].to<JsonObject>();
  fDetalle["air_temperature"]     = true;
  fDetalle["relative_humidity"]   = true;
  fDetalle["wind_speed"]          = true;   // m/s
  fDetalle["wind_from_direction"] = true;   // grados
  fEntrada["data"]["next_1_hours"]["summary"]["symbol_code"] = true;

  JsonDocument doc;
  String cuerpo;
  bool porFlujo = false;
  DeserializationError error = traerJSON(https, doc, filtro, cuerpo, porFlujo);

  Serial.printf("[clima] MET Norway: %s, %d bytes\n",
                porFlujo ? "Content-Length (stream)" : "chunked (getString)",
                porFlujo ? https.getSize() : (int)cuerpo.length());

  https.end();

  if (error) {
    Serial.printf("[clima] MET Norway: JSON invalido (%s)\n", error.c_str());
    if (!porFlujo && cuerpo.length() > 0) {
      Serial.printf("[clima]   respuesta: %s\n", cuerpo.substring(0, 200).c_str());
    }
    if (error == DeserializationError::NoMemory) logHeap("tras NoMemory");
    return RES_FALLO;
  }

  JsonObject entrada = doc["properties"]["timeseries"][0];
  if (entrada.isNull()) {
    Serial.println(F("[clima] MET Norway: serie temporal vacia."));
    if (!porFlujo && cuerpo.length() > 0) {
      Serial.printf("[clima]   respuesta: %s\n", cuerpo.substring(0, 200).c_str());
    }
    return RES_FALLO;
  }

  JsonObject detalle = entrada["data"]["instant"]["details"];

  c.temp        = detalle["air_temperature"].as<float>();
  c.humedad     = detalle["relative_humidity"].as<float>();
  c.sensacion   = sensacionTermica(c.temp, c.humedad);
  c.vientoVel   = detalle["wind_speed"].as<float>() * 3.6f;   // m/s -> km/h
  c.vientoDir   = detalle["wind_from_direction"].as<float>();
  c.codigo      = -1;
  c.descripcion = describirMETNO(
      entrada["data"]["next_1_hours"]["summary"]["symbol_code"].as<const char *>());
  c.proveedor   = "MET Norway";
  c.fuente      = FUENTE_MET_NO;
  return RES_NUEVO;
}

/* Reintenta una vez: un fallo de handshake TLS suele ser transitorio */
inline ResultadoClima obtenerMetNo(Clima &c) {
  for (int intento = 1; intento <= 2; ++intento) {
    const ResultadoClima r = intentarMetNo(c);
    if (r != RES_FALLO) return r;

    if (intento < 2) {
      Serial.println(F("[clima] MET Norway fallo. Reintento en 2 s..."));
      delay(2000);
      yield();
    }
  }
  return RES_FALLO;
}


/* ==================================================================
 *  ORQUESTADOR: prueba Open-Meteo y, si falla, MET Norway
 * ================================================================== */
inline bool obtenerClima(Clima &c) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[clima] Sin conexion WiFi, no se consulta."));
    if (c.fuente != FUENTE_NINGUNA) c.obsoleto = true;
    c.ok = false;
    planificarProximaConsulta(false);
    return false;
  }

  logHeap("antes de consultar");

  // Se trabaja sobre una copia: si algo falla a medias NO se destruye la
  // ultima lectura buena que ya estaba en pantalla.
  Clima nuevo = c;

  ResultadoClima r = obtenerOpenMeteo(nuevo);

  if (r == RES_NUEVO) {
    nuevo.ok          = true;
    nuevo.obsoleto    = false;
    nuevo.actualizado = millis();
    c = nuevo;
    Serial.printf("[clima] OK (Open-Meteo) -> %.1f C  %.0f%%  sens %.1f C  "
                  "viento %.1f %s %s  (%s)\n",
                  c.temp, c.humedad, c.sensacion,
                  valorViento(c.vientoVel), UNIDAD_VIENTO,
                  direccionCardinal(c.vientoDir), c.descripcion);
    logHeap("tras Open-Meteo");
    planificarProximaConsulta(true);
    return true;
  }

  Serial.println(F("[clima] Open-Meteo no respondio."));

#if USAR_RESPALDO
  Serial.println(F("[clima] Probando el respaldo (MET Norway)..."));
  const ResultadoClima rm = obtenerMetNo(nuevo);

  if (rm == RES_NUEVO || rm == RES_SIN_CAMBIOS) {
    nuevo.ok          = true;
    nuevo.obsoleto    = false;
    nuevo.actualizado = millis();
    c = nuevo;
    if (rm == RES_NUEVO) {
      Serial.printf("[clima] OK (MET Norway) -> %.1f C  %.0f%%  sens %.1f C  "
                    "viento %.1f %s %s  (%s)\n",
                    c.temp, c.humedad, c.sensacion,
                    valorViento(c.vientoVel), UNIDAD_VIENTO,
                    direccionCardinal(c.vientoDir), c.descripcion);
    } else {
      Serial.println(F("[clima] MET Norway confirmo los datos que ya teniamos (304)."));
    }
    logHeap("tras MET Norway");
    planificarProximaConsulta(true);
    return true;
  }

  Serial.println(F("[clima] MET Norway tampoco respondio."));
#endif

  // Los dos fallaron: se conserva lo ultimo bueno, marcado como viejo
  c.ok = false;
  if (c.fuente != FUENTE_NINGUNA) {
    c.obsoleto = true;
    Serial.printf("[clima] Sin datos nuevos. Ultima lectura hace %lu min.\n",
                  (millis() - c.actualizado) / 60000UL);
  } else {
    Serial.println(F("[clima] No hay ningun dato disponible."));
  }
  planificarProximaConsulta(false);
  logHeap("tras el fallo");
  return false;
}
