#include "provisioning_stubs.h"

#include "net/connectivity.h"

namespace testing {

FakeNvs fakeNvs;
String  fakeMac = String("AA:BB:CC:DD:EE:FF");

}  // namespace testing

namespace provisioning {

Credentials load() {
  return testing::fakeNvs.stored;
}

bool store(const Credentials& credentials) {
  if (!credentials.valid() || !testing::fakeNvs.writable) return false;
  testing::fakeNvs.stored = credentials;
  testing::fakeNvs.writes++;
  return true;
}

void clear() {
  testing::fakeNvs.stored = Credentials{};
  testing::fakeNvs.clears++;
}

}  // namespace provisioning

// Only the pieces of net/ that the registrar touches. Wi-Fi itself is not
// faked: a stub convincing enough to test it would be testing the stub.
namespace net {

String macAddress() {
  return testing::fakeMac;
}

}  // namespace net
