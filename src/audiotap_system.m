#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <AudioToolbox/AudioToolbox.h>
#include <stdlib.h>
#include "audiotap_internal.h"

// IO proc callback for system audio tap
static OSStatus system_io_proc(AudioObjectID inDevice,
                               const AudioTimeStamp *inNow,
                               const AudioBufferList *inInputData,
                               const AudioTimeStamp *inInputTime,
                               AudioBufferList *outOutputData,
                               const AudioTimeStamp *inOutputTime,
                               void *inClientData)
{
    (void)inDevice;
    (void)inNow;
    (void)outOutputData;
    (void)inOutputTime;

    audiotap_t *tap = (audiotap_t *)inClientData;
    if (!tap || !tap->callback || !inInputData)
        return noErr;

    for (UInt32 i = 0; i < inInputData->mNumberBuffers; i++) {
        const AudioBuffer *buf = &inInputData->mBuffers[i];
        if (!buf->mData || buf->mDataByteSize == 0)
            continue;

        const float *samples = (const float *)buf->mData;
        uint32_t channels = buf->mNumberChannels;
        uint32_t frame_count = buf->mDataByteSize / (channels * sizeof(float));
        uint64_t host_time = inInputTime ? inInputTime->mHostTime : 0;

        tap->callback(samples, frame_count, channels, host_time, tap->userdata);
    }

    return noErr;
}

// Resolve PIDs to AudioObjectIDs for processes that are currently producing audio
static NSArray<NSNumber *> *resolve_pids_to_process_objects(const pid_t *pids, uint32_t pid_count)
{
    // Get all audio process objects from the system
    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyProcessObjectList,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };

    UInt32 size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, NULL, &size);
    if (status != noErr || size == 0)
        return @[];

    UInt32 count = size / sizeof(AudioObjectID);
    AudioObjectID *objects = (AudioObjectID *)malloc(size);
    if (!objects)
        return @[];

    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, objects);
    if (status != noErr) {
        free(objects);
        return @[];
    }

    // For each process object, get its PID and check if it matches
    NSMutableArray<NSNumber *> *result = [NSMutableArray array];
    AudioObjectPropertyAddress pidAddr = {
        .mSelector = kAudioProcessPropertyPID,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };

    for (UInt32 i = 0; i < count; i++) {
        pid_t objPid = 0;
        UInt32 pidSize = sizeof(pid_t);
        status = AudioObjectGetPropertyData(objects[i], &pidAddr, 0, NULL, &pidSize, &objPid);
        if (status != noErr)
            continue;

        for (uint32_t j = 0; j < pid_count; j++) {
            if (objPid == pids[j]) {
                [result addObject:@(objects[i])];
                break;
            }
        }
    }

    free(objects);
    return result;
}

audiotap_t *audiotap_create_system(const audiotap_system_config_t *config)
{
    if (!config || !config->callback)
        return NULL;
    if (config->channels != 1 && config->channels != 2)
        return NULL;

    @autoreleasepool {
        // Build CATapDescription
        CATapDescription *tapDesc = nil;

        if (config->pids == NULL || config->pid_count == 0) {
            // Tap all processes (global tap excluding nothing)
            if (config->channels == 1)
                tapDesc = [[CATapDescription alloc] initMonoGlobalTapButExcludeProcesses:@[]];
            else
                tapDesc = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
        } else {
            // Tap specific processes
            NSArray<NSNumber *> *processObjects = resolve_pids_to_process_objects(config->pids, config->pid_count);
            if (processObjects.count == 0) {
                return NULL;
            }
            if (config->channels == 1)
                tapDesc = [[CATapDescription alloc] initMonoMixdownOfProcesses:processObjects];
            else
                tapDesc = [[CATapDescription alloc] initStereoMixdownOfProcesses:processObjects];
        }

        if (!tapDesc)
            return NULL;

        // Configure tap
        [tapDesc setName:@"audiotap"];
        [tapDesc setPrivate:YES];
        if (config->mute)
            [tapDesc setMuteBehavior:CATapMuted];
        else
            [tapDesc setMuteBehavior:CATapUnmuted];

        // Create the process tap
        AudioObjectID tap_id = kAudioObjectUnknown;
        OSStatus status = AudioHardwareCreateProcessTap(tapDesc, &tap_id);
        if (status != noErr)
            return NULL;

        // Get the tap's UUID string for the aggregate device
        NSString *tapUUID = [tapDesc.UUID UUIDString];

        // Create private aggregate device with the tap
        NSDictionary *tapEntry = @{
            @kAudioSubTapUIDKey: tapUUID,
        };
        NSDictionary *aggDesc = @{
            @kAudioAggregateDeviceNameKey: @"audiotap_aggregate",
            @kAudioAggregateDeviceUIDKey: @"com.audiotap.aggregate",
            @kAudioAggregateDeviceIsPrivateKey: @YES,
            @kAudioAggregateDeviceTapListKey: @[tapEntry],
            @kAudioAggregateDeviceTapAutoStartKey: @YES,
        };

        AudioObjectID aggregate_id = kAudioObjectUnknown;
        status = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggDesc, &aggregate_id);
        if (status != noErr) {
            AudioHardwareDestroyProcessTap(tap_id);
            return NULL;
        }

        // Allocate the tap struct
        audiotap_t *tap = (audiotap_t *)calloc(1, sizeof(audiotap_t));
        if (!tap) {
            AudioHardwareDestroyAggregateDevice(aggregate_id);
            AudioHardwareDestroyProcessTap(tap_id);
            return NULL;
        }

        tap->type = AUDIOTAP_TYPE_SYSTEM;
        tap->callback = config->callback;
        tap->userdata = config->userdata;
        tap->sample_rate = config->sample_rate;
        tap->channels = config->channels;
        tap->running = 0;
        tap->tap_id = tap_id;
        tap->aggregate_device_id = aggregate_id;
        tap->io_proc_id = NULL;

        return tap;
    }
}

int audiotap_system_start(audiotap_t *tap)
{
    OSStatus status = AudioDeviceCreateIOProcID(tap->aggregate_device_id,
                                                 system_io_proc,
                                                 tap,
                                                 &tap->io_proc_id);
    if (status != noErr)
        return (int)status;

    status = AudioDeviceStart(tap->aggregate_device_id, tap->io_proc_id);
    if (status != noErr) {
        AudioDeviceDestroyIOProcID(tap->aggregate_device_id, tap->io_proc_id);
        tap->io_proc_id = NULL;
        return (int)status;
    }

    tap->running = 1;
    audiotap_active_system_tap = tap;
    return 0;
}

void audiotap_system_stop(audiotap_t *tap)
{
    if (tap->io_proc_id) {
        AudioDeviceStop(tap->aggregate_device_id, tap->io_proc_id);
        AudioDeviceDestroyIOProcID(tap->aggregate_device_id, tap->io_proc_id);
        tap->io_proc_id = NULL;
    }
    tap->running = 0;
    if (audiotap_active_system_tap == tap)
        audiotap_active_system_tap = NULL;
}

void audiotap_system_destroy(audiotap_t *tap)
{
    if (tap->aggregate_device_id != kAudioObjectUnknown) {
        AudioHardwareDestroyAggregateDevice(tap->aggregate_device_id);
        tap->aggregate_device_id = kAudioObjectUnknown;
    }
    if (tap->tap_id != kAudioObjectUnknown) {
        AudioHardwareDestroyProcessTap(tap->tap_id);
        tap->tap_id = kAudioObjectUnknown;
    }
}
