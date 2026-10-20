/* Minimal test helpers: each test is a function; CHECK records failures. */
#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int test_failures;
extern int test_checks;

#define CHECK(cond) do { test_checks++; if (!(cond)) { test_failures++; \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_INT(a, b) do { long _a = (long)(a), _b = (long)(b); test_checks++; if (_a != _b) { \
    test_failures++; fprintf(stderr, "%s:%d: %s == %ld, expected %ld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); test_checks++; \
    if (!_a || !_b || strcmp(_a, _b) != 0) { test_failures++; \
    fprintf(stderr, "%s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, _a ? _a : "(null)", _b ? _b : "(null)"); } } while (0)

#define RUN(fn) do { if (getenv("TEST_VERBOSE")) fprintf(stderr, "- %s\n", #fn); fn(); } while (0)

#endif
