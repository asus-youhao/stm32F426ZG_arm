/**
 * @file test_framework.h
 * @brief 極簡單元測試框架（純 C,無相依）
 */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <math.h>

extern int g_tests, g_fails;

#define CHECK(cond) do { \
    g_tests++; \
    if (!(cond)) { g_fails++; printf("  [FAIL] %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_NEAR(a, b, tol) do { \
    g_tests++; \
    double _d = fabs((double)(a) - (double)(b)); \
    if (_d > (tol)) { g_fails++; \
        printf("  [FAIL] %s:%d  |%g - %g| = %g > %g\n", __FILE__, __LINE__, \
               (double)(a), (double)(b), _d, (double)(tol)); } \
} while (0)

#define RUN(test_fn) do { \
    printf("== %s ==\n", #test_fn); \
    int before = g_fails; \
    test_fn(); \
    printf("   %s\n", (g_fails == before) ? "ok" : "HAS FAILURES"); \
} while (0)

#endif /* TEST_FRAMEWORK_H */
