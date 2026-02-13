// Test suite for audiotap_system.m — tests the system audio tap creation
// and IO proc callback with mocked Core Audio + ObjC calls.

#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audiotap.h"
#include "../src/audiotap_internal.h"

// ============================================================================
// Test framework
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %-55s", #name); \
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

#define ASSERT_NULL(p) ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)

// ============================================================================
// Mock state
// ============================================================================

static int mock_create_tap_fail = 0;
static int mock_create_aggregate_fail = 0;
static int mock_create_io_proc_fail = 0;
static int mock_device_start_fail = 0;
static int mock_create_tap_call_count = 0;
static int mock_destroy_tap_call_count = 0;
static int mock_create_aggregate_call_count = 0;
static int mock_destroy_aggregate_call_count = 0;
static int mock_create_io_proc_call_count = 0;
static int mock_device_start_call_count = 0;
static int mock_device_stop_call_count = 0;
static int mock_destroy_io_proc_call_count = 0;
static int mock_calloc_should_fail = 0;
static int mock_resolve_pids_empty = 0;
static int mock_get_property_data_fail = 0;
static int mock_get_pid_fail = 0;
static AudioDeviceIOProc stored_io_proc = NULL;
static void *stored_io_proc_client_data = NULL;
static int fake_io_proc_id_storage = 1;

static void reset_mocks(void)
{
    mock_create_tap_fail = 0;
    mock_create_aggregate_fail = 0;
    mock_create_io_proc_fail = 0;
    mock_device_start_fail = 0;
    mock_create_tap_call_count = 0;
    mock_destroy_tap_call_count = 0;
    mock_create_aggregate_call_count = 0;
    mock_destroy_aggregate_call_count = 0;
    mock_create_io_proc_call_count = 0;
    mock_device_start_call_count = 0;
    mock_device_stop_call_count = 0;
    mock_destroy_io_proc_call_count = 0;
    mock_calloc_should_fail = 0;
    mock_resolve_pids_empty = 0;
    mock_get_property_data_fail = 0;
    mock_get_pid_fail = 0;
    stored_io_proc = NULL;
    stored_io_proc_client_data = NULL;
    fake_io_proc_id_storage = 1;
}

// ============================================================================
// Mock Core Audio functions
// ============================================================================

OSStatus AudioHardwareCreateProcessTap(CATapDescription *inDescription,
                                        AudioObjectID *outTapID)
{
    (void)inDescription;
    mock_create_tap_call_count++;
    if (mock_create_tap_fail)
        return kAudioHardwareUnspecifiedError;
    *outTapID = 100;
    return noErr;
}

OSStatus AudioHardwareDestroyProcessTap(AudioObjectID inTapID)
{
    (void)inTapID;
    mock_destroy_tap_call_count++;
    return noErr;
}

OSStatus AudioHardwareCreateAggregateDevice(CFDictionaryRef inDescription,
                                             AudioObjectID *outDeviceID)
{
    (void)inDescription;
    mock_create_aggregate_call_count++;
    if (mock_create_aggregate_fail)
        return kAudioHardwareUnspecifiedError;
    *outDeviceID = 200;
    return noErr;
}

OSStatus AudioHardwareDestroyAggregateDevice(AudioObjectID inDeviceID)
{
    (void)inDeviceID;
    mock_destroy_aggregate_call_count++;
    return noErr;
}

OSStatus AudioObjectGetPropertyDataSize(AudioObjectID inObjectID,
                                         const AudioObjectPropertyAddress *inAddress,
                                         UInt32 inQualifierDataSize,
                                         const void *inQualifierData,
                                         UInt32 *outDataSize)
{
    (void)inObjectID;
    (void)inQualifierDataSize;
    (void)inQualifierData;

    if (inAddress->mSelector == kAudioHardwarePropertyProcessObjectList) {
        if (mock_resolve_pids_empty) {
            *outDataSize = 0;
            return noErr;
        }
        // Return 2 process objects
        *outDataSize = 2 * sizeof(AudioObjectID);
        return noErr;
    }
    return kAudioHardwareUnspecifiedError;
}

