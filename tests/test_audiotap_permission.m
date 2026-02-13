// Test suite for audiotap_permission.m
// Uses the same compile-time mock approach as other test files.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audiotap.h"

// ============================================================================
// Test framework (same macros as other test files)
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %-50s", #name); \
    tests_run++; \
    name(); \
    tests_passed++; \
    printf(" PASS\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAIL\n    assertion failed: %s\n    at %s:%d\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf(" FAIL\n    expected %d == %d\n    at %s:%d\n", (int)(a), (int)(b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

// ============================================================================
// Include the source under test (mocks are active via AUDIOTAP_TESTING)
// ============================================================================

#include "../src/audiotap_permission.m"

// ============================================================================
// Tests
// ============================================================================

TEST(test_permission_status_unknown)
{
    mock_mic_permission = AUDIOTAP_PERMISSION_UNKNOWN;
    ASSERT_EQ(audiotap_mic_permission_status(), AUDIOTAP_PERMISSION_UNKNOWN);
}

TEST(test_permission_status_granted)
{
    mock_mic_permission = AUDIOTAP_PERMISSION_GRANTED;
    ASSERT_EQ(audiotap_mic_permission_status(), AUDIOTAP_PERMISSION_GRANTED);
}

TEST(test_permission_status_denied)
{
    mock_mic_permission = AUDIOTAP_PERMISSION_DENIED;
    ASSERT_EQ(audiotap_mic_permission_status(), AUDIOTAP_PERMISSION_DENIED);
}

TEST(test_request_permission_granted)
{
    mock_mic_permission = AUDIOTAP_PERMISSION_GRANTED;
    ASSERT_EQ(audiotap_request_mic_permission(), AUDIOTAP_PERMISSION_GRANTED);
}

TEST(test_request_permission_denied)
{
    mock_mic_permission = AUDIOTAP_PERMISSION_DENIED;
    ASSERT_EQ(audiotap_request_mic_permission(), AUDIOTAP_PERMISSION_DENIED);
}

TEST(test_request_permission_unknown)
{
    mock_mic_permission = AUDIOTAP_PERMISSION_UNKNOWN;
    ASSERT_EQ(audiotap_request_mic_permission(), AUDIOTAP_PERMISSION_UNKNOWN);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    printf("audiotap_permission test suite\n");
    printf("==============================\n\n");

    printf("permission_status:\n");
    RUN_TEST(test_permission_status_unknown);
    RUN_TEST(test_permission_status_granted);
    RUN_TEST(test_permission_status_denied);

    printf("\nrequest_permission:\n");
    RUN_TEST(test_request_permission_granted);
    RUN_TEST(test_request_permission_denied);
    RUN_TEST(test_request_permission_unknown);

    printf("\n==============================\n");
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
