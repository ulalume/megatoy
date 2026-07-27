#pragma once

// `assert` compiles away under NDEBUG, which Release builds define -- so the
// test suite would silently pass without checking anything. CHECK always
// evaluates its condition.

#include <cstdio>
#include <cstdlib>

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr, "CHECK failed: %s\n  at %s:%d\n", #expr, __FILE__,  \
                   __LINE__);                                                  \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
