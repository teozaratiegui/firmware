#include "rfid/tag_processing.h"

#include "R200.h"
#include "rfid/uid_utils.h"

namespace rfid {

TagProcessor::TagProcessor(R200& reader, MessageGateway& gateway)
    : reader_(reader), gateway_(gateway), cache_(kTagCooldownMs) {}

void TagProcessor::loop(uint32_t nowMs) {
  tagPresent_ = !isZeroUid(reader_.uid);
  if (!tagPresent_) return;

  if (!cache_.shouldAccept(reader_.uid, nowMs)) {
    if (nowMs - lastSkipLogMs_ >= kCacheSkipLogIntervalMs) {
      lastSkipLogMs_ = nowMs;
      Serial.print("[RFID] within cooldown, not relayed: ");
      Serial.println(toUidString(reader_.uid));
    }
    return;
  }

  const String uid = toUidString(reader_.uid);
  accepted_++;

  // Log what actually went out, not a second build of it: the payload carries a
  // timestamp, and building it twice could print a `ts` that never left.
  String published;
  if (gateway_.sendTagRead(uid, &published)) {
    Serial.print("[RFID] tag relayed ");
    Serial.println(published);
    return;
  }
  // Not necessarily an outage: a read also queues while an earlier one is still
  // waiting for the gateway's answer, because only one can be in flight at a
  // time. Either way the read is kept, not lost — unless there is no outbox to
  // keep it in, which is what serial-only bring-up configures. Saying "queued"
  // there would be a lie, and the read was still worth printing: seeing the UID
  // is the whole point of that mode.
  Serial.print(gateway_.storeAndForwardEnabled() ? "[RFID] tag accepted, not sent yet — queued: "
                                                 : "[RFID] tag read, uplink disabled: ");
  Serial.println(uid);
}

}  // namespace rfid
