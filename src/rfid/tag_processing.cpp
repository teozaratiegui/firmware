#include "rfid/tag_processing.h"

#include "config/app_config.h"
#include "MessageGateway.h"
#include "R200.h"
#include "rfid/uid_utils.h"

void tagProcessorLoop(R200& rfid, Cache<kTagCacheCapacity>& gate, MessageGateway& msgGw,
                      TagProcessorState& st, uint32_t now) {
  const bool tagPresent = !isZeroUid(rfid.uid);

  if (tagPresent) {
    if (isLikelyFramingGarbageUid(rfid.uid)) {
      if (now - st.lastGarbageLog >= kGarbageLogIntervalMs) {
        st.lastGarbageLog = now;
        Serial.println(
            "RFID_SKIP: UID empieza con DD+AA (basura de trama UART), no se loguea ni POSTea.");
      }
    } else if (gate.shouldAccept(rfid.uid, now)) {
      const String uidStr = toUidString(rfid.uid);
      Serial.print("TAG_LOG ");
      Serial.println(msgGw.makeTagPayload(uidStr));

#if MESSAGE_GATEWAY
      Serial.println(msgGw.sendTag(uidStr) ? "[GW] sent" : "[GW] send FAILED");
#endif
    } else {
      if (now - st.lastCacheSkipLog >= kCacheSkipLogIntervalMs) {
        st.lastCacheSkipLog = now;
        Serial.print("RFID_CACHE_SKIP uid=");
        Serial.print(toUidString(rfid.uid));
        Serial.println(" (esperá cooldown o probá otro tag)");
      }
    }
  }
}
