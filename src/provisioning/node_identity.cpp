#include "provisioning/node_identity.h"

#include "config/app_config.h"

#include <Preferences.h>

namespace provisioning {
namespace {

constexpr const char kKeyNodeId[]  = "node_id";
constexpr const char kKeyNodeKey[] = "node_key";

}  // namespace

Credentials load() {
  Preferences prefs;
  Credentials credentials;

  if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return credentials;
  credentials.nodeId  = prefs.getString(kKeyNodeId, "");
  credentials.nodeKey = prefs.getString(kKeyNodeKey, "");
  prefs.end();

  return credentials;
}

bool store(const Credentials& credentials) {
  if (!credentials.valid()) return false;

  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    Serial.println("[PROV] ERROR: could not open NVS for writing.");
    return false;
  }
  const bool ok = prefs.putString(kKeyNodeId, credentials.nodeId) > 0 &&
                  prefs.putString(kKeyNodeKey, credentials.nodeKey) > 0;
  prefs.end();

  if (!ok) Serial.println("[PROV] ERROR: could not persist credentials.");
  return ok;
}

// Removes the two keys rather than clearing the namespace. Today they are the
// only things in it, so the two are equivalent — but the outbox is due to be
// persisted here (ROADMAP v0.4) and it would naturally land in the same
// namespace, where a clear() triggered by a single 403 would throw away queued
// tag reads that have nothing to do with the credentials.
void clear() {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return;
  prefs.remove(kKeyNodeId);
  prefs.remove(kKeyNodeKey);
  prefs.end();
  Serial.println("[PROV] stored credentials wiped.");
}

}  // namespace provisioning
