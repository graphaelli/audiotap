#ifndef AUDIOTAP_INTERNAL_H
#define AUDIOTAP_INTERNAL_H

#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <pthread.h>
#include "audiotap.h"

typedef enum {
    AUDIOTAP_TYPE_SYSTEM,
    AUDIOTAP_TYPE_MIC
} audiotap_type_t;

struct audiotap_t {
    audiotap_type_t type;
    audiotap_callback_t callback;
    void *userdata;

    float sample_rate;
    uint32_t channels;
    int running;

    // System tap specific
    AudioObjectID tap_id;
    AudioObjectID aggregate_device_id;

    // Mic specific
    AudioObjectID input_device_id;
    AudioObjectPropertyListenerBlock device_change_listener;
    dispatch_queue_t listener_queue;
    pthread_mutex_t mic_mutex;

    // Shared
    AudioDeviceIOProcID io_proc_id;
};

// Global: currently active system tap, so mic start can temporarily pause it.
// AudioDeviceStart for a physical mic device blocks indefinitely while an
// aggregate device (system tap) is running.  The workaround is to stop the
// aggregate device, start the mic, and restart the aggregate device.
extern audiotap_t *audiotap_active_system_tap;

// Internal start/stop per type (implemented in audiotap_system.m and audiotap_mic.c)
int  audiotap_system_start(audiotap_t *tap);
void audiotap_system_stop(audiotap_t *tap);
void audiotap_system_destroy(audiotap_t *tap);

int  audiotap_mic_start(audiotap_t *tap);
void audiotap_mic_stop(audiotap_t *tap);
void audiotap_mic_destroy(audiotap_t *tap);

#endif // AUDIOTAP_INTERNAL_H
