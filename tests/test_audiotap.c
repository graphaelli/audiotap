// Test suite for audiotap with 100% code coverage.
// All Core Audio calls are mocked to test every code path
// without requiring actual audio hardware.
//
// Strategy: Include Core Audio headers for types, define our own
// implementations of the Core Audio functions (mocks), then #include
// the source files directly. We don't link the actual frameworks.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <Block.h>

// Include Core Audio headers for type definitions only.
// Our mock implementations below will satisfy the extern declarations.
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include "audiotap.h"
#include "../src/audiotap_internal.h"

// ============================================================================
// Test framework
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

#define ASSERT_STR(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf(" FAIL\n    expected \"%s\" == \"%s\"\n    at %s:%d\n", (a), (b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_NULL(p) ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)

// ============================================================================
// Mock infrastructure for Core Audio
// ============================================================================

static int mock_get_default_input_fail = 0;
static AudioObjectID mock_default_input_device = 42;
static int mock_create_io_proc_fail = 0;
static int mock_device_start_fail = 0;
static int mock_create_io_proc_call_count = 0;
static int mock_device_start_call_count = 0;
static int mock_device_stop_call_count = 0;
static int mock_destroy_io_proc_call_count = 0;
static int mock_add_listener_call_count = 0;
static int mock_remove_listener_call_count = 0;

static AudioDeviceIOProc stored_io_proc = NULL;
static void *stored_io_proc_client_data = NULL;
static AudioObjectPropertyListenerBlock stored_listener_block = NULL;

static int fake_io_proc_id_storage = 1;

static void reset_mocks(void)
{
    mock_get_default_input_fail = 0;
    mock_default_input_device = 42;
    mock_create_io_proc_fail = 0;
    mock_device_start_fail = 0;
    mock_create_io_proc_call_count = 0;
    mock_device_start_call_count = 0;
    mock_device_stop_call_count = 0;
    mock_destroy_io_proc_call_count = 0;
    mock_add_listener_call_count = 0;
    mock_remove_listener_call_count = 0;
    stored_io_proc = NULL;
    stored_io_proc_client_data = NULL;
    if (stored_listener_block) {
        Block_release(stored_listener_block);
    }
    stored_listener_block = NULL;
    fake_io_proc_id_storage = 1;
}

// ============================================================================
// Mock Core Audio function implementations
// ============================================================================

OSStatus AudioObjectGetPropertyData(AudioObjectID inObjectID,
                                     const AudioObjectPropertyAddress *inAddress,
                                     UInt32 inQualifierDataSize,
                                     const void *inQualifierData,
                                     UInt32 *ioDataSize,
                                     void *outData)
{
    (void)inObjectID;
    (void)inQualifierDataSize;
    (void)inQualifierData;

    if (inAddress->mSelector == kAudioHardwarePropertyDefaultInputDevice) {
        if (mock_get_default_input_fail)
            return kAudioHardwareUnspecifiedError;
        if (*ioDataSize >= sizeof(AudioObjectID)) {
            *(AudioObjectID *)outData = mock_default_input_device;
            return noErr;
        }
        return kAudioHardwareBadPropertySizeError;
    }

    return kAudioHardwareUnknownPropertyError;
}

OSStatus AudioObjectGetPropertyDataSize(AudioObjectID inObjectID,
                                         const AudioObjectPropertyAddress *inAddress,
                                         UInt32 inQualifierDataSize,
                                         const void *inQualifierData,
                                         UInt32 *outDataSize)
{
    (void)inObjectID;
    (void)inAddress;
    (void)inQualifierDataSize;
    (void)inQualifierData;
    (void)outDataSize;
    return kAudioHardwareUnspecifiedError;
}

OSStatus AudioDeviceCreateIOProcID(AudioObjectID inDevice,
                                    AudioDeviceIOProc inProc,
                                    void *inClientData,
                                    AudioDeviceIOProcID *outIOProcID)
{
    (void)inDevice;
    mock_create_io_proc_call_count++;
    if (mock_create_io_proc_fail)
        return kAudioHardwareUnspecifiedError;

    stored_io_proc = inProc;
    stored_io_proc_client_data = inClientData;
    *outIOProcID = (AudioDeviceIOProcID)&fake_io_proc_id_storage;
    return noErr;
}

