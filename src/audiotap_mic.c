#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <stdlib.h>
#include "audiotap_internal.h"

// IO proc callback for microphone input
static OSStatus mic_io_proc(AudioObjectID inDevice,
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

        if (channels == 1 && tap->channels == 2) {
            // Upmix mono to stereo
            float stereo[frame_count * 2];
            for (uint32_t f = 0; f < frame_count; f++)
                stereo[f * 2] = stereo[f * 2 + 1] = samples[f];
            tap->callback(stereo, frame_count, 2, host_time, tap->userdata);
        } else if (channels == 2 && tap->channels == 1) {
            // Downmix stereo to mono
            float mono[frame_count];
            for (uint32_t f = 0; f < frame_count; f++)
                mono[f] = (samples[f * 2] + samples[f * 2 + 1]) * 0.5f;
            tap->callback(mono, frame_count, 1, host_time, tap->userdata);
        } else {
            tap->callback(samples, frame_count, channels, host_time, tap->userdata);
        }
    }

    return noErr;
}

static AudioObjectID get_default_input_device(void)
{
    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };

    AudioObjectID device_id = kAudioObjectUnknown;
    UInt32 size = sizeof(device_id);
    OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr,
                                                  0, NULL, &size, &device_id);
    if (status != noErr)
        return kAudioObjectUnknown;

    return device_id;
}

// Internal start/stop — caller must hold tap->mic_mutex
static int mic_start_locked(audiotap_t *tap)
{
    OSStatus status = AudioDeviceCreateIOProcID(tap->input_device_id,
                                                 mic_io_proc,
                                                 tap,
                                                 &tap->io_proc_id);
    if (status != noErr)
        return (int)status;

#ifndef AUDIOTAP_TESTING
    // Fail fast if microphone permission has not been granted.
    // Without this, AudioDeviceStart blocks forever waiting for a
    // permission dialog that may never appear (e.g. in a CLI context).
    audiotap_permission_t perm = audiotap_mic_permission_status();
    if (perm != AUDIOTAP_PERMISSION_GRANTED) {
        AudioDeviceDestroyIOProcID(tap->input_device_id, tap->io_proc_id);
        tap->io_proc_id = NULL;
        return kAudioDevicePermissionsError;
    }
#endif

    // AudioDeviceStart for a physical input device blocks indefinitely
    // while an aggregate device (system tap) is running.  Work around
    // this Core Audio limitation by temporarily pausing the system tap.
    audiotap_t *sys = audiotap_active_system_tap;
    if (sys && sys->running)
        audiotap_system_stop(sys);

    status = AudioDeviceStart(tap->input_device_id, tap->io_proc_id);

    if (sys)
        audiotap_system_start(sys);

    if (status != noErr) {
        AudioDeviceDestroyIOProcID(tap->input_device_id, tap->io_proc_id);
        tap->io_proc_id = NULL;
        return (int)status;
    }

    tap->running = 1;
    return 0;
}

static void mic_stop_locked(audiotap_t *tap)
{
    if (tap->io_proc_id) {
        AudioDeviceStop(tap->input_device_id, tap->io_proc_id);
        AudioDeviceDestroyIOProcID(tap->input_device_id, tap->io_proc_id);
        tap->io_proc_id = NULL;
    }
    tap->running = 0;
}

// Device change listener: stops old IO proc, gets new device, restarts if was running.
// Serialized via mic_mutex to prevent concurrent Core Audio HAL calls with the main thread.
static void handle_device_change(audiotap_t *tap)
{
    pthread_mutex_lock(&tap->mic_mutex);

    AudioObjectID new_device = get_default_input_device();
    if (new_device == kAudioObjectUnknown || new_device == tap->input_device_id) {
        pthread_mutex_unlock(&tap->mic_mutex);
        return;
    }

    int was_running = tap->running;

    if (was_running) {
        mic_stop_locked(tap);
    }

    tap->input_device_id = new_device;

    if (was_running) {
        mic_start_locked(tap);
    }

    pthread_mutex_unlock(&tap->mic_mutex);
}

audiotap_t *audiotap_create_mic(const audiotap_mic_config_t *config)
{
    if (!config || !config->callback)
        return NULL;
    if (config->channels != 1 && config->channels != 2)
        return NULL;

    AudioObjectID device_id = get_default_input_device();
    if (device_id == kAudioObjectUnknown)
        return NULL;

    audiotap_t *tap = (audiotap_t *)calloc(1, sizeof(audiotap_t));
    if (!tap)
        return NULL;

    tap->type = AUDIOTAP_TYPE_MIC;
    tap->callback = config->callback;
    tap->userdata = config->userdata;
    tap->sample_rate = config->sample_rate;
    tap->channels = config->channels;
    tap->running = 0;
    tap->input_device_id = device_id;
    tap->io_proc_id = NULL;
    tap->tap_id = kAudioObjectUnknown;
    tap->aggregate_device_id = kAudioObjectUnknown;
    pthread_mutex_init(&tap->mic_mutex, NULL);

    // Register listener for default input device changes
    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };

    // Block_copy to move from stack to heap — must survive beyond this scope
    tap->device_change_listener = Block_copy(^(UInt32 inNumberAddresses,
                                                const AudioObjectPropertyAddress *inAddresses) {
        (void)inNumberAddresses;
        (void)inAddresses;
        handle_device_change(tap);
    });

    // Use a private serial queue — dispatch_get_main_queue() can deadlock
    // if Core Audio synchronizes with the main queue during AudioDeviceStart.
    tap->listener_queue = dispatch_queue_create("com.audiotap.listener", DISPATCH_QUEUE_SERIAL);

    AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &addr,
                                         tap->listener_queue,
                                         tap->device_change_listener);

    return tap;
}

int audiotap_mic_start(audiotap_t *tap)
{
    pthread_mutex_lock(&tap->mic_mutex);
    int result = mic_start_locked(tap);
    pthread_mutex_unlock(&tap->mic_mutex);
    return result;
}

void audiotap_mic_stop(audiotap_t *tap)
{
    pthread_mutex_lock(&tap->mic_mutex);
    mic_stop_locked(tap);
    pthread_mutex_unlock(&tap->mic_mutex);
}

void audiotap_mic_destroy(audiotap_t *tap)
{
    // Remove the device change listener
    if (tap->device_change_listener) {
        AudioObjectPropertyAddress addr = {
            .mSelector = kAudioHardwarePropertyDefaultInputDevice,
            .mScope = kAudioObjectPropertyScopeGlobal,
            .mElement = kAudioObjectPropertyElementMain
        };
        AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &addr,
                                                tap->listener_queue,
                                                tap->device_change_listener);
        Block_release(tap->device_change_listener);
        tap->device_change_listener = NULL;
    }
    if (tap->listener_queue) {
        dispatch_release(tap->listener_queue);
        tap->listener_queue = NULL;
    }
    pthread_mutex_destroy(&tap->mic_mutex);
}
