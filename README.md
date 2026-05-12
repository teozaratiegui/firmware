# ESP32 + R200 UHF RFID firmware

Firmware for an **Espressif ESP32** dev board and a **UART-controlled R200** UHF reader. It reads passive tag IDs over RFID and can send them to a backend over **HTTPS** or **MQTT** (see `src/config/app_config.h` and `documents/ARCHITECTURE.md`).

This README is written so you can **download the repo**, use a **breadboard**, **three jumper wires** for the data link (plus power as described below), an **ESP32**, and an **R200**, and get a first run on the serial monitor.

---

## Requirements

### Hardware

| Item                              | Notes                                                                                                                                                                                                                                           |
| --------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **ESP32** dev board               | Tested target: **ESP32-WROOM-32** class board (`esp32dev` in PlatformIO). USB cable for power and programming.                                                                                                                                  |
| **R200** module                   | UHF RFID reader with **TTL UART** (3.3 V logic typical—**check your module datasheet**).                                                                                                                                                        |
| **Breadboard** + **jumper wires** | For neat, reliable connections.                                                                                                                                                                                                                 |
| **Three wires (UART + GND)**      | **GND** (common ground), **TX** from ESP32 → **RX** on R200, **RX** on ESP32 ← **TX** from R200 (crossed serial).                                                                                                                               |
| **Power for the R200**            | The R200 is **not** powered by those three signal wires. Use the **voltage and current** your module requires (often 3.3 V or 5 V on a dedicated rail). Share **GND** between ESP32 and R200. Poor power causes dropped reads or UART failures. |
| **UHF antenna**                   | Compatible with your R200 and band (e.g. regional regulations).                                                                                                                                                                                 |
| **Passive UHF tag**               | For testing in the read range.                                                                                                                                                                                                                  |

### Software

| Requirement        | Purpose                                                                                                                                                                                                                                                      |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Git** (optional) | Clone this repository.                                                                                                                                                                                                                                       |
| **Python 3**       | Used by PlatformIO.                                                                                                                                                                                                                                          |
| **PlatformIO**     | Build, upload, and serial monitor. **Option A:** [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) (VS Code / Cursor extension). **Option B:** [PlatformIO Core (CLI)](https://docs.platformio.org/en/latest/core/installation.html) only. |
| **USB driver**     | So the PC sees the ESP32 as a **COM** port (Windows) or `/dev/ttyUSB*` (Linux) / `/dev/cu.*` (macOS). Driver depends on your board’s USB chip (CP210x, CH340, etc.).                                                                                         |

---

## Wiring (minimum)

Default UART pins are defined in `src/config/app_config.h`:

| ESP32 GPIO        | Connect to R200 | Direction                       |
| ----------------- | --------------- | ------------------------------- |
| **GPIO 16** (TX2) | **RX**          | ESP transmits → module receives |
| **GPIO 17** (RX2) | **TX**          | Module transmits → ESP receives |
| **GND**           | **GND**         | **Required**                    |

**Baud rate:** `115200` (same file). If your module shipped with another default (e.g. `9600`), change `R200_BAUD` there, rebuild, and flash.

**Power:** Connect the R200 **VCC** and **GND** according to its datasheet. Do not assume the ESP32 3.3 V pin can supply enough **peak current** for the RF stage; use a solid supply if you see brownouts or unstable UART.

---

## Get the code

```powershell
git clone <URL-of-this-repository>
cd Firmware
```

If you do not use Git, download the project as a ZIP and open the **`Firmware`** folder (the one that contains `platformio.ini`).

---

## Configure secrets

1. Copy the example secrets file:
   - Copy `src/secrets_example.h` to **`src/secrets.h`** (same folder).

2. Edit **`src/secrets.h`** and set:
   - **`WIFI_SSID`** / **`WIFI_PASS`** — network the ESP32 will join.
   - **`GATEWAY_LAMBDA_URL`** — HTTPS endpoint for tag ingest (JSON body `{"tag":"<hex>"}`), if you use HTTP mode.
   - **`GATEWAY_X_API_KEY`** — API key header expected by your gateway, if applicable.

3. **Do not commit** real `secrets.h` to a public repository. Keep `secrets_example.h` as the template only.

---

## Serial port (upload and monitor)

`platformio.ini` may contain `upload_port = COM5`. **Change it** to your ESP32’s port, or **remove** the line to let PlatformIO auto-detect.

On Windows PowerShell you can list ports (example):

```powershell
pio device list
```

Set `monitor_speed = 115200` (already in `platformio.ini`) to match `Serial.begin(115200)` in firmware.

---

## Build, upload, monitor

From the **`Firmware`** directory (where `platformio.ini` lives):

```powershell
pio run -t upload
pio device monitor
```

Or use your IDE’s **PlatformIO: Upload** and **Serial Monitor** actions.

**Expected on boot (summary):**

- Wi‑Fi connection status and IP (when connected).
- **R200 link test** line (if `R200_LINK_TEST` is enabled): confirms UART to the module.
- Periodic **HEARTBEAT** lines (uptime, Wi‑Fi, heap, tag present).
- When a tag is accepted (after cache rules): **`TAG_LOG`** and optional **`[GW] sent`** if the gateway send succeeds.

If UART fails: recheck **TX/RX crossover**, **GND**, **baud**, and **R200 power**.

---

## Feature toggles (quick reference)

In **`src/config/app_config.h`** (or via `build_flags` in `platformio.ini`):

| Flag                  | Meaning                                                                                                    |
| --------------------- | ---------------------------------------------------------------------------------------------------------- |
| `MESSAGE_GATEWAY`     | `1` = call cloud send path when a tag is accepted.                                                         |
| `GATEWAY_USE_MQTT`    | `0` = HTTPS (`GATEWAY_*` in `secrets.h`); `1` = MQTT: broker `kMqtt*` in `app_config.h`, topics `bicicletero/esp/<NODE_ID>/requests` (publish) and `…/responses` (subscribe). |
| `R200_LINK_TEST`      | `1` = run UART sanity check at boot.                                                                       |
| `USE_CONTINUOUS_POLL` | `0` = timed poll; `1` = streaming-style mode if your use case needs it.                                    |

**`SYSTEM_MODE`** — in `src/config/app_config.h`, default is `SYSTEM_MODE_RFID` (R200 + tag gateway on new/cooldown reads). Use `SYSTEM_MODE_GATEWAY_INTERVAL` for no R200: **only** periodic telemetry on `MESSAGE_GATEWAY` (HTTP or MQTT per `GATEWAY_USE_MQTT`, interval = `kHeartbeatIntervalMs`). Override with `build_flags = '-DSYSTEM_MODE=1'`. Requires `MESSAGE_GATEWAY=1` for `SYSTEM_MODE_GATEWAY_INTERVAL`.

**MQTT `NODE_ID`** — build-time env var read by `scripts/mqtt_node_env.py` into `src/config/mqtt_node_config.h`. Example (PowerShell): `$env:NODE_ID = "rack-a1"; pio run`. Default `001` if unset. Same id is used as `esp32_id` in JSON payloads.

---

## Documentation in this repo

| Document                                               | Content                                               |
| ------------------------------------------------------ | ----------------------------------------------------- |
| [documents/ARCHITECTURE.md](documents/ARCHITECTURE.md) | Software modules and data flow.                       |
| [documents/HARDWARE.md](documents/HARDWARE.md)         | ESP32 ↔ R200 electrical and protocol overview.        |
| [documents/ROADMAP.md](documents/ROADMAP.md)           | Planned versions (tests, MQTT auth, subscribe, etc.). |

---

## License / thesis

---