OSStatus AudioDeviceStart(AudioObjectID inDevice, AudioDeviceIOProcID inProcID)
{
    (void)inDevice;
    (void)inProcID;
    mock_device_start_call_count++;
    if (mock_device_start_fail)
        return kAudioHardwareNotRunningError;
    return noErr;
}

OSStatus AudioDeviceStop(AudioObjectID inDevice, AudioDeviceIOProcID inProcID)
{
    (void)inDevice;
    (void)inProcID;
    mock_device_stop_call_count++;
    return noErr;
}

OSStatus AudioDeviceDestroyIOProcID(AudioObjectID inDevice, AudioDeviceIOProcID inProcID)
{
    (void)inDevice;
    (void)inProcID;
    mock_destroy_io_proc_call_count++;
    return noErr;
}

OSStatus AudioObjectAddPropertyListenerBlock(AudioObjectID inObjectID,
                                              const AudioObjectPropertyAddress *inAddress,
                                              dispatch_queue_t inDispatchQueue,
                                              AudioObjectPropertyListenerBlock inListener)
{
    (void)inObjectID;
    (void)inAddress;
    (void)inDispatchQueue;
    mock_add_listener_call_count++;
    stored_listener_block = Block_copy(inListener);
    return noErr;
}

OSStatus AudioObjectRemovePropertyListenerBlock(AudioObjectID inObjectID,
                                                 const AudioObjectPropertyAddress *inAddress,
                                                 dispatch_queue_t inDispatchQueue,
                                                 AudioObjectPropertyListenerBlock inListener)
{
    (void)inObjectID;
    (void)inAddress;
    (void)inDispatchQueue;
    (void)inListener;
    mock_remove_listener_call_count++;
    // Don't release stored_listener_block here — the caller (audiotap_mic_destroy)
    // owns its own copy and will Block_release it separately.
    stored_listener_block = NULL;
    return noErr;
}

// ============================================================================
// Include the C source files directly (headers already included above,
// so include guards prevent re-inclusion; our mock functions above
// will be used instead of the real Core Audio framework functions).
// ============================================================================

// Mock calloc for testing allocation failures
static int mock_calloc_should_fail = 0;

// Override calloc - intercepts calls from included source files
#define calloc mock_calloc
static void *mock_calloc(size_t count, size_t size)
{
    if (mock_calloc_should_fail)
        return NULL;
    // Use malloc+memset to avoid recursion
    void *p = malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}

// We need to prevent audiotap_mic.c and audiotap_common.c from
// re-including the headers (already done via include guards).
#include "../src/audiotap_mic.c"
#include "../src/audiotap_common.c"

#undef calloc

// ============================================================================
// Stub system functions (audiotap_system.m is ObjC, can't include here)
// ============================================================================

static int system_start_call_count = 0;
static int system_stop_call_count = 0;
static int system_destroy_call_count = 0;
static int system_start_return = 0;

int audiotap_system_start(audiotap_t *tap)
{
    system_start_call_count++;
    tap->running = 1;
    audiotap_active_system_tap = tap;
    return system_start_return;
}

void audiotap_system_stop(audiotap_t *tap)
{
    system_stop_call_count++;
    tap->running = 0;
    if (audiotap_active_system_tap == tap)
        audiotap_active_system_tap = NULL;
}

void audiotap_system_destroy(audiotap_t *tap)
{
    (void)tap;
    system_destroy_call_count++;
}

static void reset_system_stubs(void)
{
    system_start_call_count = 0;
    system_stop_call_count = 0;
    system_destroy_call_count = 0;
    system_start_return = 0;
    audiotap_active_system_tap = NULL;
}

// ============================================================================
// Test helpers
// ============================================================================

static int callback_call_count = 0;
static uint32_t last_callback_frame_count = 0;
static uint32_t last_callback_channels = 0;
static uint64_t last_callback_host_time = 0;
static float last_callback_samples[1024];
static uint32_t last_callback_sample_count = 0;