OSStatus AudioObjectGetPropertyData(AudioObjectID inObjectID,
                                     const AudioObjectPropertyAddress *inAddress,
                                     UInt32 inQualifierDataSize,
                                     const void *inQualifierData,
                                     UInt32 *ioDataSize,
                                     void *outData)
{
    (void)inQualifierDataSize;
    (void)inQualifierData;

    if (inAddress->mSelector == kAudioHardwarePropertyProcessObjectList) {
        if (mock_get_property_data_fail)
            return kAudioHardwareUnspecifiedError;
        AudioObjectID *objects = (AudioObjectID *)outData;
        objects[0] = 301;
        objects[1] = 302;
        return noErr;
    }
    if (inAddress->mSelector == kAudioProcessPropertyPID) {
        if (mock_get_pid_fail)
            return kAudioHardwareUnspecifiedError;
        pid_t *pid = (pid_t *)outData;
        // Map object IDs to PIDs
        if (inObjectID == 301)
            *pid = 1001;
        else if (inObjectID == 302)
            *pid = 1002;
        else
            return kAudioHardwareUnspecifiedError;
        return noErr;
    }
    return kAudioHardwareUnknownPropertyError;
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

// ============================================================================
// Mock calloc
// ============================================================================

#define calloc mock_calloc
static void *mock_calloc(size_t count, size_t size)
{
    if (mock_calloc_should_fail)
        return NULL;
    void *p = malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}

// Global defined in audiotap_common.c (not included here)
audiotap_t *audiotap_active_system_tap = NULL;

// ============================================================================
// Include the ObjC source file
// ============================================================================

#include "../src/audiotap_system.m"

#undef calloc

// ============================================================================
// Test helpers
// ============================================================================

static int callback_call_count = 0;
static uint32_t last_callback_frame_count = 0;
static uint32_t last_callback_channels = 0;
static uint64_t last_callback_host_time = 0;

static void test_callback(const float *samples, uint32_t frame_count,
                           uint32_t channels, uint64_t host_time, void *userdata)
{
    (void)samples;
    (void)userdata;
    callback_call_count++;
    last_callback_frame_count = frame_count;
    last_callback_channels = channels;
    last_callback_host_time = host_time;
}

static void reset_callback_tracking(void)
{
    callback_call_count = 0;
    last_callback_frame_count = 0;
    last_callback_channels = 0;
    last_callback_host_time = 0;
}

// ============================================================================
// Tests: audiotap_create_system
// ============================================================================

TEST(test_create_system_null_config)
{
    ASSERT_NULL(audiotap_create_system(NULL));
}

TEST(test_create_system_null_callback)
{
    audiotap_system_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = NULL,
    };
    ASSERT_NULL(audiotap_create_system(&config));
}

TEST(test_create_system_invalid_channels)
{
    audiotap_system_config_t config = {
        .sample_rate = 48000.0f,
        .channels = 3,
        .callback = test_callback,
    };
    ASSERT_NULL(audiotap_create_system(&config));
}

TEST(test_create_system_global_stereo)
{
    reset_mocks();
    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .mute = 0,
        .callback = test_callback,
        .userdata = (void *)0xBEEF,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(tap->type, AUDIOTAP_TYPE_SYSTEM);
    ASSERT_EQ(tap->channels, 2);
    ASSERT(tap->callback == test_callback);
    ASSERT(tap->userdata == (void *)0xBEEF);
    ASSERT_EQ(tap->running, 0);
    ASSERT_EQ((int)tap->tap_id, 100);
    ASSERT_EQ((int)tap->aggregate_device_id, 200);
    ASSERT_EQ(mock_create_tap_call_count, 1);
    ASSERT_EQ(mock_create_aggregate_call_count, 1);

    audiotap_system_destroy(tap);
    free(tap);
}

TEST(test_create_system_global_mono)
{
    reset_mocks();
    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 44100.0f,
        .channels = 1,
        .mute = 1,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(tap->channels, 1);

    audiotap_system_destroy(tap);
    free(tap);
}

