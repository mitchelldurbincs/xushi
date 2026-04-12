#pragma once

#include <cstdio>

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(expr, name) do { \
    tests_run++; \
    if (expr) { \
        tests_passed++; \
        std::printf("  PASS  %s\n", name); \
    } else { \
        std::printf("  FAIL  %s\n", name); \
    } \
} while(0)

#define TEST_REPORT() do { \
    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run); \
    return (tests_passed == tests_run) ? 0 : 1; \
} while(0)