static void test_callback(const float *samples, uint32_t frame_count,
                           uint32_t channels, uint64_t host_time, void *userdata)
{
    (void)userdata;
    callback_call_count++;
    last_callback_frame_count = frame_count;
    last_callback_channels = channels;
    last_callback_host_time = host_time;
    last_callback_sample_count = frame_count * channels;
    if (last_callback_sample_count > 1024)
        last_callback_sample_count = 1024;
    memcpy(last_callback_samples, samples, last_callback_sample_count * sizeof(float));
}

static void reset_callback_tracking(void)
{
    callback_call_count = 0;
    last_callback_frame_count = 0;
    last_callback_channels = 0;
    last_callback_host_time = 0;
    last_callback_sample_count = 0;
    memset(last_callback_samples, 0, sizeof(last_callback_samples));
}

// ============================================================================
// Tests: audiotap_error_string
// ============================================================================

TEST(test_error_string_success)
{
    ASSERT_STR(audiotap_error_string(0), "success");
}

TEST(test_error_string_not_running)
{
    ASSERT_STR(audiotap_error_string(kAudioHardwareNotRunningError), "hardware not running");
}

TEST(test_error_string_unspecified)
{
    ASSERT_STR(audiotap_error_string(kAudioHardwareUnspecifiedError), "unspecified hardware error");
}

TEST(test_error_string_unknown_property)
{
    ASSERT_STR(audiotap_error_string(kAudioHardwareUnknownPropertyError), "unknown property");
}

TEST(test_error_string_bad_property_size)
{
    ASSERT_STR(audiotap_error_string(kAudioHardwareBadPropertySizeError), "bad property size");
}

TEST(test_error_string_illegal_operation)
{
    ASSERT_STR(audiotap_error_string(kAudioHardwareIllegalOperationError), "illegal operation");
}

TEST(test_error_string_bad_object)
{
    ASSERT_STR(audiotap_error_string(kAudioHardwareBadObjectError), "bad audio object");
}

TEST(test_error_string_bad_device)
{
    ASSERT_STR(audiotap_error_string(kAudioHardwareBadDeviceError), "bad device");
}

TEST(test_error_string_bad_stream)
{
    ASSERT_STR(audiotap_error_string(kAudioHardwareBadStreamError), "bad stream");
}

TEST(test_error_string_unsupported_operation)
{
    ASSERT_STR(audiotap_error_string(kAudioHardwareUnsupportedOperationError), "unsupported operation");
}

TEST(test_error_string_unsupported_format)
{
    ASSERT_STR(audiotap_error_string(kAudioDeviceUnsupportedFormatError), "unsupported format");
}

TEST(test_error_string_permission_denied)
{
    ASSERT_STR(audiotap_error_string(kAudioDevicePermissionsError), "permission denied");
}

TEST(test_error_string_unknown)
{
    ASSERT_STR(audiotap_error_string(99999), "unknown error");
}

// ============================================================================
// Tests: audiotap_is_running
// ============================================================================

TEST(test_is_running_null)
{
    ASSERT_EQ(audiotap_is_running(NULL), 0);
}

TEST(test_is_running_not_running)
{
    audiotap_t tap = {0};
    tap.running = 0;
    ASSERT_EQ(audiotap_is_running(&tap), 0);
}

TEST(test_is_running_running)
{
    audiotap_t tap = {0};
    tap.running = 1;
    ASSERT_EQ(audiotap_is_running(&tap), 1);
}

// ============================================================================
// Tests: audiotap_create_mic
// ============================================================================

TEST(test_create_mic_null_config)
{
    ASSERT_NULL(audiotap_create_mic(NULL));
}

TEST(test_create_mic_null_callback)
{
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = NULL,
        .userdata = NULL,
    };
    ASSERT_NULL(audiotap_create_mic(&config));
}

TEST(test_create_mic_invalid_channels_zero)
{
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 0,
        .callback = test_callback,
        .userdata = NULL,
    };
    ASSERT_NULL(audiotap_create_mic(&config));
}

TEST(test_create_mic_invalid_channels_three)
{
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 3,
        .callback = test_callback,
        .userdata = NULL,
    };
    ASSERT_NULL(audiotap_create_mic(&config));
}

TEST(test_create_mic_no_device)
{
    reset_mocks();
    mock_get_default_input_fail = 1;

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
        .userdata = NULL,
    };
    ASSERT_NULL(audiotap_create_mic(&config));
}