TEST(test_create_system_specific_pids_stereo)
{
    reset_mocks();
    pid_t pids[] = {1001};
    audiotap_system_config_t config = {
        .pids = pids,
        .pid_count = 1,
        .sample_rate = 48000.0f,
        .channels = 2,
        .mute = 0,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(mock_create_tap_call_count, 1);

    audiotap_system_destroy(tap);
    free(tap);
}

TEST(test_create_system_specific_pids_mono)
{
    reset_mocks();
    pid_t pids[] = {1002};
    audiotap_system_config_t config = {
        .pids = pids,
        .pid_count = 1,
        .sample_rate = 48000.0f,
        .channels = 1,
        .mute = 0,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);

    audiotap_system_destroy(tap);
    free(tap);
}

TEST(test_create_system_pids_not_found)
{
    reset_mocks();
    pid_t pids[] = {9999}; // PID not in mock process list
    audiotap_system_config_t config = {
        .pids = pids,
        .pid_count = 1,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NULL(tap); // no matching process objects
}

TEST(test_create_system_pids_no_process_list)
{
    reset_mocks();
    mock_resolve_pids_empty = 1;
    pid_t pids[] = {1001};
    audiotap_system_config_t config = {
        .pids = pids,
        .pid_count = 1,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NULL(tap);
}

TEST(test_create_system_pids_get_data_fails)
{
    reset_mocks();
    mock_get_property_data_fail = 1;
    pid_t pids[] = {1001};
    audiotap_system_config_t config = {
        .pids = pids,
        .pid_count = 1,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NULL(tap);
}

TEST(test_create_system_pids_get_pid_fails)
{
    reset_mocks();
    mock_get_pid_fail = 1;
    pid_t pids[] = {1001};
    audiotap_system_config_t config = {
        .pids = pids,
        .pid_count = 1,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    // PID resolution fails but returns empty array, so tap creation fails
    ASSERT_NULL(tap);
}

TEST(test_create_system_tap_fails)
{
    reset_mocks();
    mock_create_tap_fail = 1;
    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NULL(tap);
}

TEST(test_create_system_aggregate_fails)
{
    reset_mocks();
    mock_create_aggregate_fail = 1;
    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NULL(tap);
    // Should have cleaned up the tap
    ASSERT_EQ(mock_destroy_tap_call_count, 1);
}

TEST(test_create_system_calloc_fails)
{
    reset_mocks();
    mock_calloc_should_fail = 1;
    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NULL(tap);
    // Should have cleaned up both tap and aggregate
    ASSERT_EQ(mock_destroy_aggregate_call_count, 1);
    ASSERT_EQ(mock_destroy_tap_call_count, 1);
    mock_calloc_should_fail = 0;
}

// ============================================================================
// Tests: audiotap_system_start / stop / destroy
// ============================================================================

TEST(test_system_start_success)
{
    reset_mocks();
    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);

    int err = audiotap_system_start(tap);
    ASSERT_EQ(err, 0);
    ASSERT_EQ(tap->running, 1);
    ASSERT_EQ(mock_create_io_proc_call_count, 1);
    ASSERT_EQ(mock_device_start_call_count, 1);

    audiotap_system_stop(tap);
    audiotap_system_destroy(tap);
    free(tap);
}

TEST(test_system_start_io_proc_fails)
{
    reset_mocks();
    mock_create_io_proc_fail = 1;
    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);

    int err = audiotap_system_start(tap);
    ASSERT(err != 0);
    ASSERT_EQ(tap->running, 0);

    audiotap_system_destroy(tap);
    free(tap);
}

TEST(test_system_start_device_start_fails)
{
    reset_mocks();
    mock_device_start_fail = 1;
    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);

    int err = audiotap_system_start(tap);
    ASSERT(err != 0);
    ASSERT_EQ(tap->running, 0);
    ASSERT_EQ(mock_destroy_io_proc_call_count, 1);

    audiotap_system_destroy(tap);
    free(tap);
}

TEST(test_system_stop)
{
    reset_mocks();
    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_system_start(tap), 0);

    audiotap_system_stop(tap);
    ASSERT_EQ(tap->running, 0);
    ASSERT_EQ(mock_device_stop_call_count, 1);
    ASSERT_EQ(mock_destroy_io_proc_call_count, 1);
    ASSERT(tap->io_proc_id == NULL);

    audiotap_system_destroy(tap);
    free(tap);
}

TEST(test_system_stop_null_io_proc)
{
    reset_mocks();

    audiotap_t tap = {0};
    tap.type = AUDIOTAP_TYPE_SYSTEM;
    tap.io_proc_id = NULL;

    audiotap_system_stop(&tap);
    ASSERT_EQ(tap.running, 0);
    ASSERT_EQ(mock_device_stop_call_count, 0);
}

TEST(test_system_destroy)
{
    reset_mocks();
    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);

    audiotap_system_destroy(tap);
    ASSERT_EQ(mock_destroy_aggregate_call_count, 1);
    ASSERT_EQ(mock_destroy_tap_call_count, 1);
    ASSERT_EQ((int)tap->aggregate_device_id, (int)kAudioObjectUnknown);
    ASSERT_EQ((int)tap->tap_id, (int)kAudioObjectUnknown);
    free(tap);
}

TEST(test_system_destroy_unknown_ids)
{
    reset_mocks();

    audiotap_t tap = {0};
    tap.aggregate_device_id = kAudioObjectUnknown;
    tap.tap_id = kAudioObjectUnknown;

    audiotap_system_destroy(&tap);
    ASSERT_EQ(mock_destroy_aggregate_call_count, 0);
    ASSERT_EQ(mock_destroy_tap_call_count, 0);
}

