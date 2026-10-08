# 🌤️ Estación Meteorológica — ESP32 + Matriz MAX7219

> 🌐 **Idioma / Language:** **Español** · [English](README.en.md)

Muestra en una matriz de LEDs 8x8 (módulo 4-en-1) la **temperatura**, la
**humedad**, la **sensación térmica** y el **estado del cielo** de
**Cartagena de Indias**, consultados en tiempo real por WiFi.

> 📍 Cartagena de Indias, Bolívar (Colombia) — `10.3982, -75.4933`

```
┌──────────────────────────────────────────────────────────────┐
│  ESP32 ──WiFi──▶ 1º Open-Meteo   (principal)                │
│        └─WiFi──▶ 2º MET Norway   (respaldo si el 1º falla)   │
│    │                                                         │
│    ├──SPI──▶  Matriz MAX7219 8x8 x4  ──▶  texto en LEDs      │
│    └──WiFi─▶  mini página web (ver desde el celular)         │
└──────────────────────────────────────────────────────────────┘
```

---

## 📋 Índice

1. [Hardware y cableado](#1-hardware-y-cableado)
2. [Puesta en marcha (3 pasos)](#2-puesta-en-marcha)
3. [Las dos APIs y por qué](#3-las-dos-apis-y-por-qué)
4. [Cómo cambiar cosas (config.h)](#4-cómo-cambiar-cosas)
5. [Sin cables: la página web](#5-sin-cables-la-página-web)
6. [Problemas y soluciones](#6-problemas-y-soluciones)
7. [Qué hay instalado y dónde](#7-qué-hay-instalado-y-dónde)
8. [Ideas para ampliar](#8-ideas-para-ampliar)

---

## 1. Hardware y cableado

### Componentes

| Componente | Detalle |
|---|---|
| Microcontrolador | ESP32 (ESP-WROOM-32) sobre placa de expansión con borneras |
| Pantalla | **2 módulos MAX7219 4-en-1** ("1088AB") encadenados = **8 matrices 8x8 = 64x8 píxeles** |
| Alimentación | Cable USB al PC (5 V) |

### Cableado de dos placas encadenadas

Las dos placas van **en cadena** (una detrás de otra), no en paralelo:

```
ESP32                Placa 1 (4-en-1)          Placa 2 (4-en-1)
GPIO 23 ──DIN──▶  DIN ──[4 matrices]── DOUT ──DIN──▶ DIN ──[4 matrices]
GPIO 18 ──CLK──▶  CLK ─────────────────────────────▶ CLK
GPIO  5 ──CS ──▶  CS  ─────────────────────────────▶ CS
VIN     ──5V ──▶  VCC ─────────────────────────────▶ VCC
GND     ──────▶  GND ─────────────────────────────▶ GND
```

⚠️ **Lo importante:** la salida **DOUT** de la placa 1 va a la entrada **DIN** de la
placa 2. Si las conectas las dos directas al ESP32 verás las dos mostrando lo
mismo, o basura.

⚠️ **Y en `config.h`:** `NUM_MODULOS` tiene que ser **8** (2 placas × 4 matrices).
Si no coincide con lo que hay de verdad, el texto sale cortado o descolocado.


### Conexiones

La matriz tiene 5 pines. Conéctala **exactamente así**:

| Pin de la matriz | Pin del ESP32 | Por qué |
|---|---|---|
| **VCC** | **VIN** (5 V) | ⚠️ NO usar 3V3 — los LEDs se ven muy tenues o no funcionan |
| **GND** | **GND** | Masa común |
| **DIN** | **GPIO 23** | Datos (MOSI del SPI por hardware) |
| **CS**  | **GPIO 5**  | Selección del chip |
| **CLK** | **GPIO 18** | Reloj (SCK del SPI por hardware) |

> 💡 **Niveles lógicos:** el ESP32 saca 3,3 V y el MAX7219 espera 3,5 V en el pin
> de datos. En la práctica funciona perfectamente y así está montado este
> proyecto. Si algún día ves píxeles fantasma, pon un *level shifter* de
> 3,3 V → 5 V en las 3 líneas de datos.
>
> ⚡ **Consumo:** las 4 matrices a brillo alto pueden pedir ~1 A. Por USB y con
> brillo 3 (el configurado) va sobradísimo. Si subes el brillo, usa fuente de
> 5 V / 2 A en el VIN.

### Si la pantalla se ve rara

```c
#define HARDWARE_TYPE   MD_MAX72XX::FC16_HW
```

Casi todos los módulos 4-en-1 son `FC16_HW`. Si ves los LEDs desordenados o
espejados, prueba en este orden: `PAROLA_HW`, `GENERIC_HW`, `ICSTATION_HW`.

---

## 2. Puesta en marcha

### Paso 1 — Pon tu WiFi

Abre **`Firmware/EstacionMeteo/secrets.h`** y rellena:

```c
#define WIFI_SSID   "NOMBRE_DE_TU_WIFI"
#define WIFI_PASS   "TU_CONTRASEÑA"
```

⚠️ El ESP32 **solo funciona en WiFi de 2,4 GHz**.

### Paso 2 — Compilar

Doble clic en **`Herramientas\1-COMPILAR.cmd`**
→ debe terminar diciendo `*** COMPILADO CORRECTAMENTE ***`

### Paso 3 — Subir al ESP32 ⭐ (leer con atención)

Doble clic en **`Herramientas\2-SUBIR.cmd`** y sigue lo que diga:

> **Esta placa NO tiene circuito de auto-reset**, así que hay que meterla en
> "modo descarga" a mano:
>
> 1. **Mantén pulsado BOOT** (a veces se llama IO0) y no lo sueltes.
> 2. Sin soltar BOOT, **pulsa y suelta una vez EN** (a veces RST).
> 3. Sigue sin soltar BOOT y pulsa una tecla en la ventana.
> 4. Cuando veas `Writing at 0x...` **ya puedes soltar BOOT**.
> 5. Al terminar, si la placa no arranca sola, **pulsa y suelta EN**.

En el monitor serie (`Herramientas\3-MONITOR.cmd`) deberías ver:

```
=== ESTACION METEOROLOGICA - ESP32 + MAX7219 ===
=== Ciudad: CARTAGENA  (lat 10.3982, lon -75.4933) ===
=== APIs: Open-Meteo (principal) + MET Norway (respaldo) ===
[wifi] Conectado.  IP: 192.168.1.42   Senal: -58 dBm
[hora] Reloj sincronizado.
[clima] OK (Open-Meteo) -> 29.0 C  80%  sensacion 35.3 C  (LLOVIZNA LEVE)
[web] Servidor listo en http://192.168.1.42
```

Y la matriz mostrará:
`CARTAGENA   29.0~C   HUMEDAD 80%   SENSACION 35.3~C   LLOVIZNA LEVE   14:32`

---

## 3. Las dos APIs y por qué

Como la estación va a estar **fija en la sala**, no puede depender de un solo
servicio. El firmware usa **dos APIs independientes** y prueba la segunda
automáticamente si la primera falla **en la misma pasada**.

| | **Open-Meteo** (principal) | **MET Norway** (respaldo) |
|---|---|---|
| URL | api.open-meteo.com | api.met.no |
| ¿API key? | No | No |
| ¿Registro? | No | No |
| Quién está detrás | Proyecto open-source (AGPLv3), 30+ modelos | **El gobierno de Noruega** 🇳🇴 |
| Tiempo de respuesta | ~0,35 s | ~1,1 s |
| Da sensación térmica | Sí (ya calculada) | No → **la calculamos** con el índice de calor |
| Certificado TLS | Let's Encrypt (ISRG Root X1) | HARICA RootCA 2015 (CA griega) |

### Por qué estas dos y no otras

| API | Veredicto |
|---|---|
| **Open-Meteo** ✅ | Gratis, sin registro, rapidísima, 30+ modelos (ECMWF, ICON, GFS…). Es la recomendación casi universal para proyectos de hobby. |
| **MET Norway** ✅ | Respaldo ideal: institución pública, en línea de forma continua desde hace más de 15 años. |
| OpenWeatherMap | Exige registrarse y esperar a que activen la clave. Su capa gratuita ha ido degradándose. |
| WeatherAPI / Tomorrow.io | Exigen clave; límites gratuitos más bajos. |
| NOAA / weather.gov | Gratis y sin clave, pero **solo cubre EE. UU.** → inútil para Colombia. |
| wttr.in | Es un *agregador* de otras fuentes, no una fuente independiente. |

> **Nota sobre la verificación TLS:** las dos APIs usan **autoridades
> certificadoras distintas**. La de MET Norway (HARICA, griega) no es Let's
> Encrypt. Los dos certificados raíz van incluidos y **verificados** en
> `root_ca.h`. Como la cadena de Let's Encrypt rota de intermediario cada
> pocos meses, el firmware trae la verificación relajada por defecto
> (`TLS_INSEGURO 1`); el tráfico va igualmente cifrado. Si quieres
> verificación estricta, pon `TLS_INSEGURO 0`.

### Doble red de seguridad

1. **Si Open-Meteo falla** → prueba MET Norway en la misma consulta.
2. **Si fallan las dos** → sigue mostrando la **última lectura buena** en la
   matriz, pero con **`!!`** delante para que sepas que ese dato ya es viejo.
3. **Reintentos rápidos:** normalmente consulta cada 10 min, pero si algo falla
   reintenta cada **2 min**, así se recupera solo en cuanto vuelve internet.
4. **La página web** te dice con un punto de color de qué API viene el dato y
   cuántos minutos tiene.

### ¿Aguanta 24/7 sin que me bloqueen? Sí — estos son los números

Límites **oficiales** de Open-Meteo (plan gratis, no comercial):

| Límite | Valor | Tu consumo normal | ¿Margen? |
|---|---|---|---|
| Por minuto | 600 | 0,004 | 150.000× |
| Por hora | 5.000 | 6 | 833× |
| Por día | 10.000 | 144 | **69×** |
| Por mes | **300.000** | 4.320 | **69×** |

Si algún día falla, el reintento cada 2 min daría 720/día: sigue siendo solo el
**7,2 %** del límite diario (margen 14×). Y peor caso absoluto, con la API caída
todo el mes: 21.600 de 300.000 (7,2 %).

➡️ **Cabe de sobra 24 horas al día, 365 días al año.** Incluso bajando el
intervalo a 1 minuto entraría. La documentación permite expresamente el uso
doméstico ("*personal home automation purposes*").

Al pasarse, la respuesta es **HTTP 429**, y el firmware lo detecta y baja el
ritmo a 30 minutos automáticamente (`INTERVALO_429_MIN`).

### Lo que MET Norway exige (y por lo que te pueden bloquear para siempre)

Sus condiciones de uso son más estrictas. Esto es textual de su web:

> *"Do not fake this or you are likely to be **permanently blacklisted**!"*
> *"...this includes fake or random strings as identification in the
> User-Agent header."*

Por eso el firmware ahora lleva un **User-Agent con contacto real**
(`METNO_USER_AGENT` en `secrets.h`) y cumple las 4 reglas técnicas:

| Regla suya | Cómo la cumple el firmware |
|---|---|
| Identificarse con contacto real | `EstacionMeteoESP32/1.0 github.com/SaintChoppa` |
| `If-Modified-Since` para no rebajar lo mismo | Guarda el `Last-Modified` y lo reenvía → responde **304** (sin cuerpo) |
| Añadir minutos aleatorios | Desfase aleatorio de **0-90 s** en cada consulta |
| Bajar el ritmo ante un 429 | Castigo automático de **30 min** |
| Coordenadas a 4 decimales máximo | `10.3982, -75.4933` ✔ |

Con esto, MET Norway solo se consulta cuando Open-Meteo falla (normalmente
**0 veces al mes**), y cuando pasa, la mayoría de las respuestas son 304 de
coste mínimo.

---

## 4. Cómo cambiar cosas

Todo en **`Firmware/EstacionMeteo/config.h`**:

| Quiero… | Cambia | A |
|---|---|---|
| Cambiar de ciudad | `CIUDAD`, `LATITUD`, `LONGITUD` | los tuyos |
| **Decir cuántas matrices hay** | `NUM_MODULOS` | **8** (2 placas × 4) |
| Mostrar todo en cinta continua | `MODO_PAGINAS` | `0` (por defecto) |
| Que rote páginas (Hora → Temp → Sens → Hum → Viento → Dir) | `MODO_PAGINAS` | `1` |
| Más brillo | `BRILLO` | `0`–`15` |
| **Que se desplace más lento o más rápido** | `VELOCIDAD_SCROLL` | ms por columna: `30` rápido · **`55` actual** · `90` muy lento |
| **Hora en 12 h o en 24 h** | `HORA_24H` | `0` = 2:50 PM · `1` = 14:50 |
| **Viento en km/h o m/s** | `VIENTO_EN_KMH` | `1` = KM/H · `0` = M/S |
| Consultar menos seguido | `INTERVALO_MIN` | `15` |
| Reintentar más rápido si falla | `INTERVALO_ERROR_MIN` | `1` |
| Menos desfase aleatorio | `JITTER_MAX_S` | `0` (pero MET Norway lo pide) |
| Usar solo Open-Meteo (sin respaldo) | `USAR_RESPALDO` | `0` |
| Verificación TLS estricta | `TLS_INSEGURO` | `0` |
| Apagar la web y ahorrar RAM | `SERVIDOR_WEB` | `0` |
| Pila de la tarea (TLS) | `PILA_LOOP_BYTES` | `12288` si te falta heap |
| **Ahorrar corriente** (si el WiFi va flojo) | `WIFI_SIN_DORMIR` | `1` = WiFi siempre despierto · `0` = **deja dormir el WiFi (por defecto)**: consume ~5× menos y da igual la latencia porque se consulta cada 10 min |
| **Sacar la hora por NTP** en vez de la cabecera del clima | `HORA_POR_NTP` | `1` = NTP (petición aparte + SNTP) · `0` = **cabecera `Date` del clima (por defecto)** |
| Si el NTP falló, reintentar cada… | `RESINCRONIZAR_FALLO_MIN` | `5` minutos (solo aplica con `HORA_POR_NTP 1`) |
| **Brillo durante el arranque** | `BRILLO_ARRANQUE` | `1` (por defecto) · `0` = matriz apagada al arrancar (máxima protección si la corriente va justa, ver abajo) |

> ### 🔋 Si la pantalla se queda «llena de LEDs» y el botón EN no la apaga
>
> **Es falta de corriente (brownout), casi seguro.** Y tiene una explicación
> concreta: **el MAX7219 no tiene línea de reset**, así que *conserva lo último
> que le escribieron*. Cuando pulsas EN solo se reinicia el ESP32; la matriz se
> queda como estaba. Si se quedó con todos los LEDs encendidos (lo que más
> consume), se retroalimenta:
>
> ```
> basura en los registros → todos los LEDs encendidos → mucha corriente
>        ↑                                                      ↓
>    no arranca  ←────────── cae la tensión (brownout) ──────────┘
> ```
>
> **El firmware ya se defiende solo:** arranca con la matriz casi apagada
> (`BRILLO_ARRANQUE`) y sube el brillo solo cuando el WiFi y la primera consulta
> ya terminaron, que es cuando la corriente está más tranquila. Así **sale solo
> del bucle** aunque haya un brownout.
>
> **Pero el arreglo de verdad es físico:**
> 1. Fuente de **5 V / 2 A** (el USB del PC da ~500 mA y no llega)
> 2. **Condensador de 470–1000 µF entre 5 V y GND**, junto a la matriz: absorbe
>    los picos de la multiplexación. Cuesta céntimos
> 3. Bajar `BRILLO`
>
> **Cómo saberlo con certeza:** al arrancar, el monitor dice por qué se reinició:
>
> ```
> [salud] Motivo del ultimo reinicio : *** FALTA DE CORRIENTE (brownout) ***
> [salud] Arranques seguidos        : 7   <<<< BUCLE DE REINICIOS
> ```
>
> Si sale `PANICO` o `WATCHDOG` el problema es de software; si sale `BROWNOUT`,
> es la alimentación.

> **De dónde sale la hora (importante).** Por defecto **no** se pide a ningún
> servidor de hora: cada respuesta del clima trae una cabecera `Date` con la hora
> UTC del servidor, precisa al segundo, y el firmware la aprovecha.
>
> | | Antes (NTP) | Ahora (cabecera `Date`) |
> |---|---|---|
> | Peticiones extra | 1 + DNS | **0** |
> | Arranca SNTP | Sí | **No** (se eliminó el reinicio en bucle que causaba) |
> | Se refresca | Cada 24 h | **Cada 10 min** |
> | Redes que bloquean NTP (UDP 123) | Falla | **Funciona** |
>
> Ventaja extra: la hora **se autocorrige en cada consulta**, así que nunca se
> desvía. Si algún día quieres volver al NTP, pon `HORA_POR_NTP 1`.

Y en **`secrets.h`**: `METNO_USER_AGENT` — tu contacto para MET Norway. **No lo
dejes genérico**: pueden bloquear la IP para siempre (ver sección 3).

Coordenadas: clic derecho en Google Maps → "copiar coordenadas".

---

## 5. Sin cables: la página web

Con `SERVIDOR_WEB 1`, el ESP32 levanta un servidor. Desde el celular en la
misma red WiFi, abre:

* `http://<IP-del-ESP32>/` → tarjeta con los 4 datos, se refresca sola cada 20 s
* `http://<IP-del-ESP32>/json` → los datos en JSON, por si quieres consumirlos
  desde otra app. Incluye `proveedor`, `obsoleto` y `antiguedad_min`.

La IP aparece en la pantalla de LEDs al arrancar y en el monitor serie.

---

## 6. Problemas y soluciones

| Síntoma | Causa | Solución |
|---|---|---|
| No aparece ningún COM | Falta el driver CH340 | Ya está instalado, ver [sección 7](#7-qué-hay-instalado-y-dónde) |
| **Se queda en "ESPERANDO DATOS" y tarda en arrancar** | **NTP deja una consulta DNS a medias y `hostByName` limpia la caché → lwIP aborta y el ESP32 se reinicia en bucle** | **Arreglado**: se resuelve un nombre antes de arrancar SNTP y se para el SNTP tras sincronizar. Además, por defecto ya **no se arranca SNTP**: la hora sale de la cabecera `Date` del clima (`HORA_POR_NTP 0`) |
| **`HTTP -1 (TLS: ERROR - Generic error)` y el DNS sí funciona** | **Enlace WiFi flojo y/o alimentación justa.** El DNS resuelve y el TCP conecta, pero el handshake TLS se cae. Se midió: funcionaba a −65 dBm con 4 módulos y falló a −72 dBm con 8 | Acercar el ESP32 al router y **no alimentarlo del puerto USB del PC** (da ~500 mA): usar cargador de 5 V/2 A, o bajar `BRILLO`. El firmware ya reintenta, alarga los tiempos y deja dormir al WiFi |
| `Failed to connect … Wrong boot mode` | Auto-reset roto | Mantén **BOOT**, pulsa **EN**, suelta **EN**, y sube sin soltar BOOT |
| `!!` delante del texto | Fallaron las dos APIs | Mira el monitor serie; se reintenta solo cada 2 min |
| `SIN WIFI - REVISA secrets.h` | Red de 5 GHz o clave mal | Usa la red de **2,4 GHz** |
| La pantalla muestra basura | Tipo de módulo equivocado | Prueba otro `HARDWARE_TYPE` (sección 1) o revisa `NUM_MODULOS` |
| Ningún LED encendido | VCC en 3V3 en vez de VIN | Mueve VCC a **VIN** (5 V) |
| Se reinicia solo cada rato | Falta de corriente | Baja `BRILLO` o alimenta VIN con fuente de 5 V / 2 A |
| El texto sale espejado | Orientación del módulo | Prueba otro `HARDWARE_TYPE` |
| `Acceso denegado` al compilar | Puerto ocupado | Cierra el monitor serie |

---

## 7. Qué hay instalado y dónde

### El driver CH340 (el primer obstáculo)

Windows reconocía el chip USB-serial de la placa pero **no tenía el driver**
(código de error 28 = "los controladores no están instalados"), así que no
aparecía ningún puerto COM.

Se instaló el driver oficial de WCH (`CH341SER.EXE`, firmado digitalmente por
*Nanjing Qinheng Microelectronics*, certificado WHQL y verificado con
`Get-AuthenticodeSignature` → `Valid`) usando `pnputil`, **sin reiniciar**:

```cmd
pnputil /add-driver CH341SER.INF /install
```

Resultado: `USB-SERIAL CH340 (COM5)` en estado OK.

> El **COM puede cambiar** si enchufas el cable en otro puerto USB del PC.

### La herramienta de compilación

`arduino-cli` es el Arduino sin interfaz gráfica: un único `.exe`. Está en
`%LOCALAPPDATA%\ESP32-Toolchain` junto con el core de ESP32 y las librerías,
todo autocontenido y **fuera de OneDrive** a propósito (~8 GB).

| Qué | Versión |
|---|---|
| arduino-cli | 1.5.2 |
| Core ESP32 | 3.3.12 |
| MD_Parola | 3.7.7 |
| MD_MAX72XX | 3.5.1 |
| ArduinoJson | 7.4.3 |

Uso actual del chip: **84 % de flash** (1.111.547 / 1.310.720 B) y
**15 % de RAM** (50.256 / 327.680 B).

Si un día prefieres el IDE gráfico, instala **Arduino IDE 2.x** y añade la URL
`https://espressif.github.io/arduino-esp32/package_esp32_index.json` en
*Archivo → Preferencias*. Tu código funcionará igual.

### Qué se verificó antes de entregarlo

| Prueba | Resultado |
|---|---|
| Driver CH340 | `USB-SERIAL CH340 (COM5)`, estado OK |
| Compilación | 0 errores, 0 warnings · 85 % flash · 15 % RAM |
| **Bug del chunked** (reproducido en el PC) | `getStreamPtr()` → `current` = null ✗ · `getString()` → 29,0 °C ✓ |
| JSON de Open-Meteo (real) | 29,0 °C / 80 % / 35,3 °C / WMO 51 → "LLOVIZNA LEVE" |
| JSON de MET Norway (real, 39 KB) | 28,5 °C / 79,6 % / "lightrainshowers_day" → "CHUBASCOS LEVES" |
| Traductor de códigos MET Norway | 23/23 correctos |
| Memoria del parseo (asignador propio) | 27 KB con filtro vs 77 KB sin filtro (**2,9× menos**) |
| Certificado de Open-Meteo | `Verify return code: 0 (ok)` contra ISRG Root X1 |
| Certificado de MET Norway | `Verify return code: 0 (ok)` contra HARICA RootCA 2015 |
| Pila de la tarea (desensamblado del `.elf`) | Símbolo `T` (ganó la nuestra) y devuelve 24576 ✔ |
| Reinicio en bucle al arrancar | Traza decodificada con `addr2line`: `SNTP → dns_clear_cache` reentrante → arreglado y verificado en el desensamblado |
| Límites de las APIs | Leídos de la documentación oficial, no de blogs |
| Consumo 24/7 | 1,44 % del límite diario (69× de margen) |
| User-Agent de MET Norway | Contacto real → sin riesgo de bloqueo permanente |

---

## 8. Ideas para ampliar

1. **Sensor local DHT22 o SHT31** (~3 €). La estación pasaría a medir *de
   verdad* en tu casa y podrías comparar en pantalla "aquí 31,2 °C / API 29,0 °C".
   Es el salto de juguete a instrumento. Con `#define PIN_DHT 4`.
2. **Iconos animados del clima** (sol, nube, lluvia) dibujados píxel a píxel,
   alternando con el texto. MD_Parola ya trae los efectos.
3. **Probabilidad de lluvia** — añade `&hourly=precipitation_probability` a la
   URL de Open-Meteo. En Cartagena esto es de lo más útil.
4. **Reloj grande** con la hora NTP a pantalla completa, alternando con el clima.
5. **Historial y gráfica**: con el servidor web y SPIFFS, registra la
   temperatura cada 10 min y dibújala.
6. **Aviso de calor extremo**: si la sensación pasa de 38 °C, que la matriz
   parpadee.

---

*Proyecto montado el 8 de octubre de 2026. Nya~ 🐾*