TEST(test_create_mic_success_stereo)
{
    reset_mocks();
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
        .userdata = (void *)0xDEAD,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(tap->type, AUDIOTAP_TYPE_MIC);
    ASSERT_EQ(tap->channels, 2);
    ASSERT(tap->sample_rate == 48000.0f);
    ASSERT(tap->callback == test_callback);
    ASSERT(tap->userdata == (void *)0xDEAD);
    ASSERT_EQ(tap->running, 0);
    ASSERT_EQ((int)tap->input_device_id, (int)mock_default_input_device);
    ASSERT_EQ(mock_add_listener_call_count, 1);
    audiotap_destroy(tap);
}

TEST(test_create_mic_success_mono)
{
    reset_mocks();
    audiotap_mic_config_t config = {
        .sample_rate = 44100.0f,
        .channels = 1,
        .callback = test_callback,
        .userdata = NULL,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(tap->channels, 1);
    ASSERT(tap->sample_rate == 44100.0f);
    audiotap_destroy(tap);
}

// ============================================================================
// Tests: audiotap_start / audiotap_stop (mic)
// ============================================================================

TEST(test_start_null)
{
    ASSERT_EQ(audiotap_start(NULL), -1);
}

TEST(test_start_mic_success)
{
    reset_mocks();
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);

    int err = audiotap_start(tap);
    ASSERT_EQ(err, 0);
    ASSERT_EQ(tap->running, 1);
    ASSERT_EQ(mock_create_io_proc_call_count, 1);
    ASSERT_EQ(mock_device_start_call_count, 1);

    audiotap_destroy(tap);
}

TEST(test_start_mic_already_running)
{
    reset_mocks();
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);

    ASSERT_EQ(audiotap_start(tap), 0);
    ASSERT_EQ(audiotap_start(tap), 0);
    ASSERT_EQ(mock_create_io_proc_call_count, 1);

    audiotap_destroy(tap);
}

TEST(test_start_mic_create_io_proc_fails)
{
    reset_mocks();
    mock_create_io_proc_fail = 1;

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);

    int err = audiotap_start(tap);
    ASSERT(err != 0);
    ASSERT_EQ(tap->running, 0);

    audiotap_destroy(tap);
}

TEST(test_start_mic_device_start_fails)
{
    reset_mocks();
    mock_device_start_fail = 1;

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);

    int err = audiotap_start(tap);
    ASSERT(err != 0);
    ASSERT_EQ(tap->running, 0);
    ASSERT_EQ(mock_destroy_io_proc_call_count, 1);
    ASSERT(tap->io_proc_id == NULL);

    audiotap_destroy(tap);
}

TEST(test_start_mic_pauses_system_tap)
{
    reset_mocks();
    reset_system_stubs();

    // Create a fake "running" system tap to simulate an active aggregate device
    audiotap_t fake_sys = {0};
    fake_sys.type = AUDIOTAP_TYPE_SYSTEM;
    fake_sys.running = 1;
    audiotap_active_system_tap = &fake_sys;

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);

    int err = audiotap_start(tap);
    ASSERT_EQ(err, 0);
    ASSERT_EQ(tap->running, 1);
    // System tap should have been stopped then restarted
    ASSERT_EQ(system_stop_call_count, 1);
    ASSERT_EQ(system_start_call_count, 1);
    // System tap should be running again
    ASSERT_EQ(fake_sys.running, 1);
    ASSERT(audiotap_active_system_tap == &fake_sys);

    audiotap_active_system_tap = NULL;
    audiotap_destroy(tap);
}

TEST(test_start_mic_no_pause_when_no_system_tap)
{
    reset_mocks();
    reset_system_stubs();
    audiotap_active_system_tap = NULL;

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);

    int err = audiotap_start(tap);
    ASSERT_EQ(err, 0);
    // No system tap pause/restart should have happened
    ASSERT_EQ(system_stop_call_count, 0);
    ASSERT_EQ(system_start_call_count, 0);

    audiotap_destroy(tap);
}

TEST(test_stop_null)
{
    audiotap_stop(NULL);
}

TEST(test_stop_not_running)
{
    reset_mocks();
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);

    audiotap_stop(tap);
    ASSERT_EQ(mock_device_stop_call_count, 0);

    audiotap_destroy(tap);
}