// ============================================================================
// Tests: system IO proc callback
// ============================================================================

TEST(test_system_io_proc_normal)
{
    reset_mocks();
    reset_callback_tracking();

    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_system_start(tap), 0);

    float samples[128];
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
    AudioTimeStamp ts = {0};
    ts.mHostTime = 54321;

    ASSERT_NOT_NULL(stored_io_proc);
    OSStatus result = stored_io_proc(200, NULL, &bufList, &ts, NULL, NULL, stored_io_proc_client_data);
    ASSERT_EQ(result, noErr);
    ASSERT_EQ(callback_call_count, 1);
    ASSERT_EQ(last_callback_channels, 2);
    ASSERT_EQ(last_callback_frame_count, 128 / 2);
    ASSERT_EQ((int)last_callback_host_time, 54321);

    audiotap_system_stop(tap);
    audiotap_system_destroy(tap);
    free(tap);
}

TEST(test_system_io_proc_null_client_data)
{
    reset_mocks();
    reset_callback_tracking();

    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_system_start(tap), 0);

    ASSERT_NOT_NULL(stored_io_proc);
    OSStatus result = stored_io_proc(200, NULL, NULL, NULL, NULL, NULL, NULL);
    ASSERT_EQ(result, noErr);
    ASSERT_EQ(callback_call_count, 0);

    audiotap_system_stop(tap);
    audiotap_system_destroy(tap);
    free(tap);
}

TEST(test_system_io_proc_empty_buffer)
{
    reset_mocks();
    reset_callback_tracking();

    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 2,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_system_start(tap), 0);

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
    OSStatus result = stored_io_proc(200, NULL, &bufList, NULL, NULL, NULL, stored_io_proc_client_data);
    ASSERT_EQ(result, noErr);
    ASSERT_EQ(callback_call_count, 0);

    audiotap_system_stop(tap);
    audiotap_system_destroy(tap);
    free(tap);
}

TEST(test_system_io_proc_null_timestamp)
{
    reset_mocks();
    reset_callback_tracking();

    audiotap_system_config_t config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = 48000.0f,
        .channels = 1,
        .callback = test_callback,
    };
    audiotap_t *tap = audiotap_create_system(&config);
    ASSERT_NOT_NULL(tap);
    ASSERT_EQ(audiotap_system_start(tap), 0);

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
    // NULL timestamp -> host_time should be 0
    OSStatus result = stored_io_proc(200, NULL, &bufList, NULL, NULL, NULL, stored_io_proc_client_data);
    ASSERT_EQ(result, noErr);
    ASSERT_EQ(callback_call_count, 1);
    ASSERT_EQ((int)last_callback_host_time, 0);

    audiotap_system_stop(tap);
    audiotap_system_destroy(tap);
    free(tap);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    @autoreleasepool {
        printf("audiotap_system test suite\n");
        printf("==========================\n\n");

        printf("create_system:\n");
        RUN_TEST(test_create_system_null_config);
        RUN_TEST(test_create_system_null_callback);
        RUN_TEST(test_create_system_invalid_channels);
        RUN_TEST(test_create_system_global_stereo);
        RUN_TEST(test_create_system_global_mono);
        RUN_TEST(test_create_system_specific_pids_stereo);
        RUN_TEST(test_create_system_specific_pids_mono);
        RUN_TEST(test_create_system_pids_not_found);
        RUN_TEST(test_create_system_pids_no_process_list);
        RUN_TEST(test_create_system_pids_get_data_fails);
        RUN_TEST(test_create_system_pids_get_pid_fails);
        RUN_TEST(test_create_system_tap_fails);
        RUN_TEST(test_create_system_aggregate_fails);
        RUN_TEST(test_create_system_calloc_fails);

        printf("\nstart/stop/destroy:\n");
        RUN_TEST(test_system_start_success);
        RUN_TEST(test_system_start_io_proc_fails);
        RUN_TEST(test_system_start_device_start_fails);
        RUN_TEST(test_system_stop);
        RUN_TEST(test_system_stop_null_io_proc);
        RUN_TEST(test_system_destroy);
        RUN_TEST(test_system_destroy_unknown_ids);

        printf("\nio_proc callback:\n");
        RUN_TEST(test_system_io_proc_normal);
        RUN_TEST(test_system_io_proc_null_client_data);
        RUN_TEST(test_system_io_proc_empty_buffer);
        RUN_TEST(test_system_io_proc_null_timestamp);

        printf("\n==========================\n");
        printf("%d/%d tests passed\n", tests_passed, tests_run);
        return (tests_passed == tests_run) ? 0 : 1;
    }
}
