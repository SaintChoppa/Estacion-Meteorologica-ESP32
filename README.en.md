# 🌤️ Weather Station — ESP32 + MAX7219 Matrix

> 🌐 **Idioma / Language:** [Español](README.md) · **English**

It displays on an 8x8 LED matrix (4-in-1 module) the **temperature**, the
**humidity**, the **feels-like temperature** and the **sky condition** for
**Cartagena de Indias**, fetched in real time over WiFi.

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

## 📋 Index

1. [Hardware and wiring](#1-hardware-and-wiring)
2. [Getting started (3 steps)](#2-getting-started)
3. [The two APIs and why](#3-the-two-apis-and-why)
4. [How to change things (config.h)](#4-how-to-change-things)
5. [No cables: the web page](#5-no-cables-the-web-page)
6. [Problems and solutions](#6-problems-and-solutions)
7. [What's installed and where](#7-whats-installed-and-where)
8. [Ideas to extend](#8-ideas-to-extend)

---

## 1. Hardware and wiring

### Components

| Component | Detail |
|---|---|
| Microcontroller | ESP32 (ESP-WROOM-32) on an expansion board with screw terminals |
| Display | **2 MAX7219 4-in-1 modules** ("1088AB") daisy-chained = **8 8x8 matrices = 64x8 pixels** |
| Power | USB cable to the PC (5 V) |

### Wiring two daisy-chained boards

The two boards are **daisy-chained** (one after the other), not in parallel:

```
ESP32                Placa 1 (4-en-1)          Placa 2 (4-en-1)
GPIO 23 ──DIN──▶  DIN ──[4 matrices]── DOUT ──DIN──▶ DIN ──[4 matrices]
GPIO 18 ──CLK──▶  CLK ─────────────────────────────▶ CLK
GPIO  5 ──CS ──▶  CS  ─────────────────────────────▶ CS
VIN     ──5V ──▶  VCC ─────────────────────────────▶ VCC
GND     ──────▶  GND ─────────────────────────────▶ GND
```

⚠️ **The key point:** the **DOUT** output of board 1 goes to the **DIN** input of
board 2. If you connect both directly to the ESP32 you'll see both showing the
same thing, or garbage.

⚠️ **And in `config.h`:** `NUM_MODULOS` must be **8** (2 boards × 4 matrices).
If it doesn't match what's actually there, the text comes out cut off or
misaligned.


### Connections

The matrix has 5 pins. Connect it **exactly like this**:

| Matrix pin | ESP32 pin | Why |
|---|---|---|
| **VCC** | **VIN** (5 V) | ⚠️ Do NOT use 3V3 — the LEDs look very dim or don't work |
| **GND** | **GND** | Common ground |
| **DIN** | **GPIO 23** | Data (hardware SPI MOSI) |
| **CS**  | **GPIO 5**  | Chip select |
| **CLK** | **GPIO 18** | Clock (hardware SPI SCK) |

> 💡 **Logic levels:** the ESP32 outputs 3.3 V and the MAX7219 expects 3.5 V on
> the data pin. In practice it works perfectly and that's how this project is
> wired. If some day you see ghost pixels, add a *level shifter* from
> 3.3 V → 5 V on the 3 data lines.
>
> ⚡ **Current draw:** the 4 matrices at high brightness can draw ~1 A. Over USB
> and at brightness 3 (the configured value) it's more than enough. If you raise
> the brightness, use a 5 V / 2 A supply on VIN.

### If the display looks wrong

```c
#define HARDWARE_TYPE   MD_MAX72XX::FC16_HW
```

Almost all 4-in-1 modules are `FC16_HW`. If you see scrambled or mirrored LEDs,
try in this order: `PAROLA_HW`, `GENERIC_HW`, `ICSTATION_HW`.

---

## 2. Getting started

### Step 1 — Set your WiFi

Open **`Firmware/EstacionMeteo/secrets.h`** and fill in:

```c
#define WIFI_SSID   "NOMBRE_DE_TU_WIFI"
#define WIFI_PASS   "TU_CONTRASEÑA"
```

⚠️ The ESP32 **only works on 2.4 GHz WiFi**.

### Step 2 — Compile

Double-click **`Herramientas\1-COMPILAR.cmd`**
→ it should end by printing `*** COMPILADO CORRECTAMENTE ***`

### Step 3 — Upload to the ESP32 ⭐ (read carefully)

Double-click **`Herramientas\2-SUBIR.cmd`** and follow what it says:

> **This board does NOT have an auto-reset circuit**, so you have to put it
> into "download mode" by hand:
>
> 1. **Hold down BOOT** (sometimes called IO0) and don't let go.
> 2. Without releasing BOOT, **press and release EN once** (sometimes RST).
> 3. Still holding BOOT, press a key in the window.
> 4. When you see `Writing at 0x...` **you can now release BOOT**.
> 5. When it finishes, if the board doesn't start on its own, **press and release EN**.

In the serial monitor (`Herramientas\3-MONITOR.cmd`) you should see:

```
=== ESTACION METEOROLOGICA - ESP32 + MAX7219 ===
=== Ciudad: CARTAGENA  (lat 10.3982, lon -75.4933) ===
=== APIs: Open-Meteo (principal) + MET Norway (respaldo) ===
[wifi] Conectado.  IP: 192.168.1.42   Senal: -58 dBm
[hora] Reloj sincronizado.
[clima] OK (Open-Meteo) -> 29.0 C  80%  sensacion 35.3 C  (LLOVIZNA LEVE)
[web] Servidor listo en http://192.168.1.42
```

And the matrix will show:
`CARTAGENA   29.0~C   HUMEDAD 80%   SENSACION 35.3~C   LLOVIZNA LEVE   14:32`

---

## 3. The two APIs and why

Since the station is going to sit **fixed in the living room**, it can't depend
on a single service. The firmware uses **two independent APIs** and automatically
tries the second one if the first fails **in the same pass**.

| | **Open-Meteo** (primary) | **MET Norway** (fallback) |
|---|---|---|
| URL | api.open-meteo.com | api.met.no |
| API key? | No | No |
| Registration? | No | No |
| Who's behind it | Open-source project (AGPLv3), 30+ models | **The government of Norway** 🇳🇴 |
| Response time | ~0.35 s | ~1.1 s |
| Provides feels-like temperature | Yes (already computed) | No → **we compute it** with the heat index |
| TLS certificate | Let's Encrypt (ISRG Root X1) | HARICA RootCA 2015 (Greek CA) |

### Why these two and not others

| API | Verdict |
|---|---|
| **Open-Meteo** ✅ | Free, no registration, extremely fast, 30+ models (ECMWF, ICON, GFS…). It's the almost universal recommendation for hobby projects. |
| **MET Norway** ✅ | Ideal fallback: a public institution, continuously online for more than 15 years. |
| OpenWeatherMap | Requires registering and waiting for the key to be activated. Its free tier has been degrading. |
| WeatherAPI / Tomorrow.io | Require a key; lower free limits. |
| NOAA / weather.gov | Free and keyless, but **only covers the US** → useless for Colombia. |
| wttr.in | It's an *aggregator* of other sources, not an independent source. |

> **Note on TLS verification:** both APIs use **different certificate
> authorities**. MET Norway's (HARICA, Greek) is not Let's Encrypt. Both root
> certificates are included and **verified** in `root_ca.h`. Since the Let's
> Encrypt chain rotates its intermediate every few months, the firmware ships
> with relaxed verification by default (`TLS_INSEGURO 1`); the traffic is
> encrypted either way. If you want strict verification, set `TLS_INSEGURO 0`.

### Double safety net

1. **If Open-Meteo fails** → it tries MET Norway in the same query.
2. **If both fail** → it keeps showing the **last good reading** on the matrix,
   but with **`!!`** in front so you know that data is already stale.
3. **Quick retries:** it normally queries every 10 min, but if something fails
   it retries every **2 min**, so it recovers on its own as soon as the internet
   comes back.
4. **The web page** tells you with a colored dot which API the data comes from
   and how many minutes old it is.

### Does it hold up 24/7 without getting me blocked? Yes — here are the numbers

**Official** Open-Meteo limits (free, non-commercial plan):

| Limit | Value | Your normal usage | Margin? |
|---|---|---|---|
| Per minute | 600 | 0.004 | 150,000× |
| Per hour | 5,000 | 6 | 833× |
| Per day | 10,000 | 144 | **69×** |
| Per month | **300,000** | 4,320 | **69×** |

If it ever fails, the retry every 2 min would give 720/day: that's still only the
**7.2 %** of the daily limit (14× margin). And the absolute worst case, with the
API down all month: 21,600 out of 300,000 (7.2 %).

➡️ **It easily fits 24 hours a day, 365 days a year.** Even dropping the interval
to 1 minute would fit. The documentation expressly allows home use
("*personal home automation purposes*").

If you exceed it, the response is **HTTP 429**, and the firmware detects it and
throttles down to 30 minutes automatically (`INTERVALO_429_MIN`).

### What MET Norway requires (and what can get you permanently blocked)

Its terms of use are stricter. This is verbatim from its website:

> *"Do not fake this or you are likely to be **permanently blacklisted**!"*
> *"...this includes fake or random strings as identification in the
> User-Agent header."*

That's why the firmware now carries a **User-Agent with real contact info**
(`METNO_USER_AGENT` in `secrets.h`) and complies with the 4 technical rules:

| Its rule | How the firmware complies |
|---|---|
| Identify with real contact info | `EstacionMeteoESP32/1.0 github.com/SaintChoppa` |
| `If-Modified-Since` to avoid re-downloading the same thing | Stores `Last-Modified` and sends it back → responds **304** (no body) |
| Add random minutes | Random offset of **0-90 s** on every query |
| Throttle down on a 429 | Automatic penalty of **30 min** |
| Coordinates to 4 decimals max | `10.3982, -75.4933` ✔ |

With this, MET Norway is only queried when Open-Meteo fails (normally
**0 times a month**), and when that happens, most responses are 304s with
minimal cost.

---

## 4. How to change things

Everything is in **`Firmware/EstacionMeteo/config.h`**:

| I want to… | Change | To |
|---|---|---|
| Change city | `CIUDAD`, `LATITUD`, `LONGITUD` | yours |
| **Set how many matrices there are** | `NUM_MODULOS` | **8** (2 boards × 4) |
| Show everything in a continuous marquee | `MODO_PAGINAS` | `0` (default) |
| Rotate pages (Time → Temp → Feels → Hum → Wind → Dir) | `MODO_PAGINAS` | `1` |
| More brightness | `BRILLO` | `0`–`15` |
| **Scroll slower or faster** | `VELOCIDAD_SCROLL` | ms per column: `30` fast · **`55` current** · `90` very slow |
| **Time in 12 h or 24 h** | `HORA_24H` | `0` = 2:50 PM · `1` = 14:50 |
| **Wind in km/h or m/s** | `VIENTO_EN_KMH` | `1` = KM/H · `0` = M/S |
| Query less often | `INTERVALO_MIN` | `15` |
| Retry faster on failure | `INTERVALO_ERROR_MIN` | `1` |
| Less random offset | `JITTER_MAX_S` | `0` (but MET Norway asks for it) |
| Use only Open-Meteo (no fallback) | `USAR_RESPALDO` | `0` |
| Strict TLS verification | `TLS_INSEGURO` | `0` |
| Turn off the web page and save RAM | `SERVIDOR_WEB` | `0` |
| Task stack (TLS) | `PILA_LOOP_BYTES` | `12288` if you're short on heap |
| **Save power** (if WiFi is weak) | `WIFI_SIN_DORMIR` | `1` = WiFi always awake · `0` = **let WiFi sleep (default)**: draws ~5× less and the latency doesn't matter because it queries every 10 min |
| **Get the time via NTP** instead of the weather header | `HORA_POR_NTP` | `1` = NTP (separate request + SNTP) · `0` = **weather `Date` header (default)** |
| If NTP failed, retry every… | `RESINCRONIZAR_FALLO_MIN` | `5` minutes (only applies with `HORA_POR_NTP 1`) |
| **Brightness during startup** | `BRILLO_ARRANQUE` | `1` (default) · `0` = matrix off at startup (maximum protection if power is tight, see below) |

> ### 🔋 If the display gets "full of LEDs" and the EN button won't turn it off
>
> **It's a lack of current (brownout), almost certainly.** And it has a
> specific explanation: **the MAX7219 has no reset line**, so it *keeps the last
> thing it was written*. When you press EN only the ESP32 resets; the matrix
> stays as it was. If it was left with all the LEDs on (the biggest draw), it
> feeds back:
>
> ```
> basura en los registros → todos los LEDs encendidos → mucha corriente
>        ↑                                                      ↓
>    no arranca  ←────────── cae la tensión (brownout) ──────────┘
> ```
>
> **The firmware already defends itself:** it starts with the matrix almost off
> (`BRILLO_ARRANQUE`) and only raises the brightness once WiFi and the first
> query have finished, which is when the current is calmest. That way it **gets
> itself out of the loop** even with a brownout.
>
> **But the real fix is physical:**
> 1. A **5 V / 2 A** supply (the PC's USB gives ~500 mA and isn't enough)
> 2. **A 470–1000 µF capacitor between 5 V and GND**, next to the matrix: it
>    absorbs the multiplexing peaks. Costs pennies
> 3. Lower `BRILLO`
>
> **How to know for sure:** at startup, the monitor says why it reset:
>
> ```
> [salud] Motivo del ultimo reinicio : *** FALTA DE CORRIENTE (brownout) ***
> [salud] Arranques seguidos        : 7   <<<< BUCLE DE REINICIOS
> ```
>
> If it says `PANICO` or `WATCHDOG` the problem is software; if it says
> `BROWNOUT`, it's the power supply.

> **Where the time comes from (important).** By default it is **not** requested
> from any time server: every weather response carries a `Date` header with the
> server's UTC time, accurate to the second, and the firmware takes advantage of it.
>
> | | Before (NTP) | Now (`Date` header) |
> |---|---|---|
> | Extra requests | 1 + DNS | **0** |
> | Starts SNTP | Yes | **No** (the reboot loop it caused was removed) |
> | Refreshes | Every 24 h | **Every 10 min** |
> | Networks that block NTP (UDP 123) | Fails | **Works** |
>
> Extra benefit: the time **self-corrects on every query**, so it never drifts.
> If some day you want to go back to NTP, set `HORA_POR_NTP 1`.

And in **`secrets.h`**: `METNO_USER_AGENT` — your contact for MET Norway. **Don't
leave it generic**: they can block the IP forever (see section 3).

Coordinates: right-click in Google Maps → "copy coordinates".

---

## 5. No cables: the web page

With `SERVIDOR_WEB 1`, the ESP32 brings up a server. From your phone on the
same WiFi network, open:

* `http://<IP-del-ESP32>/` → a card with the 4 readings, refreshes by itself
  every 20 s
* `http://<IP-del-ESP32>/json` → the data as JSON, in case you want to consume
  it from another app. Includes `proveedor`, `obsoleto` and `antiguedad_min`.

The IP shows on the LED display at startup and in the serial monitor.

---

## 6. Problems and solutions

| Symptom | Cause | Solution |
|---|---|---|
| No COM port appears | The CH340 driver is missing | It's already installed, see [section 7](#7-whats-installed-and-where) |
| **It gets stuck on "ESPERANDO DATOS" and takes a while to start** | **NTP leaves a DNS query half-done and `hostByName` clears the cache → lwIP aborts and the ESP32 reboots in a loop** | **Fixed**: a name is resolved before starting SNTP and SNTP is stopped after syncing. Also, by default it **no longer starts SNTP**: the time comes from the weather `Date` header (`HORA_POR_NTP 0`) |
| **`HTTP -1 (TLS: ERROR - Generic error)` and DNS does work** | **Weak WiFi link and/or tight power.** DNS resolves and TCP connects, but the TLS handshake drops. It was measured: it worked at −65 dBm with 4 modules and failed at −72 dBm with 8 | Move the ESP32 closer to the router and **don't power it from the PC's USB port** (it gives ~500 mA): use a 5 V/2 A charger, or lower `BRILLO`. The firmware already retries, lengthens the timeouts and lets WiFi sleep |
| `Failed to connect … Wrong boot mode` | Auto-reset is broken | Hold **BOOT**, press **EN**, release **EN**, and upload without releasing BOOT |
| `!!` in front of the text | Both APIs failed | Check the serial monitor; it retries on its own every 2 min |
| `SIN WIFI - REVISA secrets.h` | 5 GHz network or wrong password | Use the **2.4 GHz** network |
| The display shows garbage | Wrong module type | Try another `HARDWARE_TYPE` (section 1) or check `NUM_MODULOS` |
| No LED lit | VCC on 3V3 instead of VIN | Move VCC to **VIN** (5 V) |
| It reboots by itself every so often | Lack of current | Lower `BRILLO` or power VIN with a 5 V / 2 A supply |
| The text comes out mirrored | Module orientation | Try another `HARDWARE_TYPE` |
| `Acceso denegado` when compiling | Port busy | Close the serial monitor |

---

## 7. What's installed and where

### The CH340 driver (the first hurdle)

Windows recognized the board's USB-serial chip but **had no driver**
(error code 28 = "the drivers are not installed"), so no COM port appeared.

The official WCH driver was installed (`CH341SER.EXE`, digitally signed by
*Nanjing Qinheng Microelectronics*, WHQL certified and verified with
`Get-AuthenticodeSignature` → `Valid`) using `pnputil`, **without rebooting**:

```cmd
pnputil /add-driver CH341SER.INF /install
```

Result: `USB-SERIAL CH340 (COM5)` in OK state.

> The **COM port can change** if you plug the cable into a different USB port on the PC.

### The build tool

`arduino-cli` is Arduino without a GUI: a single `.exe`. It lives in
`%LOCALAPPDATA%\ESP32-Toolchain` along with the ESP32 core and the libraries,
all self-contained and deliberately **outside OneDrive** (~8 GB).

| What | Version |
|---|---|
| arduino-cli | 1.5.2 |
| Core ESP32 | 3.3.12 |
| MD_Parola | 3.7.7 |
| MD_MAX72XX | 3.5.1 |
| ArduinoJson | 7.4.3 |

Current chip usage: **84 % flash** (1,111,547 / 1,310,720 B) and
**15 % RAM** (50,256 / 327,680 B).

If one day you prefer the graphical IDE, install **Arduino IDE 2.x** and add the
URL `https://espressif.github.io/arduino-esp32/package_esp32_index.json` under
*File → Preferences*. Your code will work the same.

### What was verified before handing it over

| Test | Result |
|---|---|
| CH340 driver | `USB-SERIAL CH340 (COM5)`, OK state |
| Compilation | 0 errors, 0 warnings · 85 % flash · 15 % RAM |
| **Chunked bug** (reproduced on the PC) | `getStreamPtr()` → `current` = null ✗ · `getString()` → 29.0 °C ✓ |
| Open-Meteo JSON (real) | 29.0 °C / 80 % / 35.3 °C / WMO 51 → "LLOVIZNA LEVE" |
| MET Norway JSON (real, 39 KB) | 28.5 °C / 79.6 % / "lightrainshowers_day" → "CHUBASCOS LEVES" |
| MET Norway code translator | 23/23 correct |
| Parsing memory (custom allocator) | 27 KB with filter vs 77 KB without (**2.9× less**) |
| Open-Meteo certificate | `Verify return code: 0 (ok)` against ISRG Root X1 |
| MET Norway certificate | `Verify return code: 0 (ok)` against HARICA RootCA 2015 |
| Task stack (`.elf` disassembly) | Symbol `T` (ours won) and returns 24576 ✔ |
| Reboot loop at startup | Trace decoded with `addr2line`: reentrant `SNTP → dns_clear_cache` → fixed and verified in the disassembly |
| API limits | Read from the official documentation, not from blogs |
| 24/7 usage | 1.44 % of the daily limit (69× margin) |
| MET Norway User-Agent | Real contact → no risk of permanent blocking |

---

## 8. Ideas to extend

1. **Local DHT22 or SHT31 sensor** (~€3). The station would then measure *for
   real* in your home and you could compare on screen "here 31.2 °C / API 29.0 °C".
   It's the leap from toy to instrument. With `#define PIN_DHT 4`.
2. **Animated weather icons** (sun, cloud, rain) drawn pixel by pixel,
   alternating with the text. MD_Parola already ships the effects.
3. **Rain probability** — add `&hourly=precipitation_probability` to the
   Open-Meteo URL. In Cartagena this is one of the most useful things.
4. **Big clock** with the NTP time full screen, alternating with the weather.
5. **History and chart**: with the web server and SPIFFS, log the
   temperature every 10 min and plot it.
6. **Extreme heat alert**: if the feels-like goes above 38 °C, make the matrix
   blink.

---

*Project built on October 8, 2026. Nya~ 🐾*