TEST(test_stop_mic)
{
    reset_mocks();
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);

    ASSERT_EQ(audiotap_start(tap), 0);
    ASSERT_EQ(tap->running, 1);

    audiotap_stop(tap);
    ASSERT_EQ(tap->running, 0);
    ASSERT_EQ(mock_device_stop_call_count, 1);
    ASSERT_EQ(mock_destroy_io_proc_call_count, 1);
    ASSERT(tap->io_proc_id == NULL);

    audiotap_destroy(tap);
}

// ============================================================================
// Tests: audiotap_destroy
// ============================================================================

TEST(test_destroy_null)
{
    audiotap_destroy(NULL);
}

TEST(test_destroy_running_mic)
{
    reset_mocks();
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_start(tap), 0);

    audiotap_destroy(tap);
    ASSERT_EQ(mock_device_stop_call_count, 1);
    ASSERT_EQ(mock_remove_listener_call_count, 1);
}

TEST(test_destroy_stopped_mic)
{
    reset_mocks();
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);

    audiotap_destroy(tap);
    ASSERT_EQ(mock_device_stop_call_count, 0);
    ASSERT_EQ(mock_remove_listener_call_count, 1);
}

// ============================================================================
// Tests: IO proc callback (mic)
// ============================================================================

TEST(test_mic_io_proc_normal)
{
    reset_mocks();
    reset_callback_tracking();

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_start(tap), 0);

    float samples[256];
    memset(samples, 0, sizeof(samples));
    AudioBuffer buf = {
        .mNumberChannels = 2,
        .mDataByteSize = sizeof(samples),
        .mData = samples,
    };
    AudioBufferList bufList = {
        .mNumberBuffers = 1,
        .mBuffers = {buf},
    };
    AudioTimeStamp inputTime = {0};
    inputTime.mHostTime = 12345;

    ASSERT_NOT_NULL(stored_io_proc);
    OSStatus result = stored_io_proc(42, NULL, &bufList, &inputTime, NULL, NULL, stored_io_proc_client_data);
    ASSERT_EQ(result, noErr);
    ASSERT_EQ(callback_call_count, 1);
    ASSERT_EQ(last_callback_frame_count, 256 / 2);
    ASSERT_EQ(last_callback_channels, 2);
    ASSERT_EQ((int)last_callback_host_time, 12345);

    audiotap_destroy(tap);
}

TEST(test_mic_io_proc_null_tap)
{
    reset_mocks();
    reset_callback_tracking();

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_start(tap), 0);

    ASSERT_NOT_NULL(stored_io_proc);
    OSStatus result = stored_io_proc(42, NULL, NULL, NULL, NULL, NULL, NULL);
    ASSERT_EQ(result, noErr);
    ASSERT_EQ(callback_call_count, 0);

    audiotap_destroy(tap);
}

TEST(test_mic_io_proc_null_input_data)
{
    reset_mocks();
    reset_callback_tracking();

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_start(tap), 0);

    ASSERT_NOT_NULL(stored_io_proc);
    OSStatus result = stored_io_proc(42, NULL, NULL, NULL, NULL, NULL, stored_io_proc_client_data);
    ASSERT_EQ(result, noErr);
    ASSERT_EQ(callback_call_count, 0);

    audiotap_destroy(tap);
}

TEST(test_mic_io_proc_empty_buffer)
{
    reset_mocks();
    reset_callback_tracking();

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_start(tap), 0);

    AudioBuffer buf = {
        .mNumberChannels = 2,
        .mDataByteSize = 0,
        .mData = NULL,
    };
    AudioBufferList bufList = {
        .mNumberBuffers = 1,
        .mBuffers = {buf},
    };

    ASSERT_NOT_NULL(stored_io_proc);
    OSStatus result = stored_io_proc(42, NULL, &bufList, NULL, NULL, NULL, stored_io_proc_client_data);
    ASSERT_EQ(result, noErr);
    ASSERT_EQ(callback_call_count, 0);

    audiotap_destroy(tap);
}

