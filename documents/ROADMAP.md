# Roadmap

Planned evolution of this firmware (ESP32 + R200, HTTP/MQTT gateway). Versions follow [Semantic Versioning](https://semver.org/): **0.x** indicates pre–1.0 APIs and behavior that may still change.

---

## v0.1 — current

**Status:** baseline described in [ARCHITECTURE.md](ARCHITECTURE.md).

**Scope**

- **Two uplink transports:** HTTPS POST (Lambda-style JSON body) via `Http`, and **MQTT publish** via `Mqtt` / `PubSubClient`, selected at build time (`GATEWAY_USE_MQTT`) or via `MessageGateway::setTransport`.
- **R200 integration:** UART (`Serial2`), polling / inventory path, UID surfaced to application code.
- **Tag pipeline:** debouncing with `Cache`, optional garbage-frame filter, `MessageGateway::sendTag` / payload shaping per transport.
- **Configuration:** `app_config.h` (pins, timing, MQTT broker endpoints), `secrets.h` (Wi‑Fi, HTTPS URL/API key).

---

## v0.2 — automated testing

**Goal:** increase confidence in regressions and refactors without relying only on on-device manual tests.

**Planned direction**

- **PlatformIO `test/`** (e.g. Unity / native or embedded targets as appropriate).
- **`Cache`:** cooldown behavior, capacity / eviction, repeated UID timing edge cases.
- **Tag processing:** UID formatting, zero/garbage heuristics, interaction with a mock gateway (no real Wi‑Fi/R200).
- **`MessageGateway` / transports:** where feasible, mock `WiFiClient` / `HTTPClient` is heavy on ESP32; options include extracting pure JSON builders and testing them on host, or lightweight integration tests that only verify payload strings and header composition without opening sockets.
- **MQTT / HTTP:** prioritize **unit-level** tests (payloads, topic names, config structs); reserve **hardware-in-the-loop** for a later checklist or manual test script if full broker mocks are impractical in CI.

---

## v0.3 — MQTT authentication

**Goal:** support brokers that require **authenticated** clients instead of anonymous connects.

**Context today**

- `Mqtt::Config` already has optional `user` and `pass` fields; `ensureConnected()` calls `mqtt.connect(..., user, pass)` when `user` is non-empty.
- Values are currently exposed as empty placeholders in `app_config.h` (or could move to `secrets.h`).

**Planned evolution**

1. **Username / password (MQTT v3.1.1)**  
   - Standard broker login: client sends username and password in the CONNECT packet.  
   - **Configuration:** store credentials in `secrets.h` (or NVS in a later phase), not in committed templates.  
   - **Security note:** credentials are **plaintext in flash** unless combined with TLS; on plain TCP, sniffing on the LAN exposes them.

2. **TLS + authentication (recommended for production)**  
   - Use **MQTTS** (MQTT over TLS, typically port **8883**).  
   - Requires `WiFiClientSecure` (or equivalent) for the MQTT socket, server certificate validation (CA bundle or pinning), and optionally **client certificates** if the broker uses mutual TLS.  
   - **Work:** extend `Mqtt` to use a secure client stack, add config for broker CA / `setInsecure()` only for lab use, align `kMqttPort` and `PubSubClient` buffer sizes for TLS overhead.

3. **Optional: token-based patterns**  
   - Some clouds put a **JWT or API key in the password field** or in a custom mechanism; document the broker’s expected CONNECT shape and map it to `user`/`pass` or a small extension if required.

**Deliverables**

- Documented config keys for MQTT auth and TLS.  
- Clear separation: **lab** (optional insecure TLS) vs **production** (verify server cert, secrets not in repo).

---

## v0.4 — MQTT subscribe: cloud feedback per tag

**Goal:** after publishing a tag (or a related event), the **device can receive** a **response from the cloud/gateway** on a **known topic**, so firmware logic (logging, UI, or future actuators) can reflect **per-tag outcomes** (e.g. allowed/denied, metadata).

**Context today**

- `Mqtt` subscribes to `bicicletero/esp/<NODE_ID>/responses` before publishing to `…/requests` (see `lib/MessageGateway/MessageGateway.h`).  
- Incoming payloads are printed to Serial; they are **not** correlated with a specific outbound tag in application state.

**Planned evolution**

1. **Contract with the gateway**  
   - Define a **JSON schema** (or minimal key set) for responses: e.g. `tag_id` or `correlation_id`, `status`, optional `message`, timestamp.  
   - Gateway publishes **after** processing the ingest message, to a topic the device already subscribes to (or a dedicated subtopic, e.g. `…/tag/ack`).

2. **Firmware behavior**  
   - Parse incoming JSON (lightweight parser or `ArduinoJson` if added).  
   - **Correlate** with the last N published tags (small ring buffer of pending IDs + timeout).  
   - Surface result to **Serial**, optional callback, or future API for UI/actuators.

3. **Reliability**  
   - Handle **duplicate** or **out-of-order** messages; ignore malformed payloads; optional QoS alignment with broker.

**Non-goals for this version (unless promoted from v0.5)**

- Full request/response RPC over MQTT with guaranteed pairing (may need message IDs in both directions).

---

## v0.5 — TBD

Reserved for scope decided after v0.3–v0.4 (e.g. OTA, offline queue, NVS config, Wi‑Fi provisioning, TLS for HTTP, second reader, or thesis-specific experiments).

---

_See [ARCHITECTURE.md](ARCHITECTURE.md) and [HARDWARE.md](HARDWARE.md) for the current system description and wiring._
