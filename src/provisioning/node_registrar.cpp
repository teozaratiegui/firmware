#include "provisioning/node_registrar.h"

#include "net/connectivity.h"

namespace provisioning {

NodeRegistrar::NodeRegistrar(MessageGateway& gateway, String apiKey, uint8_t maxAttempts,
                             uint32_t timeoutMs, bool revalidateOnBoot)
    : gateway_(gateway),
      apiKey_(std::move(apiKey)),
      maxAttempts_(maxAttempts),
      timeoutMs_(timeoutMs),
      revalidateOnBoot_(revalidateOnBoot) {}

void NodeRegistrar::begin() {
  const Credentials stored = load();
  if (stored.valid()) {
    gateway_.setIdentity(stored.nodeId, stored.nodeKey);
    state_             = State::Registered;
    persisted_         = true;
    revalidatePending_ = revalidateOnBoot_;
    Serial.printf("[PROV] identity restored from NVS: node_id=%s%s\n", stored.nodeId.c_str(),
                  revalidatePending_ ? " (will revalidate once)" : "");
    return;
  }
  Serial.printf("[PROV] no stored identity — will register as mac=%s\n",
                net::macAddress().c_str());
}

const char* NodeRegistrar::stateName() const {
  switch (state_) {
    case State::Unregistered:     return "unregistered";
    case State::AwaitingResponse: return "awaiting_response";
    case State::Registered:       return persisted_ ? "registered" : "registered_unpersisted";
    case State::GaveUp:           return "gave_up";
  }
  return "unknown";
}

void NodeRegistrar::reset() {
  // Idempotent on purpose: several tag reads can be answered 403 in a row, and
  // each one must not cost another NVS erase.
  if (state_ != State::Registered) return;

  clear();
  gateway_.clearIdentity();
  state_      = State::Unregistered;
  attempts_   = 0;
  deadlineMs_ = 0;
  persisted_  = true;
  // The identity it would have revalidated no longer exists; the full
  // registration below replaces it.
  revalidatePending_ = false;
}

void NodeRegistrar::loop() {
  if (!gateway_.isConnected()) return;
  if (!ensureSubscribed()) return;

  // Credentials in NVS are not proof that the gateway still knows this node: it
  // is one `docker compose up --build` away from losing its nodes table
  // (gateway finding G4), and the node would then sit on dead credentials until
  // the next tag — at night, in RFID mode, the whole fleet silently. Since
  // register_node is idempotent by MAC (register_node.py:48-52), re-registering
  // returns the same credentials and re-marks the node ACTIVE, so it self-heals.
  //
  // The cost, stated plainly: the gateway's answer republishes this node's
  // node_key on <gateway prefix>/register/response/<MAC>, a topic the Node-RED
  // flow is subscribed to by wildcard (gateway finding G10). That exposure
  // already exists on every registration; this makes it happen once per boot
  // as well. It is a marginal worsening of a known hole, not a new one.
  if (revalidatePending_) {
    revalidatePending_ = false;
    Serial.println("[PROV] revalidating the stored identity against the gateway.");
    publishRegistration();
  }

  if (state_ == State::Registered) return;

  switch (state_) {
    case State::Unregistered:
      requestRegistration();
      break;

    case State::AwaitingResponse:
      if (static_cast<int32_t>(millis() - deadlineMs_) < 0) break;
      if (attempts_ >= maxAttempts_) {
        state_        = State::GaveUp;
        gaveUpOnLink_ = gateway_.linkGeneration();
        Serial.printf(
            "[PROV] gave up after %u attempts on link generation %u. Check NODE_API_KEY in "
            "secrets.h and that the gateway is running. Retrying on the next reconnect.\n",
            static_cast<unsigned>(attempts_), static_cast<unsigned>(gaveUpOnLink_));
        break;
      }
      requestRegistration();
      break;

    case State::GaveUp:
      // A fresh link is a fresh chance — but only a genuinely fresh one. Before
      // this was gated on the link generation, GaveUp lasted a single superloop
      // iteration: a node with a bad api_key republished its registration every
      // few seconds forever and kRegisterMaxAttempts did nothing at all.
      if (gateway_.linkGeneration() == gaveUpOnLink_) break;
      Serial.printf("[PROV] link generation %u — registration budget renewed.\n",
                    static_cast<unsigned>(gateway_.linkGeneration()));
      attempts_ = 0;
      state_    = State::Unregistered;
      break;

    case State::Registered:
      break;
  }
}

bool NodeRegistrar::ensureSubscribed() {
  // The subscription has to exist before the request goes out, or the answer
  // is published into the void.
  if (subscribed_) return true;
  subscribed_ = gateway_.subscribeTopic(
      gateway_.registerResponseTopic(),
      [this](const String& payload) { onRegisterResponse(payload); });
  return subscribed_;
}

void NodeRegistrar::publishRegistration() {
  gateway_.publishTopic(gateway_.registerTopic(),
                        messages::registerRequest(apiKey_, net::macAddress()));
}

void NodeRegistrar::requestRegistration() {
  attempts_++;
  Serial.printf("[PROV] register attempt %u/%u mac=%s\n", static_cast<unsigned>(attempts_),
                static_cast<unsigned>(maxAttempts_), net::macAddress().c_str());

  publishRegistration();
  deadlineMs_ = millis() + timeoutMs_;
  state_      = State::AwaitingResponse;
}

void NodeRegistrar::onRegisterResponse(const String& payload) {
  const RegisterCredentials issued = RegisterCredentials::parse(payload);
  if (!issued.valid) {
    Serial.print("[PROV] malformed registration response: ");
    Serial.println(payload);
    return;
  }

  const Credentials credentials{issued.nodeId, issued.nodeKey};
  // A failed NVS write is not fatal — the node runs on the identity it just got
  // — but it is not harmless either: it re-registers on the next boot and leaves
  // another abandoned node behind in the gateway's table every time. Reported in
  // telemetry rather than only on the serial console.
  persisted_ = store(credentials);
  gateway_.setIdentity(credentials.nodeId, credentials.nodeKey);
  state_ = State::Registered;

  Serial.printf("[PROV] registered as node_id=%s%s\n", credentials.nodeId.c_str(),
                persisted_ ? "" : " (NOT persisted — will re-register on the next boot)");
}

}  // namespace provisioning
