/* =====================================================================
 *  config.h  -  Ajustes de la Estacion Meteorologica
 * ---------------------------------------------------------------------
 *  Aqui van TODOS los valores que quieres tocar sin romper el codigo.
 * =====================================================================
 */
#pragma once

/* ------------------------------------------------------------------
 * 1. UBICACION  (aqui se calcula el clima de tu ciudad)
 * ------------------------------------------------------------------
 *  Cartagena de Indias, Bolivar (Colombia). 914.552 habitantes.
 *  Si te mudas, cambia solo estos 2 numeros.
 *  Los puedes sacar de Google Maps: clic derecho -> copiar coordenadas.
 * ------------------------------------------------------------------ */
#define CIUDAD          "CARTAGENA"
#define LATITUD         "10.3982"
#define LONGITUD        "-75.4933"
#define TZ_POSIX        "<-05>5"          // zona horaria (Colombia = UTC-5, sin horario de verano)

/* ------------------------------------------------------------------
 * 2. PANTALLA MAX7219
 * ------------------------------------------------------------------ */
#define HARDWARE_TYPE   MD_MAX72XX::FC16_HW   // Este es el tipo de los modulos 1088AB 4-en-1
#define NUM_MODULOS     8                     // Cuantas matrices 8x8 hay encadenadas
                                              // (2 placas de 4-en-1 = 8 modulos = 64 columnas)
#define BRILLO          3                     // 0 (apagado) a 15 (maximo)

/* ---- Pines - ESP32 (SPI por hardware, estos son los pines por defecto del ESP32)
 *      MAX7219  ->  ESP32
 *      VCC      ->  VIN  (5V, NO 3V3)
 *      GND      ->  GND
 *      DIN      ->  GPIO 23
 *      CS       ->  GPIO 5
 *      CLK      ->  GPIO 18
 * ------------------------------------------------------------------ */
#define PIN_CS          5
#define PIN_DIN         23
#define PIN_CLK         18

/* ------------------------------------------------------------------
 * 3. COMO SE MUESTRA LA INFORMACION
 * ------------------------------------------------------------------
 *  El orden de los datos en la cinta es fijo (en el .ino):
 *     Hora -> Temperatura -> Sensacion -> Humedad -> Viento (vel) -> Viento (dir)
 * ------------------------------------------------------------------ */
#define MODO_PAGINAS    0          // 0 = cinta continua | 1 = una pagina por dato
#define VELOCIDAD_SCROLL 55        // ms por columna. MENOR = mas rapido.
                                   //   30 = rapido | 55 = tranquilo (actual) | 90 = muy lento
#define PAUSA_TEXTO     1200       // ms de pausa entre repeticiones

/* ---- Formato de la hora ---- */
#define HORA_24H        0          // 0 = 12 horas (2:35 PM) | 1 = 24 horas (14:35)

/* ---- Unidad del viento ---- */
#define VIENTO_EN_KMH   1          // 1 = km/h | 0 = m/s

/* ------------------------------------------------------------------
 * 4. RED Y FRECUENCIA DE ACTUALIZACION
 * ------------------------------------------------------------------ */
#define INTERVALO_MIN       10     // minutos entre consultas cuando TODO va bien
                                   // (Open-Meteo/MET Norway se actualizan cada 15 min)

#define INTERVALO_ERROR_MIN 2      // minutos entre reintentos si la consulta falla
                                   // asi se recupera rapido si se cae internet o la API

#define WIFI_TIMEOUT_MS     20000  // cuanto esperar a que conecte el WiFi antes de rendirse

/* ------------------------------------------------------------------
 * 4b. BUENA CIUDADANIA CON LAS APIs  (esto evita que te bloqueen)
 * ------------------------------------------------------------------
 *  MET Norway pide expresamente en sus condiciones:
 *    - "Add a random number of minutes to the time of the requests"
 *      -> no pedir siempre en el mismo instante exacto.
 *    - "Always check response Status headers and limit traffic
 *       immediately" si devuelven 429 (demasiadas peticiones).
 * ------------------------------------------------------------------ */
#define JITTER_MAX_S        90     // desfase aleatorio de 0 a 90 s en cada consulta
#define INTERVALO_429_MIN   30     // si una API responde 429, esperar 30 min antes de insistir

/* ------------------------------------------------------------------
 * 4c. PILA DE MEMORIA DE LA TAREA PRINCIPAL
 * ------------------------------------------------------------------
 *  Arduino da 8 KB de pila a la tarea donde corre setup() y loop().
 *  Un handshake TLS con certificados RSA-4096 (los de HARICA, que usa
 *  MET Norway) consume bastante mas pila que uno ECDSA (el de
 *  Open-Meteo, que es mucho mas ligero): eso explicaria por que una API
 *  conectaba y la otra no.
 *
 *  Este valor es SIEMPRE una concesion: quitarsela al heap para darsela
 *  a la pila. Con 277 KB libres, 24 KB es un precio razonable.
 *  Si algun dia te quedas sin memoria en el parseo, bajalo a 12288.
 * ------------------------------------------------------------------ */
#define PILA_LOOP_BYTES     24576

/* ------------------------------------------------------------------
 * 4d. RELOJ POR INTERNET (NTP)
 * ------------------------------------------------------------------
 *  Dos servidores da mas posibilidades de acertar.
 *  OJO: el orden en que se pide la hora y el clima IMPORTA mucho.
 *  Ver el comentario largo en sincronizarHora(), en el .ino: si NTP
 *  deja una consulta DNS a medias y luego se resuelve otro nombre, el
 *  ESP32 se reinicia en bucle.
 * ------------------------------------------------------------------ */
