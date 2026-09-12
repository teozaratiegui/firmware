#pragma once
#include <cstdio>

// Tiny assertion harness — no framework, so the suite runs anywhere a C++
// compiler is installed, with or without PlatformIO.
extern int g_failures;
extern int g_checks;

#define CHECK(cond)                                                              \
  do {                                                                           \
    g_checks++;                                                                  \
    if (!(cond)) {                                                               \
      g_failures++;                                                              \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
    }                                                                            \
  } while (0)

#define SECTION(name) std::printf("- %s\n", name)
