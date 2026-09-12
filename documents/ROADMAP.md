# Roadmap

Versions follow [Semantic Versioning](https://semver.org/); `0.x` means the API and the
behaviour can still change.

---

## v0.2 — current

Bringing the node in line with the contract the Fog gateway actually implements, plus the
first tests.

**Provisioning**
- Registration by MAC against `<gateway prefix>/register`, credentials persisted in NVS.
- The `node_id` assigned by the gateway replaces the build-time `"001"`.
- A `403` wipes the stored identity and re-registers — by removing the two credential keys,
  not by clearing the NVS namespace, so the persistent outbox below can share it safely.
- Credentials are restored before the broker connection opens, so the last will names the
  node instead of carrying an empty `node_id`.
- The attempt budget is real: leaving `GaveUp` needs a new link generation, not just a live
  link. It used to last one superloop iteration, which made `kRegisterMaxAttempts` a no-op.
- One opportunistic revalidation per boot (`kRevalidateIdentityOnBoot`), since the
  gateway's `register_node` is idempotent by MAC — a node recovers on its own when the
  gateway loses its node table instead of waiting for the next tag.

**Contract**
- The MQTT payload is `{"tag","node_key","ts"}` — what the gateway parses.
- Responses on `…/responses` are parsed, not just printed: status, message, error,
  round-trip time, and a GPIO pulse when one is configured.
- Telemetry moved off the ingest topic onto `…/<node_id>/telemetry`, falling back to
  `…/<MAC>/telemetry` while the node has no id, and carrying the registration state — an
  unregistered node is no longer invisible to everything except a serial console.
- Retained presence document and last will on `…/<MAC>/status`, re-announced `online:true`
  after every reconnect.
- `500`/`503` are signalled as degradation, not as a refused tag. The door policy is
  unchanged (fail-closed); only what the node says about it changed.

**Reliability**
- Stable MQTT client id and `clean_session=false`; subscriptions replayed on reconnect, and
  an abandoned `…/<node_id>/responses` subscription is dropped on re-registration instead
  of being left behind in the broker's session.
- Bounded in-RAM outbox: a read taken while the link is down is relayed afterwards, and so
  is one that went out and was never answered (`kUnansweredReadRetries`, `0` to keep the
  old drop-on-timeout policy; `kOutboxCapacity = 0` disables the outbox outright).
- Exactly one read on the wire at a time. The gateway's answer carries no tag, so the
  in-flight slot is the only thing pairing a response with the read that caused it: a
  backlog now drains one read per answer instead of going out in a burst that overwrote
  the slot on every send, which reported one real round trip and zeroes for the rest, and
  retried only the last read of the burst while losing the others uncounted.
- Non-blocking Wi-Fi and broker reconnection — nothing in the loop calls `delay()` to wait,
  and `socketTimeoutS` caps how long one CONNECT attempt can block inside PubSubClient
  (its own default is 15 s of frozen superloop per retry).
- NTP, re-armed on every Wi-Fi rising edge rather than once at boot: a node that booted
  before its access point — the normal order after a power cut — never started SNTP at all
  and sent every read with no timestamp for the rest of its uptime.
- An identity NVS refused to store is reported as `registered_unpersisted` in telemetry
  instead of only on the serial console.

**Driver fixes**
- The checksum loop no longer runs off the end of a frame that declares a huge parameter
  length (it used to wrap a `uint16_t` index and never terminate).
- Frame reception is length-driven, so an EPC containing `0xDD` is no longer truncated.
- The EPC is read from byte 8, not 9 — every UID the firmware ever reported was shifted
  by one byte.
- `receiveData` returns as soon as the frame is complete instead of burning its whole
  timeout on every call.

**Tests**
- `test/native/` — 189 checks, runnable with nothing but a C++ compiler: the cache, the
  frame decoder, the UID helpers, and — against a hand-driven `FakeTransport` — the
  uplink's behaviour across a reconnection, the outbox retry policy, the registration state
  machine and the access-decision classification.
- A second binary, 61 more checks, builds the real `GatewayMessages.cpp` against the real
  ArduinoJson and asserts the exact bytes of the wire contract. It is built only when
  ArduinoJson is present (`pio run` once, or set `ARDUINOJSON_DIR`); without it the suite
  above still runs and the codec tests are skipped.
- Still no host coverage for `MqttTransport` or `node_identity`: they are PubSubClient and
  `Preferences` all the way down, and a stub convincing enough to test them would be
  testing the stub.

---

## v0.3 — measurement

The thesis has no measured number yet, and the firmware is where most of them come from.

- Export the counters the node already keeps (`tagReadsSent`, `tagReadsQueued`,
  `tagReadsDropped`, `responsesLost`, `readsAbandoned`, `lastLatencyMs`) as a structured
  serial line that a script can parse into a CSV. `lastLatencyMs` is a real round trip for
  every read since v0.2 paced the uplink to one in-flight read at a time.
- Widen that from `lastLatencyMs` to a distribution: the node measures every read but only
  keeps the most recent number, so a p95 still has to be reconstructed off-device.
- Compare `kUnansweredReadRetries = 0` against `2`: duplicate events upstream versus reads
  lost to a gateway restart. Both are one constant away, so the trade-off is measurable.
- Run the same tag sequence through MQTT and through HTTPS and compare end-to-end latency
  and bytes on the wire — the A/B bench that quantifies what the Fog layer contributes.
- Read-rate and read-range characterisation of the R200 with the actual antenna.

## v0.4 — resilience

- Task watchdog enabled, fed from the superloop.
- Outbox persisted to NVS or LittleFS so a power cut does not lose queued reads. If it goes
  to NVS it must not go in the `node` namespace shared with the credentials.
- Exponential backoff on registration, instead of a fixed interval. The link generation
  that gates `GaveUp` is where it hangs off.

## v0.5 — security

- MQTT over TLS (port 8883) with server certificate validation, and certificate
  validation for the HTTPS path — it currently runs with `setInsecure()`.
- Per-device broker credentials instead of the shared registration key.
- Move to an MQTT client that supports QoS 1 publish (`256dpi/arduino-mqtt`); PubSubClient
  has no API for it.

## v0.6 — remote configuration

Blocked on the gateway: there is no `…/<node_id>/config` topic to subscribe to
(gateway finding G7, in the project-level findings report). Once it exists:

- Reconfigure cooldown, poll interval and telemetry period without reflashing.
- OTA updates.

---

_See [ARCHITECTURE.md](ARCHITECTURE.md) for the current structure and [HARDWARE.md](HARDWARE.md) for the wiring._
