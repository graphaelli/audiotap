// Test suite for the bridge ring buffer with 100% code coverage.
//
// Strategy: include bridge.c directly to access static functions,
// provide mock stubs for audiotap_create_system / audiotap_create_mic.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

// ============================================================================
// Test framework (matches project convention)
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %-60s", #name); \
    tests_run++; \
    name(); \
    tests_passed++; \
    printf(" PASS\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAIL\n    assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        printf(" FAIL\n    expected %lld == %lld\n    at %s:%d\n", \
               _a, _b, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_FLOAT_EQ(a, b) do { \
    float _a = (a), _b = (b); \
    if (fabsf(_a - _b) > 1e-9f) { \
        printf(" FAIL\n    expected %f == %f\n    at %s:%d\n", \
               (double)_a, (double)_b, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)
#define ASSERT_NULL(p) ASSERT((p) == NULL)

// ============================================================================
// Mock audiotap functions (bridge_create_system/mic call these)
// ============================================================================

#include "../../include/audiotap.h"

// Minimal definition so we can return a pointer.
struct audiotap_t { int dummy; };

static audiotap_t mock_system_tap;
static audiotap_t mock_mic_tap;
static int mock_system_create_call_count = 0;
static int mock_mic_create_call_count = 0;
static int mock_system_create_fail = 0;
static int mock_mic_create_fail = 0;

// Capture last config for verification.
static audiotap_system_config_t last_system_config;
static audiotap_mic_config_t last_mic_config;

audiotap_t *audiotap_create_system(const audiotap_system_config_t *config) {
    mock_system_create_call_count++;
    memcpy(&last_system_config, config, sizeof(*config));
    if (mock_system_create_fail) return NULL;
    return &mock_system_tap;
}

audiotap_t *audiotap_create_mic(const audiotap_mic_config_t *config) {
    mock_mic_create_call_count++;
    memcpy(&last_mic_config, config, sizeof(*config));
    if (mock_mic_create_fail) return NULL;
    return &mock_mic_tap;
}

static void reset_mocks(void) {
    mock_system_create_call_count = 0;
    mock_mic_create_call_count = 0;
    mock_system_create_fail = 0;
    mock_mic_create_fail = 0;
    memset(&last_system_config, 0, sizeof(last_system_config));
    memset(&last_mic_config, 0, sizeof(last_mic_config));
}

// ============================================================================
// Include bridge.c directly to access static bridge_callback
// ============================================================================

#include "../bridge.c"

// ============================================================================
// Tests: bridge_create / bridge_destroy
// ============================================================================

TEST(test_create_returns_non_null) {
    bridge_state_t *b = bridge_create();
    ASSERT_NOT_NULL(b);
    bridge_destroy(b);
}

TEST(test_create_initialises_positions) {
    bridge_state_t *b = bridge_create();
    ASSERT_EQ(atomic_load(&b->write_pos), 0);
    ASSERT_EQ(atomic_load(&b->read_pos), 0);
    ASSERT_EQ(atomic_load(&b->closed), 0);
    bridge_destroy(b);
}

TEST(test_destroy_null_is_safe) {
    bridge_destroy(NULL); // must not crash
}

TEST(test_close_null_is_safe) {
    bridge_close(NULL); // must not crash
}

// ============================================================================
// Tests: bridge_callback
// ============================================================================

TEST(test_callback_null_state) {
    float samples[] = {1.0f, 2.0f};
    bridge_callback(samples, 2, 1, 0, NULL); // must not crash
}

TEST(test_callback_after_close) {
    bridge_state_t *b = bridge_create();
    bridge_close(b);

    float samples[] = {1.0f, 2.0f};
    uint32_t before = atomic_load(&b->write_pos);
    bridge_callback(samples, 2, 1, 0, b);
    uint32_t after = atomic_load(&b->write_pos);
    ASSERT_EQ(before, after); // write_pos must not change

    bridge_destroy(b);
}

TEST(test_callback_basic_write) {
    bridge_state_t *b = bridge_create();

    float samples[] = {0.5f, 1.5f, 2.5f};
    bridge_callback(samples, 3, 1, 0, b);

    ASSERT_EQ(atomic_load(&b->write_pos), 3);
    ASSERT_FLOAT_EQ(b->ring[0], 0.5f);
    ASSERT_FLOAT_EQ(b->ring[1], 1.5f);
    ASSERT_FLOAT_EQ(b->ring[2], 2.5f);

    bridge_destroy(b);
}

TEST(test_callback_stereo) {
    bridge_state_t *b = bridge_create();

    // 2 frames × 2 channels = 4 floats
    float samples[] = {1.0f, 2.0f, 3.0f, 4.0f};
    bridge_callback(samples, 2, 2, 0, b);

    ASSERT_EQ(atomic_load(&b->write_pos), 4);
    ASSERT_FLOAT_EQ(b->ring[0], 1.0f);
    ASSERT_FLOAT_EQ(b->ring[3], 4.0f);

    bridge_destroy(b);
}

TEST(test_callback_wraparound) {
    bridge_state_t *b = bridge_create();

    // Advance write_pos to near the end of the ring.
    uint32_t near_end = RING_SIZE - 4;
    float fill[RING_SIZE];
    for (uint32_t i = 0; i < near_end; i++) fill[i] = (float)i;
    bridge_callback(fill, near_end, 1, 0, b);
    ASSERT_EQ(atomic_load(&b->write_pos), near_end);

    // Drain so the ring doesn't overflow.
    float drain[RING_SIZE];
    int n = bridge_read(b, drain, RING_SIZE);
    ASSERT_EQ(n, (int)near_end);

    // Now write 8 samples that must wrap: 4 fit at the end, 4 wrap to start.
    float wrap_data[] = {100.0f, 101.0f, 102.0f, 103.0f,
                         104.0f, 105.0f, 106.0f, 107.0f};
    bridge_callback(wrap_data, 8, 1, 0, b);

    // Read back and verify.
    float out[8];
    n = bridge_read(b, out, 8);
    ASSERT_EQ(n, 8);
    for (int i = 0; i < 8; i++) {
        ASSERT_FLOAT_EQ(out[i], 100.0f + (float)i);
    }

    bridge_destroy(b);
}

TEST(test_callback_overflow_drops_oldest) {
    bridge_state_t *b = bridge_create();

    // Fill the ring completely.
    float *fill = calloc(RING_SIZE, sizeof(float));
    ASSERT_NOT_NULL(fill);
    for (uint32_t i = 0; i < RING_SIZE; i++) fill[i] = (float)i;
    bridge_callback(fill, RING_SIZE, 1, 0, b);
    ASSERT_EQ(atomic_load(&b->write_pos), RING_SIZE);
    ASSERT_EQ(atomic_load(&b->read_pos), 0);

    // Overflow by 100: oldest 100 samples should be dropped.
    float overflow[100];
    for (int i = 0; i < 100; i++) overflow[i] = -(float)(i + 1);
    bridge_callback(overflow, 100, 1, 0, b);

    ASSERT_EQ(atomic_load(&b->read_pos), 100);

    // Read everything back.
    float *out = calloc(RING_SIZE, sizeof(float));
    ASSERT_NOT_NULL(out);
    int n = bridge_read(b, out, RING_SIZE);
    ASSERT_EQ(n, RING_SIZE);

    // First (RING_SIZE - 100) values should be 100..RING_SIZE-1 (surviving originals).
    for (int i = 0; i < (int)RING_SIZE - 100; i++) {
        ASSERT_FLOAT_EQ(out[i], (float)(i + 100));
    }
    // Last 100 values should be -1..-100 (the overflow data).
    for (int i = 0; i < 100; i++) {
        ASSERT_FLOAT_EQ(out[RING_SIZE - 100 + i], -(float)(i + 1));
    }

    free(fill);
    free(out);
    bridge_destroy(b);
}

// ============================================================================
// Tests: bridge_read
// ============================================================================

TEST(test_read_null_returns_error) {
    float buf[10];
    ASSERT_EQ(bridge_read(NULL, buf, 10), -1);
}

TEST(test_read_basic) {
    bridge_state_t *b = bridge_create();

    float in[] = {10.0f, 20.0f, 30.0f};
    bridge_callback(in, 3, 1, 0, b);

    float out[3];
    int n = bridge_read(b, out, 3);
    ASSERT_EQ(n, 3);
    ASSERT_FLOAT_EQ(out[0], 10.0f);
    ASSERT_FLOAT_EQ(out[1], 20.0f);
    ASSERT_FLOAT_EQ(out[2], 30.0f);

    bridge_destroy(b);
}

TEST(test_read_partial) {
    bridge_state_t *b = bridge_create();

    float in[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    bridge_callback(in, 5, 1, 0, b);

    // Read only 3 of 5 available.
    float out[3];
    int n = bridge_read(b, out, 3);
    ASSERT_EQ(n, 3);
    ASSERT_FLOAT_EQ(out[0], 1.0f);
    ASSERT_FLOAT_EQ(out[2], 3.0f);

    // Read remaining 2.
    float out2[2];
    n = bridge_read(b, out2, 2);
    ASSERT_EQ(n, 2);
    ASSERT_FLOAT_EQ(out2[0], 4.0f);
    ASSERT_FLOAT_EQ(out2[1], 5.0f);

    bridge_destroy(b);
}

TEST(test_read_multiple_writes) {
    bridge_state_t *b = bridge_create();

    float a[] = {1.0f, 2.0f};
    float c[] = {3.0f, 4.0f, 5.0f};
    bridge_callback(a, 2, 1, 0, b);
    bridge_callback(c, 3, 1, 0, b);

    float out[5];
    int n = bridge_read(b, out, 5);
    ASSERT_EQ(n, 5);
    for (int i = 0; i < 5; i++) {
        ASSERT_FLOAT_EQ(out[i], (float)(i + 1));
    }

    bridge_destroy(b);
}

TEST(test_read_wraparound) {
    bridge_state_t *b = bridge_create();

    // Fill and drain to advance both pointers near the end.
    uint32_t near_end = RING_SIZE - 3;
    float *fill = calloc(near_end, sizeof(float));
    ASSERT_NOT_NULL(fill);
    bridge_callback(fill, near_end, 1, 0, b);
    float *drain = calloc(near_end, sizeof(float));
    ASSERT_NOT_NULL(drain);
    bridge_read(b, drain, near_end);

    // Write 6 samples: 3 at end, 3 wrapped to start.
    float in[] = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f};
    bridge_callback(in, 6, 1, 0, b);

    float out[6];
    int n = bridge_read(b, out, 6);
    ASSERT_EQ(n, 6);
    for (int i = 0; i < 6; i++) {
        ASSERT_FLOAT_EQ(out[i], (float)((i + 1) * 10));
    }

    free(fill);
    free(drain);
    bridge_destroy(b);
}

TEST(test_read_after_close_with_data) {
    bridge_state_t *b = bridge_create();

    float in[] = {7.0f, 8.0f, 9.0f};
    bridge_callback(in, 3, 1, 0, b);
    bridge_close(b);

    // First read: returns buffered data.
    float out[3];
    int n = bridge_read(b, out, 3);
    ASSERT_EQ(n, 3);
    ASSERT_FLOAT_EQ(out[0], 7.0f);

    // Second read: ring empty + closed → 0.
    n = bridge_read(b, out, 3);
    ASSERT_EQ(n, 0);

    bridge_destroy(b);
}

TEST(test_read_after_close_empty) {
    bridge_state_t *b = bridge_create();
    bridge_close(b);

    float out[10];
    int n = bridge_read(b, out, 10);
    ASSERT_EQ(n, 0);

    bridge_destroy(b);
}

// ============================================================================
// Tests: bridge_write_samples (public wrapper around callback)
// ============================================================================

TEST(test_write_samples) {
    bridge_state_t *b = bridge_create();

    float in[] = {42.0f, 43.0f};
    bridge_write_samples(b, in, 2);

    float out[2];
    int n = bridge_read(b, out, 2);
    ASSERT_EQ(n, 2);
    ASSERT_FLOAT_EQ(out[0], 42.0f);
    ASSERT_FLOAT_EQ(out[1], 43.0f);

    bridge_destroy(b);
}

// ============================================================================
// Tests: bridge_close
// ============================================================================

TEST(test_close_sets_flag) {
    bridge_state_t *b = bridge_create();
    ASSERT_EQ(atomic_load(&b->closed), 0);
    bridge_close(b);
    ASSERT_EQ(atomic_load(&b->closed), 1);
    bridge_destroy(b);
}

TEST(test_double_close_is_safe) {
    bridge_state_t *b = bridge_create();
    bridge_close(b);
    bridge_close(b); // must not crash
    bridge_destroy(b);
}

// ============================================================================
// Tests: bridge_create_system / bridge_create_mic (mock verification)
// ============================================================================

TEST(test_create_system_wires_callback) {
    reset_mocks();
    bridge_state_t *b = bridge_create();

    pid_t pids[] = {100, 200};
    audiotap_t *tap = bridge_create_system(b, pids, 2, 48000.0f, 2, 1);

    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(mock_system_create_call_count, 1);
    ASSERT_EQ(last_system_config.pid_count, 2);
    ASSERT(last_system_config.pids[0] == 100);
    ASSERT(last_system_config.pids[1] == 200);
    ASSERT(last_system_config.sample_rate == 48000.0f);
    ASSERT_EQ(last_system_config.channels, 2);
    ASSERT_EQ(last_system_config.mute, 1);
    ASSERT(last_system_config.callback == bridge_callback);
    ASSERT(last_system_config.userdata == b);

    bridge_destroy(b);
}

TEST(test_create_system_null_pids) {
    reset_mocks();
    bridge_state_t *b = bridge_create();

    audiotap_t *tap = bridge_create_system(b, NULL, 0, 16000.0f, 1, 0);
    ASSERT_NOT_NULL(tap);
    ASSERT_NULL(last_system_config.pids);
    ASSERT_EQ(last_system_config.pid_count, 0);

    bridge_destroy(b);
}

TEST(test_create_system_returns_null_on_failure) {
    reset_mocks();
    mock_system_create_fail = 1;
    bridge_state_t *b = bridge_create();

    audiotap_t *tap = bridge_create_system(b, NULL, 0, 16000.0f, 1, 0);
    ASSERT_NULL(tap);

    bridge_destroy(b);
}

TEST(test_create_mic_wires_callback) {
    reset_mocks();
    bridge_state_t *b = bridge_create();

    audiotap_t *tap = bridge_create_mic(b, 16000.0f, 1);

    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(mock_mic_create_call_count, 1);
    ASSERT(last_mic_config.sample_rate == 16000.0f);
    ASSERT_EQ(last_mic_config.channels, 1);
    ASSERT(last_mic_config.callback == bridge_callback);
    ASSERT(last_mic_config.userdata == b);

    bridge_destroy(b);
}

TEST(test_create_mic_returns_null_on_failure) {
    reset_mocks();
    mock_mic_create_fail = 1;
    bridge_state_t *b = bridge_create();

    audiotap_t *tap = bridge_create_mic(b, 16000.0f, 1);
    ASSERT_NULL(tap);

    bridge_destroy(b);
}

// ============================================================================
// Tests: concurrent callback + read (pthread)
// ============================================================================

struct writer_args {
    bridge_state_t *bridge;
    int total_samples;
};

static void *writer_thread(void *arg) {
    struct writer_args *a = (struct writer_args *)arg;
    float buf[256];
    int written = 0;
    while (written < a->total_samples) {
        int chunk = a->total_samples - written;
        if (chunk > 256) chunk = 256;
        for (int i = 0; i < chunk; i++) {
            buf[i] = (float)(written + i);
        }
        bridge_callback(buf, (uint32_t)chunk, 1, 0, a->bridge);
        written += chunk;
    }
    return NULL;
}

TEST(test_concurrent_write_read) {
    bridge_state_t *b = bridge_create();

    const int total = 80000; // > RING_SIZE to test steady-state
    struct writer_args args = {.bridge = b, .total_samples = total};

    pthread_t tid;
    pthread_create(&tid, NULL, writer_thread, &args);

    float buf[512];
    int total_read = 0;
    float last_val = -1.0f;
    while (total_read < total) {
        int n = bridge_read(b, buf, 512);
        ASSERT(n > 0);
        // Values should be monotonically increasing (though some may be
        // dropped due to ring overflow under contention).
        for (int i = 0; i < n; i++) {
            ASSERT(buf[i] > last_val || last_val < 0.0f);
            last_val = buf[i];
        }
        total_read += n;
    }

    pthread_join(tid, NULL);
    bridge_destroy(b);
}

// ============================================================================
// Tests: read blocks until data or close
// ============================================================================

static void *close_after_delay(void *arg) {
    bridge_state_t *b = (bridge_state_t *)arg;
    usleep(50000); // 50 ms
    bridge_close(b);
    return NULL;
}

TEST(test_read_blocks_then_unblocked_by_close) {
    bridge_state_t *b = bridge_create();

    pthread_t tid;
    pthread_create(&tid, NULL, close_after_delay, b);

    float out[10];
    int n = bridge_read(b, out, 10);
    ASSERT_EQ(n, 0); // must return 0 after close

    pthread_join(tid, NULL);
    bridge_destroy(b);
}

static void *write_after_delay(void *arg) {
    bridge_state_t *b = (bridge_state_t *)arg;
    usleep(50000); // 50 ms
    float samples[] = {99.0f};
    bridge_callback(samples, 1, 1, 0, b);
    return NULL;
}

TEST(test_read_blocks_then_unblocked_by_write) {
    bridge_state_t *b = bridge_create();

    pthread_t tid;
    pthread_create(&tid, NULL, write_after_delay, b);

    float out[10];
    int n = bridge_read(b, out, 10);
    ASSERT_EQ(n, 1);
    ASSERT_FLOAT_EQ(out[0], 99.0f);

    pthread_join(tid, NULL);
    bridge_destroy(b);
}

// ============================================================================
// Tests: error paths
// ============================================================================

TEST(test_read_pipe_error) {
    bridge_state_t *b = bridge_create();

    // Sabotage the read end of the pipe so read() fails with EBADF.
    close(b->wait_fd);
    b->wait_fd = -1;

    float out[10];
    int n = bridge_read(b, out, 10);
    ASSERT_EQ(n, -1);

    bridge_destroy(b);
}

// ============================================================================
// main
// ============================================================================

int main(void) {
    printf("Bridge tests:\n");

    // create / destroy
    RUN_TEST(test_create_returns_non_null);
    RUN_TEST(test_create_initialises_positions);
    RUN_TEST(test_destroy_null_is_safe);
    RUN_TEST(test_close_null_is_safe);

    // callback
    RUN_TEST(test_callback_null_state);
    RUN_TEST(test_callback_after_close);
    RUN_TEST(test_callback_basic_write);
    RUN_TEST(test_callback_stereo);
    RUN_TEST(test_callback_wraparound);
    RUN_TEST(test_callback_overflow_drops_oldest);

    // read
    RUN_TEST(test_read_null_returns_error);
    RUN_TEST(test_read_basic);
    RUN_TEST(test_read_partial);
    RUN_TEST(test_read_multiple_writes);
    RUN_TEST(test_read_wraparound);
    RUN_TEST(test_read_after_close_with_data);
    RUN_TEST(test_read_after_close_empty);

    // write_samples (public API)
    RUN_TEST(test_write_samples);

    // close
    RUN_TEST(test_close_sets_flag);
    RUN_TEST(test_double_close_is_safe);

    // create_system / create_mic
    RUN_TEST(test_create_system_wires_callback);
    RUN_TEST(test_create_system_null_pids);
    RUN_TEST(test_create_system_returns_null_on_failure);
    RUN_TEST(test_create_mic_wires_callback);
    RUN_TEST(test_create_mic_returns_null_on_failure);

    // error paths
    RUN_TEST(test_read_pipe_error);

    // concurrency
    RUN_TEST(test_concurrent_write_read);
    RUN_TEST(test_read_blocks_then_unblocked_by_close);
    RUN_TEST(test_read_blocks_then_unblocked_by_write);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