TEST(test_mic_io_proc_null_timestamp)
{
    reset_mocks();
    reset_callback_tracking();

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 1,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_start(tap), 0);

    float samples[64];
    memset(samples, 0, sizeof(samples));
    AudioBuffer buf = {
        .mNumberChannels = 1,
        .mDataByteSize = sizeof(samples),
        .mData = samples,
    };
    AudioBufferList bufList = {
        .mNumberBuffers = 1,
        .mBuffers = {buf},
    };

    ASSERT_NOT_NULL(stored_io_proc);
    OSStatus result = stored_io_proc(42, NULL, &bufList, NULL, NULL, NULL, stored_io_proc_client_data);
    ASSERT_EQ(result, noErr);
    ASSERT_EQ(callback_call_count, 1);
    ASSERT_EQ((int)last_callback_host_time, 0);
    ASSERT_EQ(last_callback_channels, 1);
    ASSERT_EQ(last_callback_frame_count, 64);

    audiotap_destroy(tap);
}

TEST(test_mic_io_proc_upmix_mono_to_stereo)
{
    reset_mocks();
    reset_callback_tracking();

    // Tap configured for stereo, but device delivers mono
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_start(tap), 0);

    float mono_samples[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    AudioBuffer buf = {
        .mNumberChannels = 1,
        .mDataByteSize = sizeof(mono_samples),
        .mData = mono_samples,
    };
    AudioBufferList bufList = {
        .mNumberBuffers = 1,
        .mBuffers = {buf},
    };
    AudioTimeStamp inputTime = {0};

    stored_io_proc(42, NULL, &bufList, &inputTime, NULL, NULL, stored_io_proc_client_data);
    ASSERT_EQ(callback_call_count, 1);
    ASSERT_EQ(last_callback_channels, 2);
    ASSERT_EQ(last_callback_frame_count, 4);
    // Each mono sample should be duplicated to L and R
    ASSERT(last_callback_samples[0] == 0.1f); // L
    ASSERT(last_callback_samples[1] == 0.1f); // R
    ASSERT(last_callback_samples[2] == 0.2f); // L
    ASSERT(last_callback_samples[3] == 0.2f); // R
    ASSERT(last_callback_samples[4] == 0.3f); // L
    ASSERT(last_callback_samples[5] == 0.3f); // R
    ASSERT(last_callback_samples[6] == 0.4f); // L
    ASSERT(last_callback_samples[7] == 0.4f); // R

    audiotap_destroy(tap);
}

TEST(test_mic_io_proc_downmix_stereo_to_mono)
{
    reset_mocks();
    reset_callback_tracking();

    // Tap configured for mono, but device delivers stereo
    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 1,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_start(tap), 0);

    // 3 stereo frames: L=0.2 R=0.4, L=0.6 R=0.8, L=1.0 R=0.0
    float stereo_samples[6] = {0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 0.0f};
    AudioBuffer buf = {
        .mNumberChannels = 2,
        .mDataByteSize = sizeof(stereo_samples),
        .mData = stereo_samples,
    };
    AudioBufferList bufList = {
        .mNumberBuffers = 1,
        .mBuffers = {buf},
    };
    AudioTimeStamp inputTime = {0};

    stored_io_proc(42, NULL, &bufList, &inputTime, NULL, NULL, stored_io_proc_client_data);
    ASSERT_EQ(callback_call_count, 1);
    ASSERT_EQ(last_callback_channels, 1);
    ASSERT_EQ(last_callback_frame_count, 3);
    // Each pair averaged: (0.2+0.4)/2=0.3, (0.6+0.8)/2=0.7, (1.0+0.0)/2=0.5
    float eps = 1e-6f;
    ASSERT(last_callback_samples[0] > 0.3f - eps && last_callback_samples[0] < 0.3f + eps);
    ASSERT(last_callback_samples[1] > 0.7f - eps && last_callback_samples[1] < 0.7f + eps);
    ASSERT(last_callback_samples[2] > 0.5f - eps && last_callback_samples[2] < 0.5f + eps);

    audiotap_destroy(tap);
}

// ============================================================================
// Tests: device change handling
// ============================================================================

TEST(test_device_change_while_running)
{
    reset_mocks();

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_start(tap), 0);
    ASSERT_EQ((int)tap->input_device_id, 42);

    mock_default_input_device = 99;
    ASSERT_NOT_NULL(stored_listener_block);

    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };
    stored_listener_block(1, &addr);

    ASSERT_EQ((int)tap->input_device_id, 99);
    ASSERT_EQ(tap->running, 1);

    audiotap_destroy(tap);
}

