# Firmware architecture (ESP32 + R200)

How this PlatformIO project is laid out, and what happens to a tag between the antenna
and the gateway.

---

## 1. The shape of one read

```
R200 (UART)
  │  frame: AA | type | cmd | len | params | checksum | DD
  ▼
R200 driver            resync on header, read the declared length, verify the checksum
  │  uid[12]
  ▼
TagProcessor           per-UID cooldown (5 s) so a tag left in the field is not replayed
  │  "E28006900000500E88C6A4A7"
  ▼
MessageGateway         {"tag":…,"node_key":…,"ts":…}  →  <prefix>/<node_id>/requests
  │                    queues the read instead of losing it when the link is down
  ▼
Fog gateway → Cloud
  │
  ▼
<prefix>/<node_id>/responses  →  {"status":200}
  │
  ▼
AccessIndicator        parse, measure the round trip, classify the outcome
                       (granted / denied / degraded) and pulse a GPIO if one is configured
```

---

## 2. `src/` versus `lib/`

| Zone | Role |
| --- | --- |
| **`lib/`** | Reusable pieces with no idea what this particular node is for: the R200 driver, the debounce cache, the uplink and its transports. |
| **`src/`** | This node: which pins, which broker, which run mode, and the composition root that wires everything together. |

`lib/` never includes anything from `src/` — that is what lets the host test suite compile
the driver and the cache without an Arduino toolchain, and it is why every configuration
value arrives as a constructor argument rather than as a `#define` read from deep inside.

---

## 3. Modules

### `src/main.cpp` — composition root

Builds one `Application` struct holding every collaborator, hands each one its
dependencies, and runs the superloop. Nothing else in the firmware reaches for a global.

The `Application` is constructed in `setup()` rather than statically, because the MAC
address — the node's identity before the gateway gives it one — is only available after
the Wi-Fi stack is up.

### `src/config/app_config.h`

Every compile-time knob in one file: run mode, transport, pins, timings, topic prefixes,
broker address, GPIO for the access decision.

Two kinds live here and they are not interchangeable. The `#define`s carry an `#ifndef`
guard, so `build_flags` or `PLATFORMIO_BUILD_FLAGS` override them without editing the file;
the `static constexpr` values (timings, capacities, `kUnansweredReadRetries`,
`kRevalidateIdentityOnBoot`) have no such guard and a `-D` for one of them is a compile
error, not an override. The README's *Feature toggles* section splits them by that line.

The EPC length is deliberately absent: it belongs to the reader, so `R200::kEpcLength` owns
it and every UID buffer derives its length from there.

### `src/net/connectivity.{h,cpp}`

Wi-Fi station, non-blocking reconnect, NTP, and the node's hardware identity
(`macAddress()`, `macCompact()`). `isoTimestamp()` returns an empty string until NTP has
answered, so the firmware never passes a `millis()`-derived number off as wall-clock time.

SNTP is armed on every Wi-Fi rising edge, not once at boot. Arming it only at boot meant a
node that came up before its access point — the ordinary sequence after a power cut — never
called `configTime()` at all, and then omitted the timestamp from every read for the rest
of its uptime with nothing but a serial line to say so.

### `src/provisioning/` — how the node gets an identity

`node_identity` stores the `node_id` / `node_key` pair the gateway issues, in NVS.
`node_registrar` is a non-blocking state machine that runs the handshake:

```
                 ┌──────────────┐  credentials in NVS   ┌────────────┐
    boot ───────►│ Unregistered │──────────────────────►│ Registered │
                 └──────┬───────┘                       └────────────┘
                        │ SUBSCRIBE <gw>/register/response/<MAC>       ▲  │
                        │ PUBLISH   <gw>/register {"api_key","mac"}    │  │ one
                        ▼                                             │  │ revalidation
                ┌──────────────────┐   {"node_id","node_key"} → NVS    │  │ per boot
                │ AwaitingResponse │───────────────────────────────────┘◄─┘
                └────────┬─────────┘
                         │ 5 timeouts
                         ▼
                     ┌────────┐  new link generation
                     │ GaveUp │─────────────────────► Unregistered
                     └────────┘
```

