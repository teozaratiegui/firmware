# ESP32 + R200 UHF RFID firmware

Firmware for an **Espressif ESP32** dev board and a **UART-controlled R200** UHF reader. It
reads passive UHF tags and relays them either to a **Fog gateway over MQTT** (the
production path, where the node registers itself and gets an access decision back) or
**straight to an AWS Lambda over HTTPS** (the bench path that bypasses the Fog layer).

This is the Edge layer of an Edge / Fog / Cloud asset-traceability system. For how the
pieces fit — the path of one read, the registration handshake, the topics, what is
deliberately missing — see [documents/ARCHITECTURE.md](documents/ARCHITECTURE.md).

This README takes you from a fresh clone to a first read on the serial monitor with an
ESP32, an R200, a breadboard and three jumper wires for the data link (plus a proper power
rail for the reader, see below).

---

## Repository layout

```
firmware/
├── platformio.ini              build environment; the build-flag switches are listed here too
├── scripts/direct_node_id.py   pre-build script: turns $NODE_ID into src/config/direct_node_id.h
├── lib/                        reusable, knows nothing about this particular node
│   ├── R200/                   UART driver: framing, checksum, EPC extraction
│   ├── Cache/                  fixed-size per-UID debounce table, no heap
│   └── MessageGateway/         uplink: topics, outbox, round-trip timing
│       ├── TransportMode.h     Strategy interface
│       ├── MqttTransport.*     MQTT to the Fog gateway
│       ├── HttpTransport.*     HTTPS to the Lambda Function URL
│       └── GatewayMessages.*   every JSON shape on the wire, in one file
├── src/                        this node: pins, broker, run mode, wiring of the parts
│   ├── main.cpp                composition root and superloop
│   ├── config/app_config.h     every compile-time knob
│   ├── secrets.h               credentials — gitignored, copy from secrets_example.h
│   ├── net/                    Wi-Fi, non-blocking reconnect, NTP, MAC identity
│   ├── provisioning/           registration state machine + NVS credential storage
│   ├── rfid/                   reader bring-up, UID helpers, debounce and relay policy
│   ├── gateway/                transport factory (the only place that sees secrets.h)
│   └── app/                    access indicator (GPIO) and telemetry reporter
├── test/native/                host test suite — needs only a C++17 compiler
└── documents/                  architecture, hardware and roadmap
```

`lib/` never includes anything from `src/`. That is what lets the host suite compile the
driver, the cache and the uplink without an Arduino toolchain.

---

## Requirements

### Hardware

| Item | Notes |
| --- | --- |
| **ESP32** dev board | Tested target: **ESP32-WROOM-32** class board (`esp32dev` in PlatformIO). USB cable for power and programming. |
| **R200** module | UHF RFID reader with **TTL UART** (3.3 V logic typical — **check your module datasheet**). |
| **Breadboard** + **jumper wires** | For neat, reliable connections. |
| **Three wires (UART + GND)** | **GND** (common ground), ESP32 **GPIO 16** → R200 **RX**, ESP32 **GPIO 17** ← R200 **TX** (crossed serial). |
| **Power for the R200** | The R200 is **not** powered by those three signal wires. Use the voltage and current your module requires, on a dedicated rail. Share **GND** between ESP32 and R200. Poor power causes dropped reads and UART failures. |
| **UHF antenna** | Compatible with your R200 and band (mind regional regulations). |
| **Passive UHF tag** | For testing in the read range. |

### Software