TEST(test_device_change_while_stopped)
{
    reset_mocks();

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);

    mock_default_input_device = 99;
    ASSERT_NOT_NULL(stored_listener_block);

    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };
    stored_listener_block(1, &addr);

    ASSERT_EQ((int)tap->input_device_id, 99);
    ASSERT_EQ(tap->running, 0);

    audiotap_destroy(tap);
}

TEST(test_device_change_same_device)
{
    reset_mocks();

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_start(tap), 0);

    int start_count_before = mock_device_start_call_count;

    ASSERT_NOT_NULL(stored_listener_block);
    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };
    stored_listener_block(1, &addr);

    ASSERT_EQ(mock_device_start_call_count, start_count_before);

    audiotap_destroy(tap);
}

TEST(test_device_change_unknown_device)
{
    reset_mocks();

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_start(tap), 0);

    mock_get_default_input_fail = 1;

    ASSERT_NOT_NULL(stored_listener_block);
    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };
    stored_listener_block(1, &addr);

    ASSERT_EQ((int)tap->input_device_id, 42);
    ASSERT_EQ(tap->running, 1);

    audiotap_destroy(tap);
}

// ============================================================================
// Tests: system type dispatch (via audiotap_common.c)
// ============================================================================

TEST(test_start_system_dispatch)
{
    reset_mocks();
    reset_system_stubs();

    audiotap_t tap = {0};
    tap.type = AUDIOTAP_TYPE_SYSTEM;
    tap.callback = test_callback;

    int err = audiotap_start(&tap);
    ASSERT_EQ(err, 0);
    ASSERT_EQ(system_start_call_count, 1);
}

TEST(test_start_system_dispatch_failure)
{
    reset_mocks();
    reset_system_stubs();
    system_start_return = -1;

    audiotap_t tap = {0};
    tap.type = AUDIOTAP_TYPE_SYSTEM;
    tap.callback = test_callback;

    int err = audiotap_start(&tap);
    ASSERT_EQ(err, -1);
    ASSERT_EQ(system_start_call_count, 1);
}

TEST(test_stop_system_dispatch)
{
    reset_mocks();
    reset_system_stubs();

    audiotap_t tap = {0};
    tap.type = AUDIOTAP_TYPE_SYSTEM;
    tap.running = 1;

    audiotap_stop(&tap);
    ASSERT_EQ(system_stop_call_count, 1);
    ASSERT_EQ(tap.running, 0);
}

TEST(test_destroy_system_dispatch)
{
    reset_mocks();
    reset_system_stubs();

    audiotap_t *tap = (audiotap_t *)calloc(1, sizeof(audiotap_t));
    tap->type = AUDIOTAP_TYPE_SYSTEM;
    tap->running = 1;

    audiotap_destroy(tap);
    ASSERT_EQ(system_stop_call_count, 1);
    ASSERT_EQ(system_destroy_call_count, 1);
}

TEST(test_destroy_system_not_running)
{
    reset_mocks();
    reset_system_stubs();

    audiotap_t *tap = (audiotap_t *)calloc(1, sizeof(audiotap_t));
    tap->type = AUDIOTAP_TYPE_SYSTEM;
    tap->running = 0;

    audiotap_destroy(tap);
    ASSERT_EQ(system_stop_call_count, 0);
    ASSERT_EQ(system_destroy_call_count, 1);
}

// ============================================================================
// Tests: edge cases
// ============================================================================

TEST(test_mic_stop_null_io_proc)
{
    reset_mocks();

    audiotap_t tap = {0};
    tap.type = AUDIOTAP_TYPE_MIC;
    tap.running = 1;
    tap.io_proc_id = NULL;

    audiotap_mic_stop(&tap);
    ASSERT_EQ(tap.running, 0);
    ASSERT_EQ(mock_device_stop_call_count, 0);
}

TEST(test_mic_destroy_null_listener)
{
    reset_mocks();

    audiotap_t tap = {0};
    tap.type = AUDIOTAP_TYPE_MIC;
    tap.device_change_listener = NULL;

    audiotap_mic_destroy(&tap);
    ASSERT_EQ(mock_remove_listener_call_count, 0);
}

