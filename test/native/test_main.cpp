#include <cstdio>

int g_failures = 0;
int g_checks   = 0;

void testCache();
void testR200();
void testUid();
void testRegistrar();
void testGateway();
void testTagProcessing();
void testAccess();

int main() {
  std::printf("Edge firmware — native tests\n\n");
  testCache();
  testR200();
  testUid();
  testRegistrar();
  testGateway();
  testTagProcessing();
  testAccess();

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