#define SERVIDOR_NTP_1      "pool.ntp.org"
#define SERVIDOR_NTP_2      "time.nist.gov"
#define ESPERA_NTP_MS       20000UL   // maximo a esperar la respuesta de NTP
#define RESINCRONIZAR_HORAS 24        // cada cuantas horas se vuelve a pedir la hora
#define RESINCRONIZAR_FALLO_MIN 5     // si el NTP FALLO, reintentar cada 5 min
                                      // (con red floja es normal que falle al arrancar)

/* ------------------------------------------------------------------
 * 4e. CONSUMO DEL WIFI  (importante con muchos LEDs)
 * ------------------------------------------------------------------
 *  El modulo WiFi es, de lejos, lo que mas corriente pide del ESP32, y
 *  los picos coinciden con el handshake TLS... justo cuando mas falla.
 *  Si la alimentacion va justa (muchos LEDs + puerto USB del PC), esos
 *  picos hacen caer la tension y el TLS se cae con "Generic error".
 *
 *    0 = dejar que el WiFi duerma entre balizas -> MENOS consumo
 *        (unas 5x menos en reposo). RECOMENDADO: consultamos cada
 *        10 minutos, asi que la latencia da exactamente igual.
 *    1 = WiFi siempre despierto -> mas consumo, menos latencia.
 * ------------------------------------------------------------------ */
#define WIFI_SIN_DORMIR 0

/* ------------------------------------------------------------------
 * 4f. DE DONDE SACA LA HORA
 * ------------------------------------------------------------------
 *  0 = de la cabecera "Date" de la respuesta del clima (RECOMENDADO).
 *      Es GRATIS: esa informacion viaja en la MISMA descarga que ya
 *      hacemos cada 10 minutos, con precision de segundo, y se refresca
 *      sola en cada consulta. No gasta ninguna peticion extra, no
 *      arranca SNTP y ademas funciona en redes que bloquean el NTP
 *      (UDP 123), cosa tipica en algunos puntos de acceso moviles.
 *  1 = por NTP (pool.ntp.org), como antes: peticion aparte + SNTP.
 * ------------------------------------------------------------------ */
#define HORA_POR_NTP 0

/* ------------------------------------------------------------------
 * 4g. BRILLO DURANTE EL ARRANQUE  (importante si la corriente va justa)
 * ------------------------------------------------------------------
 *  El peor momento para la alimentacion es EL ARRANQUE: el WiFi tira
 *  picos enormes al asociarse Y la matriz esta encendida. Si el equipo
 *  se queda sin corriente justo ahi, se reinicia con la matriz ya
 *  encendida -> mas consumo -> se vuelve a reiniciar... y se queda en
 *  bucle con TODOS los LEDs encendidos (lo que mas consume).
 *
 *  Y el boton EN NO lo arregla: el MAX7219 no tiene linea de reset, asi
 *  que CONSERVA lo ultimo que le escribieron. El ESP32 se reinicia, pero
 *  la matriz se queda como estaba. Ese es exactamente el sintoma
 *  "pantalla llena y le doy a reset y no pasa nada".
 *
 *  Solucion: arrancar con la matriz casi apagada y subir el brillo SOLO
 *  cuando el WiFi y la primera consulta ya han terminado, que es cuando
 *  la corriente esta mas tranquila. Asi, aunque haya un brownout, el
 *  siguiente arranque empieza con el consumo minimo y SALE SOLO del bucle.
 *
 *    0      = la matriz no enseña nada durante el arranque (maxima proteccion)
 *    1..15  = brillo reducido durante el arranque (compromiso, 1 por defecto)
 * ------------------------------------------------------------------ */
#define BRILLO_ARRANQUE 1

/* ------------------------------------------------------------------
 * 5. RESPALDO AUTOMATICO ENTRE DOS APIs INDEPENDIENTES
 *    Con esto es MUY dificil quedarse sin datos:
 *      1) Open-Meteo   (principal, rapida)
 *      2) MET Norway   (respaldo: servicio meteorologico del gobierno noruego,
 *                       en linea desde hace mas de 15 años)
 *    Si Open-Meteo falla, el ESP32 prueba MET Norway en la misma pasada.
 *    Ponlo en 0 para usar solo Open-Meteo.
 * ------------------------------------------------------------------ */
#define USAR_RESPALDO       1

/* ------------------------------------------------------------------
 * 6. SEGURIDAD TLS
 *    1 = no valida el certificado (funciona siempre, mas simple)
 *    0 = valida contra el certificado raiz de cada proveedor
 *        (Let's Encrypt para Open-Meteo, HARICA para MET Norway)
 * ------------------------------------------------------------------ */
#define TLS_INSEGURO    1

/* ------------------------------------------------------------------
 * 7. SERVIDOR WEB DEL ESP32
 *    Ponlo en 0 si no quieres la pagina web y quieres ahorrar memoria.
 *    Con 1 puedes ver los datos desde el celular en http://<IP-del-ESP32>
 * ------------------------------------------------------------------ */
#define SERVIDOR_WEB    1
#define PUERTO_WEB      80

/* ------------------------------------------------------------------
 * 8. DEPURACION POR PUERTO SERIE
 * ------------------------------------------------------------------ */
#define BAUDIOS         115200
#define DEPURAR         1