`begin()` runs **before** the transport connects, because the last will is part of the
CONNECT packet: a node holding credentials has to know its `node_id` by then or its
testament says `node_id:""`.

Leaving `GaveUp` requires a genuinely new link, not merely a live one —
`MessageGateway::linkGeneration()` counts disconnected→connected transitions, and the
registrar records the one it gave up on. Without that gate `GaveUp` lasted a single
superloop iteration and a node with a bad `api_key` republished its registration forever.

A node that boots with credentials still re-registers **once**, because the gateway's
`register_node` is idempotent by MAC: if the gateway lost its node table, the node heals
itself instead of waiting for the next tag to discover it. Revalidation is opportunistic —
no answer means the node keeps using what NVS holds.

A `403` from the gateway wipes NVS and sends the node back to `Unregistered`: that is the
only status that means "these credentials are not valid any more", and it is what lets a
fleet recover on its own if the gateway loses its node table.

### `lib/MessageGateway/` — the uplink

| File | Responsibility |
| --- | --- |
| `TransportMode.h` | Strategy interface: connect, send, pub/sub, unsubscribe, last will |
| `MqttTransport` | MQTT to the Fog gateway; stable client id, `clean_session=false`, subscriptions replayed after every reconnect |
| `HttpTransport` | HTTPS straight to the Lambda Function URL, bypassing the Fog layer |
| `GatewayMessages` | Every JSON shape the node produces or consumes, in one file |
| `MessageGateway` | Topic layout, message routing, the outbox, round-trip timing, link generation |

`MessageGateway` holds a small **outbox**: when a read cannot go out it is queued rather
than dropped. It is bounded (`kOutboxCapacity`, oldest dropped; `0` disables it) and lives
in RAM, so it survives a broker or Wi-Fi outage but not a reboot.

"Cannot go out" covers three cases, not one: the link is down, the node has no credentials
the gateway would accept, or **an earlier read is still waiting for its answer**. That last
one is what paces the uplink to a single read in flight. The gateway's answer carries no
tag (its `NodeResponse`, see the gateway repository's `doc/node-manual.md` §
"Response (gateway publica)"), so the in-flight slot is the only thing pairing a
response with the read that caused it — and a backlog that drained in one burst overwrote
that slot on every send, which reported one real round trip and zeroes for the rest, and
put only the last read of the burst back on the outbox while the others disappeared
without incrementing a counter. The queue now drains one read per answer.

It covers a second failure mode too, and this is the likelier one on a single-Pi
deployment: the broker is up, the QoS 0 PUBLISH succeeds, and the gateway container that
should consume it is down. Nothing fails, nothing answers. A read that gets no answer
within `kResponseTimeoutMs` goes back on the outbox up to `kUnansweredReadRetries` times
and is then counted in `Stats::readsAbandoned`. Set the constant to `0` for the original
drop-on-timeout policy: retrying risks a duplicate event upstream when the answer was only
slow, so both policies are measurable on the A/B bench rather than hard-coded.

Reconnections are observable rather than merely detectable. `linkGeneration()` counts
disconnected→connected transitions, and on each rising edge the gateway re-states the
things a new broker session does not know: the response subscription and the retained
presence document. Without the latter, the `online:false` left by the last will stayed on
`…/<MAC>/status` for good after the first outage.

### `lib/R200/` — the reader

Frame reception is driven by the length the frame declares, not by scanning for the
frame-end byte: an EPC containing `0xDD` would otherwise be truncated. The declared length
is validated against what actually arrived before it is used as an index anywhere.

### `lib/Cache/` — debounce

Fixed-capacity table of UID → last accepted timestamp, with per-UID cooldown and
LRU eviction. No allocation, no `std::` container: it has to be predictable on a
microcontroller.

Capacity and UID length are both template parameters, and a UID arrives as a reference to
an array of exactly that length — so a buffer of the wrong size is a build error rather
than a read past the end of it. The cooldown arithmetic is unsigned and survives the
`millis()` rollover; the LRU comparison is not, and says so in a comment.

---

## 4. Topics

With `kNodePrefix = "bicicletero/esp"` and `kGatewayPrefix = "bicicletero/gateway"`:

| Topic | Direction | Payload |
| --- | --- | --- |
| `…/esp/<node_id>/requests` | node → gateway | `{"tag","node_key","ts"}` |
| `…/esp/<node_id>/responses` | gateway → node | `{"status","message"?,"error"?}` |
| `…/esp/<node_id>/telemetry` | node → *(nobody yet)* | uptime, heap, RSSI, IP, counters, provisioning state |
| `…/esp/<MAC>/telemetry` | node → *(nobody yet)* | the same, while the node has no `node_id` |
| `…/esp/<MAC>/status` | node → *(nobody yet)*, retained | `{"online":true\|false}`, also the last will |
| `…/gateway/register` | node → gateway | `{"api_key","mac"}` |
| `…/gateway/register/response/<MAC>` | gateway → node | `{"node_id","node_key"}` |

The telemetry and status topics have no handler on the gateway side yet. They are
deliberately outside `…/requests`, because a synthetic "tag" on the ingest topic shows up
upstream as an asset event that never happened.

Telemetry falls back to the MAC-keyed topic while the node has no `node_id`. A node that
cannot register was otherwise loud on `…/register`, which nobody watches, and silent
everywhere that is watched; its only trace was a serial console that a door-mounted node
does not have. It carries the registration state (`unregistered`, `awaiting_response`,
`registered`, `gave_up`) and the attempt count for the same reason. It never goes on
`…/<MAC>/status`: that one is retained and doubles as the last will.

---

## 5. Run modes and transports

`SYSTEM_MODE` decides whether the reader is driven. `GATEWAY_USE_MQTT` decides where the
data goes. They are independent.

|  | `SYSTEM_MODE_RFID` (default) | `SYSTEM_MODE_GATEWAY_INTERVAL` |
| --- | --- | --- |
| R200 initialised | yes | no |
| Tag reads relayed | yes | — |
| Telemetry published | yes | yes |
| Registers with the gateway | yes | yes |

The two transports send **the same information in the two shapes each side expects**,
which is what makes them comparable: running the same tags through MQTT and then through
HTTPS measures what the Fog layer costs and what it buys. That comparison is the
quantitative evidence the thesis is missing.

---

## 6. Testing

`test/native/` compiles the pure-logic modules against a small Arduino stub and runs them
on the host:

```bash
./test/native/run.sh
```

It covers the debounce cache, the R200 frame decoder (including the two bugs that used to
live there), the UID helpers, and — against a hand-driven `FakeTransport` — the uplink's
behaviour across a reconnection, the outbox retry policy and pacing, the registration state
machine and the access-decision classification.

A second binary pins the wire contract itself: the real `GatewayMessages.cpp` against the
real ArduinoJson, asserting the exact payloads the node publishes and what it accepts and
rejects from the gateway. It is built only when ArduinoJson is available, so the suite
above keeps running on a machine that has never used PlatformIO.

In that first binary two collaborators are substituted rather than faked in earnest: the
JSON codec (so it needs nothing but a compiler — the second binary is what covers the real
one) and NVS-backed credential storage. Anything that needs Wi-Fi, MQTT or `Preferences`
itself is not testable this way and is exercised on hardware — which is also why
`node_identity.cpp` and `MqttTransport` have no host coverage.

---

## 7. What is deliberately not here

- **No watchdog.** Every loop that could block is bounded by a timeout, but nothing
  recovers the node if one is ever unbounded again. Enabling the task watchdog is cheap
  and belongs in the next version.
- **No persistent outbox.** The queue is RAM-only; a reboot loses it. It does now cover
  the unanswered-read case as well as the failed-publish one, but only within one power
  cycle. When it moves to NVS it must not share the credentials namespace, which is why
  `node_identity::clear()` removes its two keys instead of clearing the namespace.
- **No QoS 1 publish.** PubSubClient has no API for it. Subscribe is QoS 1 and the session
  is persistent, so the downlink is covered; the uplink is best-effort and is confirmed by
  the gateway's own answer.
- **No runtime configuration.** Every knob is compile-time, because the gateway exposes no
  downlink topic for node configuration.
