# ESP32 + R200 UHF RFID firmware

Firmware for an **Espressif ESP32** dev board and a **UART-controlled R200** UHF reader. It
reads passive UHF tags and relays them either to a **Fog gateway over MQTT** (the
production path, where the node registers itself and receives an access decision back) or
**straight to an AWS Lambda over HTTPS** (the bench path that bypasses the Fog layer).

This is the Edge layer of an Edge / Fog / Cloud asset-traceability system; see
`documents/ARCHITECTURE.md` for how the pieces fit.

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

1. Copy `src/secrets_example.h` to **`src/secrets.h`** (same folder). It is gitignored.

2. Fill in:

   | Define | Needed when | What it is |
   | --- | --- | --- |
   | `WIFI_SSID` / `WIFI_PASS` | always | The network the ESP32 joins |
   | `NODE_API_KEY` | MQTT mode | Shared key the Fog gateway checks before it issues node credentials. Must match `NODE_API_KEY` in the gateway's `.env` |
   | `GATEWAY_LAMBDA_URL` | HTTPS mode | Lambda Function URL. HTTPS only — `http://` gets a TLS mismatch from AWS |
   | `GATEWAY_X_API_KEY` | HTTPS mode | The Cloud API key, from `terraform output -raw api_key_value` on the tenant root |

3. Never commit the real `secrets.h`. `secrets_example.h` is the template.

The node's **identity is not configured here**. Over MQTT it registers itself with the
gateway using its MAC address and stores the `node_id` / `node_key` the gateway hands back
in NVS, so it survives reboots and reflashes of the same board.

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

```
=== Edge node 0.2.0 — mode=rfid transport=MQTT (Fog gateway) ===
[NET] connecting to Wi-Fi....
[NET] Wi-Fi up  ip=192.168.49.51  rssi=-54 dBm  mac=A0:B7:65:12:34:56
[RFID] UART link test: PASS — the ESP32 is talking to the R200.
[PROV] no stored identity — will register as mac=A0:B7:65:12:34:56
[GW/MQTT] connected as esp32-r200-A0B765123456 (clean_session=false)
[PROV] register attempt 1/5 mac=A0:B7:65:12:34:56
[PROV] registered as node_id=node-3f9a1c04
[GW/MQTT] subscribed bicicletero/esp/node-3f9a1c04/responses
[APP] ready.
[TELEMETRY] up=30s wifi=up ip=192.168.49.51 rssi=-54 heap=213480 node=node-3f9a1c04 ...
[RFID] tag accepted {"tag":"E28006900000500E88C6A4A7","node_key":"…","ts":"2026-09-12T14:03:07Z"}
[ACCESS] status=200 (access allowed) rtt=184 ms message=Access allowed
```

On a second boot the `[PROV]` lines are replaced by
`[PROV] identity restored from NVS: node_id=node-3f9a1c04`.

If UART fails: recheck **TX/RX crossover**, **GND**, **baud**, and **R200 power**.

---

## Feature toggles (quick reference)

All of these are **compile-time**: changing one means rebuilding and reflashing. Set them
in `src/config/app_config.h` or with `build_flags` in `platformio.ini`.

| Flag | Default | Meaning |
| --- | --- | --- |
| `SYSTEM_MODE` | `SYSTEM_MODE_RFID` (0) | `0` reads tags; `1` skips the reader entirely and only reports telemetry |
| `GATEWAY_USE_MQTT` | `1` | `1` = MQTT to the Fog gateway. `0` = HTTPS straight to the Lambda, bypassing the Fog layer |
| `MESSAGE_GATEWAY` | `1` | `0` disables the uplink altogether (serial-only bring-up) |
| `R200_LINK_TEST` | `1` | UART sanity check at boot |
| `USE_CONTINUOUS_POLL` | `0` | `1` uses multi-poll, which runs a finite counter and is never re-armed. `0` (single poll) is the supported mode |
| `APP_MQTT_HOST` | `192.168.49.28` | Broker address. Must be reachable from the ESP32 — never `localhost` |
| `APP_NODE_PREFIX` / `APP_GATEWAY_PREFIX` | `bicicletero/esp`, `bicicletero/gateway` | MQTT topic prefixes. Both sides of the contract have to agree, so change them together with the gateway's `APP_MQTT_TOPIC` |
| `ACCESS_GRANTED_PIN` / `ACCESS_DENIED_PIN` | `-1` (off) | GPIO pulsed when the gateway allows or refuses a tag |
| `ACCESS_DEGRADED_PIN` | `-1` (off) | GPIO pulsed when the gateway could not decide (`400`, `401`, `403`, `500`, `503`). Left off, the degradation is blinked on the denied pin instead, so a dead backend never looks like a refused tag |
| `kUnansweredReadRetries` | `2` | How many times a read that got no answer goes back on the outbox. `0` restores the drop-on-timeout policy |
| `kRevalidateIdentityOnBoot` | `true` | Re-register once per boot even with credentials in NVS, so a node recovers on its own if the gateway lost its node table |

Example:

```bash
PLATFORMIO_BUILD_FLAGS='-DSYSTEM_MODE=1' pio run
PLATFORMIO_BUILD_FLAGS='-DGATEWAY_USE_MQTT=0 -DAPP_MQTT_HOST=\"192.168.0.20\"' pio run
```

Two things bite here. Build flags go through `PLATFORMIO_BUILD_FLAGS`, **not** through
`pio run -a` — `-a` is `--program-arg`, PlatformIO ignores it for build flags and hands you
the default firmware with no warning at all. And a flag whose value is a string needs its
quotes escaped (`\"…\"`), because PlatformIO strips one level when it splits the variable;
without the backslashes the preprocessor sees a bare `192.168.0.20` and the build fails
with *too many decimal points in number*.

`NODE_ID` is an **environment variable**, not a build flag: `scripts/direct_node_id.py`
writes it into `src/config/direct_node_id.h` before each build, as `DIRECT_NODE_ID`. It
labels the direct-to-Lambda mode only — over MQTT the gateway assigns the real id, so
setting `NODE_ID` does nothing in the default configuration.

---

## Run the tests

The logic that does not need a radio or a network runs on the host:

```bash
./test/native/run.sh
```

No PlatformIO needed, just a C++17 compiler. See [test/native/README.md](test/native/README.md).

## Documentation in this repo

| Document                                               | Content                                               |
| ------------------------------------------------------ | ----------------------------------------------------- |
| [documents/ARCHITECTURE.md](documents/ARCHITECTURE.md) | Modules, the path of one tag read, topics, run modes.  |
| [documents/HARDWARE.md](documents/HARDWARE.md)         | ESP32 ↔ R200 electrical and protocol overview.        |
| [documents/ROADMAP.md](documents/ROADMAP.md)           | What this version does and what comes next.           |
| [test/native/README.md](test/native/README.md)         | Host test suite: what it covers and what it does not. |

---

## License / thesis

---
