#!/usr/bin/env bash
# Builds and runs the host-side test suite. Needs only a C++17 compiler:
#     ./test/native/run.sh
#
# Two binaries come out of this:
#
#   behaviour  always built. Links a hand-written stand-in for the JSON codec
#              (gateway_codec_stub.cpp) so the suite runs on a machine that has
#              never seen PlatformIO. It covers what the node *does*: which
#              topic, retained or not, how many times, in what order.
#
#   codec      built only when ArduinoJson can be found. Links the real
#              lib/MessageGateway/GatewayMessages.cpp against the real
#              ArduinoJson and asserts the exact bytes on the wire — the
#              contract with the Fog gateway, which the stand-in cannot vouch
#              for because it parses by substring search.
#
# ArduinoJson is looked for in $ARDUINOJSON_DIR first, then where PlatformIO
# unpacks it. `pio run` once and the codec binary starts building itself.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$HERE/../.."
OUT="${TMPDIR:-/tmp}/edge-native-tests"

COMMON_INCLUDES=(-I"$HERE" -I"$HERE/arduino_stub" -I"$ROOT/src" -I"$ROOT/lib/Cache"
                 -I"$ROOT/lib/R200" -I"$ROOT/lib/MessageGateway")

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O1 \
  "${COMMON_INCLUDES[@]}" \
  -o "$OUT" \
  "$HERE/arduino_stub/arduino_stub.cpp" \
  "$ROOT/lib/R200/R200.cpp" \
  "$ROOT/lib/MessageGateway/MessageGateway.cpp" \
  "$ROOT/src/provisioning/node_registrar.cpp" \
  "$ROOT/src/app/access_indicator.cpp" \
  "$HERE/gateway_codec_stub.cpp" "$HERE/provisioning_stubs.cpp" \
  "$HERE/test_main.cpp" "$HERE/test_cache.cpp" "$HERE/test_r200.cpp" "$HERE/test_uid.cpp" \
  "$HERE/test_registrar.cpp" "$HERE/test_gateway.cpp" \
  "$HERE/test_access.cpp"

"$OUT"

# ── The wire codec, against the real ArduinoJson ─────────────────────────────

find_arduinojson() {
  if [[ -n "${ARDUINOJSON_DIR:-}" && -f "$ARDUINOJSON_DIR/ArduinoJson.h" ]]; then
    printf '%s' "$ARDUINOJSON_DIR"
    return 0
  fi
  local candidate
  for candidate in "$ROOT"/.pio/libdeps/*/ArduinoJson/src; do
    if [[ -f "$candidate/ArduinoJson.h" ]]; then
      printf '%s' "$candidate"
      return 0
    fi
  done
  return 1
}

if ! JSON_DIR="$(find_arduinojson)"; then
  echo
  echo "SKIPPED: the wire codec tests need ArduinoJson, which was not found."
  echo "         Run 'pio run' once to fetch it, or set ARDUINOJSON_DIR to a checkout."
  echo "         The suite above does not depend on it."
  exit 0
fi

echo
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O1 \
  "${COMMON_INCLUDES[@]}" -I"$JSON_DIR" \
  -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_PROGMEM=0 \
  -o "$OUT-codec" \
  "$HERE/arduino_stub/arduino_stub.cpp" \
  "$ROOT/lib/MessageGateway/GatewayMessages.cpp" \
  "$HERE/test_codec.cpp"

"$OUT-codec"