TEST(test_start_invalid_type)
{
    audiotap_t tap = {0};
    tap.type = (audiotap_type_t)99;
    tap.running = 0;

    int err = audiotap_start(&tap);
    ASSERT_EQ(err, -1);
}

TEST(test_create_mic_calloc_fails)
{
    reset_mocks();
    mock_calloc_should_fail = 1;

    audiotap_mic_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_mic(&config);
    ASSERT_NULL(tap);

    mock_calloc_should_fail = 0;
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    printf("audiotap test suite\n");
    printf("===================\n\n");

    printf("error_string:\n");
    RUN_TEST(test_error_string_success);
    RUN_TEST(test_error_string_not_running);
    RUN_TEST(test_error_string_unspecified);
    RUN_TEST(test_error_string_unknown_property);
    RUN_TEST(test_error_string_bad_property_size);
    RUN_TEST(test_error_string_illegal_operation);
    RUN_TEST(test_error_string_bad_object);
    RUN_TEST(test_error_string_bad_device);
    RUN_TEST(test_error_string_bad_stream);
    RUN_TEST(test_error_string_unsupported_operation);
    RUN_TEST(test_error_string_unsupported_format);
    RUN_TEST(test_error_string_permission_denied);
    RUN_TEST(test_error_string_unknown);

    printf("\nis_running:\n");
    RUN_TEST(test_is_running_null);
    RUN_TEST(test_is_running_not_running);
    RUN_TEST(test_is_running_running);

    printf("\ncreate_mic:\n");
    RUN_TEST(test_create_mic_null_config);
    RUN_TEST(test_create_mic_null_callback);
    RUN_TEST(test_create_mic_invalid_channels_zero);
    RUN_TEST(test_create_mic_invalid_channels_three);
    RUN_TEST(test_create_mic_no_device);
    RUN_TEST(test_create_mic_success_stereo);
    RUN_TEST(test_create_mic_success_mono);

    printf("\nstart/stop:\n");
    RUN_TEST(test_start_null);
    RUN_TEST(test_start_mic_success);
    RUN_TEST(test_start_mic_already_running);
    RUN_TEST(test_start_mic_create_io_proc_fails);
    RUN_TEST(test_start_mic_device_start_fails);
    RUN_TEST(test_start_mic_pauses_system_tap);
    RUN_TEST(test_start_mic_no_pause_when_no_system_tap);
    RUN_TEST(test_stop_null);
    RUN_TEST(test_stop_not_running);
    RUN_TEST(test_stop_mic);

    printf("\ndestroy:\n");
    RUN_TEST(test_destroy_null);
    RUN_TEST(test_destroy_running_mic);
    RUN_TEST(test_destroy_stopped_mic);

    printf("\nio_proc callback:\n");
    RUN_TEST(test_mic_io_proc_normal);
    RUN_TEST(test_mic_io_proc_null_tap);
    RUN_TEST(test_mic_io_proc_null_input_data);
    RUN_TEST(test_mic_io_proc_empty_buffer);
    RUN_TEST(test_mic_io_proc_null_timestamp);
    RUN_TEST(test_mic_io_proc_upmix_mono_to_stereo);
    RUN_TEST(test_mic_io_proc_downmix_stereo_to_mono);

    printf("\ndevice change:\n");
    RUN_TEST(test_device_change_while_running);
    RUN_TEST(test_device_change_while_stopped);
    RUN_TEST(test_device_change_same_device);
    RUN_TEST(test_device_change_unknown_device);

    printf("\nsystem dispatch:\n");
    RUN_TEST(test_start_system_dispatch);
    RUN_TEST(test_start_system_dispatch_failure);
    RUN_TEST(test_stop_system_dispatch);
    RUN_TEST(test_destroy_system_dispatch);
    RUN_TEST(test_destroy_system_not_running);

    printf("\nedge cases:\n");
    RUN_TEST(test_mic_stop_null_io_proc);
    RUN_TEST(test_mic_destroy_null_listener);
    RUN_TEST(test_start_invalid_type);
    RUN_TEST(test_create_mic_calloc_fails);

    printf("\n===================\n");
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
