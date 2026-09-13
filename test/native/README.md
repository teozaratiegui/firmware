# Host tests

Runs the parts of the firmware that are pure logic on a laptop, against a small stand-in
for the Arduino core. No PlatformIO, no board, no toolchain beyond a C++17 compiler:

```bash
./run.sh
```

Exit code 0 means every check passed; the output names any that did not.

It builds two binaries, always both, so one run reports on both. The first covers what the
node *does*. The second covers the bytes it puts on the wire, and needs ArduinoJson — it is
looked for in `$ARDUINOJSON_DIR` and then in `.pio/libdeps/*/ArduinoJson/src`, so running
`pio run` once is enough to enable it.

**Without ArduinoJson the run fails.** The first binary parses with a stand-in that
duplicates the field names, so it cannot tell a correct contract from an unchecked one —
see *Why the codec is tested twice*. `ALLOW_SKIP_CODEC=1` accepts that trade deliberately
and says out loud that the contract went unverified.

## What is covered

| File | Subject |
| --- | --- |
| `test_cache.cpp` | Per-UID cooldown, capacity and LRU eviction of the debounce cache; the cooldown across the `millis()` rollover; that the UID length is part of the cache's type |
| `test_r200.cpp` | Frame decoding: valid answers, truncation, bad checksums, hostile lengths, an EPC containing the frame-end byte, an answer too short to hold one. Also the frames it emits — all four command checksums recomputed — and `linkTest()`: pass, corrupt answer, and bounded give-up when nothing is wired |
| `test_uid.cpp` | UID formatting and comparison helpers, and that each one takes its length from the buffer it was handed rather than from a macro |
| `test_gateway.cpp` | The uplink across a reconnection (link generation, presence, subscriptions, last will), the outbox retry policy, the telemetry topic fallback, and the direct-to-Lambda arm: delivery vs. decision, the round trip it reports, and what a real transport failure still does |
| `test_tag_processing.cpp` | The per-UID debounce between reader and uplink: one relay per cooldown per UID, that the window is per tag and not global, and what happens to a read when there is no outbox to keep it in |
| `test_registrar.cpp` | The registration state machine: the attempt budget and what renews it, adoption of issued credentials, the once-per-boot revalidation |
| `test_access.cpp` | Which physical output each gateway status drives, and the granted / denied / degraded split |
| `test_codec.cpp` | *(needs ArduinoJson)* The real `GatewayMessages.cpp`: the exact JSON the node publishes, and what it accepts and rejects from the gateway |

Several of these are regression tests for bugs that were live in v0.1 — a checksum loop
that never terminated, an EPC read one byte off, frames truncated at the first `0xDD`, UID
helpers that walked twelve bytes over whatever buffer they were given. They are here so
those cannot come back quietly.

A few checks are compile-time rather than runtime: `static_assert`s that a `Cache` or a UID
helper *rejects* a buffer of the wrong length. They are mirrored as `CHECK`s so they show
up in the count, but what they really assert is that the mismatch cannot be built at all.

## Why the codec is tested twice

The behavioural suite links `gateway_codec_stub.cpp`, a hand-written stand-in for
`GatewayMessages.cpp`, so that it needs nothing but a compiler. That stand-in parses by
substring search: it is close enough to drive tests about *which topic, retained or not,
how many times, in what order*, and it says nothing at all about whether the firmware and
the gateway agree on the bytes. `test_codec.cpp` is the other half — the real
implementation, the real ArduinoJson, and assertions about exact payloads. Keeping them
apart is what lets the suite run anywhere while still pinning the contract where the
dependency is available.

The stand-in **duplicates the field names**, which is why the split has to fail loudly
rather than skip: renaming `node_key` to `nodeKey` in the real `GatewayMessages.cpp` leaves
the behaviour binary entirely green, and only `test_codec.cpp` goes red.

## What is not covered

Anything that needs Wi-Fi, MQTT, HTTP or NVS. `arduino_stub/` deliberately stops short of
faking those: a stub convincing enough to test them would be testing the stub. Those paths
are exercised on hardware, against a real broker.

The one rule pulled back out of `HttpTransport.cpp` is `isDelivered`: it is static and in
the header precisely so *which statuses count as delivered* can be asserted without an
HTTPClient. Everything around it — the TLS setup, the timeouts, the POST — is not covered
here.

## Adding a test

1. Write `test_<subject>.cpp` with a `void test<Subject>()` entry point, using `CHECK` and
   `SECTION` from `test_support.h`.
2. Declare and call it in `test_main.cpp`.
3. Add the file to the first compile line in `run.sh`.

A test that needs ArduinoJson goes in the second binary instead, next to `test_codec.cpp`,
so the first one keeps building with nothing but a compiler.