| Requirement | Purpose |
| --- | --- |
| **Git** (optional) | Clone this repository. |
| **Python 3** | Used by PlatformIO. |
| **PlatformIO** | Build, upload, serial monitor. **Option A:** [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) (VS Code / Cursor extension). **Option B:** [PlatformIO Core (CLI)](https://docs.platformio.org/en/latest/core/installation.html). |
| **USB driver** | So the PC sees the ESP32 as a **COM** port (Windows) or `/dev/ttyUSB*` (Linux) / `/dev/cu.*` (macOS). Depends on your board's USB chip (CP210x, CH340…). |
| **A C++17 compiler** | Only to run the host test suite. Not needed to build the firmware. |

---

## Wiring (minimum)

| ESP32 GPIO | Connect to R200 | Direction |
| --- | --- | --- |
| **GPIO 16** | **RX** | ESP transmits → module receives |
| **GPIO 17** | **TX** | Module transmits → ESP receives |
| **GND** | **GND** | **Required** |

> **The two UART pins are swapped relative to the usual ESP32 pinout on purpose.** Most
> pinout diagrams label GPIO 16 as *RX2* and GPIO 17 as *TX2*; this firmware remaps
> `Serial2` the other way round (`R200_RX_PIN 17`, `R200_TX_PIN 16` in
> `src/config/app_config.h`, passed to `R200::begin` in `src/rfid/rfid_hw.cpp`). Wire it as
> the table says, not as the silkscreen suggests. If you would rather follow the diagram,
> swap the two defines and rebuild.

**Baud rate:** `115200` (`R200_BAUD`, same file). If your module shipped with another
default (e.g. `9600`), change it there, rebuild and flash.

**Power:** connect R200 **VCC** and **GND** per its datasheet. Do not assume the ESP32
3.3 V pin can supply the **peak current** of the RF stage; use a solid supply if you see
brownouts or unstable UART.

---

## Get the code

```bash
git clone git@github.com:teozaratiegui/firmware.git
cd firmware
```

Without Git, download the ZIP and open the folder that contains `platformio.ini`.

---

## Configure secrets

1. Copy `src/secrets_example.h` to **`src/secrets.h`** (same folder). It is gitignored.

2. Fill in:

   | Define | Needed when | What it is |
   | --- | --- | --- |
   | `WIFI_SSID` / `WIFI_PASS` | always | The network the ESP32 joins |
   | `NODE_API_KEY` | MQTT mode | Shared key the Fog gateway checks before it issues node credentials. Must match `NODE_API_KEY` in the gateway's `.env` |
   | `GATEWAY_LAMBDA_URL` | HTTPS mode | Lambda Function URL. HTTPS only — `http://` gets a TLS mismatch from AWS |
   | `GATEWAY_X_API_KEY` | HTTPS mode | The Cloud API key (see below) |

3. Never commit the real `secrets.h`. `secrets_example.h` is the template.

The Cloud API key is **not** a Terraform output: the tenant stack creates the SSM parameter
but never holds its value, so it has to be read from SSM. From the tenant root
(`iac/orgs/<org>/tenant/<env>/` in the IaC repository):

```bash
aws ssm get-parameter --with-decryption --output text --query Parameter.Value \
  --name "$(terraform output -raw api_key_parameter_name)"
```

The node's **identity is not configured here.** Over MQTT it registers itself with the
gateway using its MAC address and stores the `node_id` / `node_key` the gateway hands back
in NVS, so they survive reboots and reflashes of the same board.

---

## Serial port (upload and monitor)

PlatformIO auto-detects the port. To pin it — useful when more than one serial device is
attached — uncomment and edit the `upload_port` line in `platformio.ini`:

```ini
upload_port = /dev/cu.usbserial-0001   ; macOS; COM5 on Windows, /dev/ttyUSB0 on Linux
```

List the candidates with `pio device list`. `monitor_speed = 115200` is already set and
matches `Serial.begin(115200)` in the firmware.

---

## Build, upload, monitor

From the directory that holds `platformio.ini`:

```bash
pio run -t upload
pio device monitor
```

Or your IDE's **PlatformIO: Upload** and **Serial Monitor** actions.

**Expected on a first boot:**

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
[TELEMETRY] up=30s wifi=up ip=192.168.49.51 rssi=-54 heap=213480 node=node-3f9a1c04 tag_present=0 sent=0 queued=0 dropped=0 lost=0 abandoned=0 prov=registered/1 last_rtt=0ms clock=ntp
[RFID] tag relayed {"tag":"E28006900000500E88C6A4A7","node_key":"…","ts":"2026-09-12T14:03:07Z"}
[ACCESS] status=200 (access allowed) rtt=184 ms message=Access allowed
```

On a second boot the two `[PROV]` lines before `registered` are replaced by
`[PROV] identity restored from NVS: node_id=node-3f9a1c04`.

---

## What the node does once it is up

| Behaviour | Value | Where |
| --- | --- | --- |
| Inventory poll | every **350 ms** (`kPollIntervalMs`) | `src/rfid/` |
| Per-UID debounce | **5 s** (`kTagCooldownMs`), up to **16** UIDs tracked (`kTagCacheCapacity`) | `lib/Cache/` |
| Reads in flight | **one**; the next read waits for the gateway's answer | `lib/MessageGateway/` |
| Unanswered read | retried **2×** (`kUnansweredReadRetries`) after **8 s** (`kResponseTimeoutMs`), then counted in `abandoned` | `lib/MessageGateway/` |
| Offline queue | **16** reads (`kOutboxCapacity`), **in RAM** — survives an outage, not a reboot | `lib/MessageGateway/` |
| Telemetry | every **30 s** (`kTelemetryIntervalMs`), on `…/<node_id>/telemetry` | `src/app/` |
| Presence | retained `{"online":…}` on `…/<MAC>/status`, also the last will | `lib/MessageGateway/` |
| A `403` from the gateway | wipes the stored credentials and re-registers | `src/provisioning/` |

The `[TELEMETRY]` line is the node's own status report and is the fastest way to see what
it thinks is going on: `prov=` is the registration state, `queued=` the outbox depth,
`clock=ntp|unset` whether the timestamps are real.

---

## Feature toggles

All of these are **compile-time**: changing one means rebuilding and reflashing.

### Build flags

Overridable from the command line or from `build_flags` in `platformio.ini`, without
editing any source file.

| Flag | Default | Meaning |
| --- | --- | --- |
| `SYSTEM_MODE` | `SYSTEM_MODE_RFID` (0) | `0` reads tags; `1` skips the reader entirely and only reports telemetry |
| `GATEWAY_USE_MQTT` | `1` | `1` = MQTT to the Fog gateway. `0` = HTTPS straight to the Lambda, bypassing the Fog layer |
| `MESSAGE_GATEWAY` | `1` | `0` disables the uplink altogether (serial-only bring-up): the reader still runs and prints every UID, and reads are dropped rather than queued for a link that is never opened |
| `R200_LINK_TEST` | `1` | UART sanity check at boot |
| `USE_CONTINUOUS_POLL` | `0` | `1` uses multi-poll, which runs a finite counter and is never re-armed. `0` (single poll) is the supported mode |
| `APP_MQTT_HOST` | `"192.168.49.28"` | Broker address. Must be reachable from the ESP32 — never `localhost` |
| `APP_NODE_PREFIX` / `APP_GATEWAY_PREFIX` | `"bicicletero/esp"`, `"bicicletero/gateway"` | MQTT topic prefixes — see the note below |
| `ACCESS_GRANTED_PIN` / `ACCESS_DENIED_PIN` | `-1` (off) | GPIO pulsed for 2 s when the gateway allows (`200`, `204`) or refuses (`404`, `422`) a tag |
| `ACCESS_DEGRADED_PIN` | `-1` (off) | GPIO pulsed when the gateway reached no verdict (`400`, `401`, `403`, `500`, `503`). Left off, degradation is *blinked* on the denied pin, so a dead backend never looks like a refused tag |

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

> **Both topic prefixes have a counterpart on the gateway, and they are two settings, not
> one.** `APP_NODE_PREFIX` pairs with `APP_MQTT_TOPIC` and `APP_GATEWAY_PREFIX` with
> `APP_MQTT_GATEWAY_PREFIX`, both in the gateway's `.env`. Changing one side only is
> silent: the node publishes where nobody listens, registration times out, and it ends up
> in `prov=gave_up`.

### Source constants

These are `static constexpr` in `src/config/app_config.h`, not macros. **A `-D` flag will
not override them** — it produces a compile error. Edit the file.

| Constant | Default | Meaning |
| --- | --- | --- |
| `kUnansweredReadRetries` | `2` | How many times a read that got no answer goes back on the outbox. `0` restores the drop-on-timeout policy |
| `kRevalidateIdentityOnBoot` | `true` | Re-register once per boot even with credentials in NVS, so a node recovers on its own if the gateway lost its node table |
| `kTagCooldownMs`, `kPollIntervalMs`, `kOutboxCapacity`, `kResponseTimeoutMs`, `kTelemetryIntervalMs` | see the table above | Timing and sizing of the read path |

### The direct-to-Lambda node id

`NODE_ID` is an **environment variable**, not a build flag: `scripts/direct_node_id.py`
writes it into `src/config/direct_node_id.h` as `DIRECT_NODE_ID` before each build. It
labels the direct-to-Lambda mode only — over MQTT the gateway assigns the real id, so
setting `NODE_ID` does nothing in the default configuration.

```bash
NODE_ID=bench-7a PLATFORMIO_BUILD_FLAGS='-DGATEWAY_USE_MQTT=0' pio run
```

---

## Troubleshooting

Read the serial monitor first: every failure below announces itself there.

| Symptom | Meaning | What to check |
| --- | --- | --- |
| `[RFID] UART link test: FAIL` | The ESP32 is not talking to the R200 | TX/RX crossover (see the note in *Wiring* — the pins are swapped on purpose), common GND, `R200_BAUD`, and the reader's own power rail |
| `[NET] Wi-Fi FAILED` | Association failed | `WIFI_SSID` / `WIFI_PASS` in `secrets.h`. The ESP32 is 2.4 GHz only |
| `[PROV] gave up after 5 attempts` | The gateway never answered the registration | `NODE_API_KEY` matches the gateway's `.env`; the gateway container is running; `APP_GATEWAY_PREFIX` matches `APP_MQTT_GATEWAY_PREFIX`. It retries on the next reconnect, so it is not terminal |
| `prov=gave_up` in telemetry, forever | Same as above, seen from the broker instead of the console | While unregistered the node publishes telemetry on `…/<MAC>/telemetry`, so subscribe there |
| `clock=unset` in telemetry | NTP never answered, so reads go out **without** a `ts` | Outbound UDP 123 from the node's network. The field is omitted rather than faked, so this is silent otherwise |
| `queued=` climbing | Reads are piling up in the outbox | The broker is up but the gateway consumer is not, or answers are not coming back. Check `lost=` and `abandoned=` next to it |
| `[GW/MQTT]` never connects | Broker unreachable | `APP_MQTT_HOST` must be an address the ESP32 can reach — not `localhost`, not a Docker-internal name. The log decodes PubSubClient's error code |
| Tags read but nothing relayed | Debounce | The same UID is only relayed once per `kTagCooldownMs` (5 s); `[RFID] within cooldown, not relayed:` says so every 3 s |

---

## Run the tests

The logic that needs no radio and no network runs on the host:

```bash
./test/native/run.sh
```

No PlatformIO needed, just a C++17 compiler. A second binary covering the wire contract
against the real ArduinoJson is built automatically if ArduinoJson is on disk (run
`pio run` once, or set `ARDUINOJSON_DIR`). See
[test/native/README.md](test/native/README.md) for what is covered and what is not.

> **Both binaries have to run.** The behaviour binary asserts against a hand-written parser
> (`gateway_codec_stub.cpp`), so it stays green even if a field name in the real
> `GatewayMessages.cpp` changes — only the codec binary catches that. So a missing
> ArduinoJson is a **failure**, not a skip: "the contract is fine" must not look like "the
> contract was not checked". `ALLOW_SKIP_CODEC=1` runs the behaviour suite alone on purpose,
> and says out loud that the contract went unverified.

---

## Documentation in this repo

| Document | Content |
| --- | --- |
| [documents/ARCHITECTURE.md](documents/ARCHITECTURE.md) | Modules, the path of one tag read, the registration state machine, topics, run modes, and what is deliberately absent |
| [documents/HARDWARE.md](documents/HARDWARE.md) | ESP32 ↔ R200 electrical and protocol overview |
| [documents/ROADMAP.md](documents/ROADMAP.md) | What this version does and what comes next |
| [test/native/README.md](test/native/README.md) | Host test suite: what it covers and what it does not |

---

## Status

**v0.2.** The MQTT contract with the Fog gateway closes in code and is covered by tests,
but the full Edge → Fog → Cloud path has **not** been run end to end against a live
gateway yet. Treat "it works" as a hypothesis until that happens.

Part of an undergraduate thesis on RFID asset traceability over an Edge / Fog / Cloud
architecture deployed with Infrastructure as Code. No license has been chosen yet.
