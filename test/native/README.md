# Host tests

Runs the parts of the firmware that are pure logic on a laptop, against a small stand-in
for the Arduino core. No PlatformIO, no board, no toolchain beyond a C++17 compiler:

```bash
./run.sh
```

Exit code 0 means every check passed; the output names any that did not.

It builds two binaries. The first always runs and covers what the node *does*. The second
covers the bytes it puts on the wire, and needs ArduinoJson — it is looked for in
`$ARDUINOJSON_DIR` and then in `.pio/libdeps/*/ArduinoJson/src`, so running `pio run` once
is enough to enable it. Without ArduinoJson that half is skipped with a notice and the run
still passes.

## What is covered

| File | Subject |
| --- | --- |
| `test_cache.cpp` | Per-UID cooldown, capacity and LRU eviction of the debounce cache |
| `test_r200.cpp` | Frame decoding: valid answers, truncation, bad checksums, hostile lengths, an EPC containing the frame-end byte |
| `test_uid.cpp` | UID formatting and comparison helpers |
| `test_gateway.cpp` | The uplink across a reconnection (link generation, presence, subscriptions, last will), the outbox retry policy, the telemetry topic fallback |
| `test_registrar.cpp` | The registration state machine: the attempt budget and what renews it, adoption of issued credentials, the once-per-boot revalidation |
| `test_access.cpp` | Which physical output each gateway status drives, and the granted / denied / degraded split |
| `test_codec.cpp` | *(needs ArduinoJson)* The real `GatewayMessages.cpp`: the exact JSON the node publishes, and what it accepts and rejects from the gateway |

Several of these are regression tests for bugs that were live in v0.1 — a checksum loop
that never terminated, an EPC read one byte off, frames truncated at the first `0xDD`.
They are here so those cannot come back quietly.

## Why the codec is tested twice

The behavioural suite links `gateway_codec_stub.cpp`, a hand-written stand-in for
`GatewayMessages.cpp`, so that it needs nothing but a compiler. That stand-in parses by
substring search: it is close enough to drive tests about *which topic, retained or not,
how many times, in what order*, and it says nothing at all about whether the firmware and
the gateway agree on the bytes. `test_codec.cpp` is the other half — the real
implementation, the real ArduinoJson, and assertions about exact payloads. Keeping them
apart is what lets the suite run anywhere while still pinning the contract where the
dependency is available.

## What is not covered

Anything that needs Wi-Fi, MQTT, HTTP or NVS. `arduino_stub/` deliberately stops short of
faking those: a stub convincing enough to test them would be testing the stub. Those paths
are exercised on hardware, against a real broker.

## Adding a test

1. Write `test_<subject>.cpp` with a `void test<Subject>()` entry point, using `CHECK` and
   `SECTION` from `test_support.h`.
2. Declare and call it in `test_main.cpp`.
3. Add the file to the first compile line in `run.sh`.

A test that needs ArduinoJson goes in the second binary instead, next to `test_codec.cpp`,
so the first one keeps building with nothing but a compiler.
