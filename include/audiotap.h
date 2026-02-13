#ifndef AUDIOTAP_H
#define AUDIOTAP_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback signature for audio delivery.
// Called on a real-time audio thread — do not allocate, lock, or block.
// samples: interleaved float PCM
// frame_count: number of frames in this buffer
// channels: number of channels (1 or 2)
// host_time: mach_continuous_time timestamp for correlation between streams
// userdata: opaque pointer passed at creation
typedef void (*audiotap_callback_t)(
    const float *samples,
    uint32_t frame_count,
    uint32_t channels,
    uint64_t host_time,
    void *userdata
);

typedef struct audiotap_t audiotap_t;

// --- System audio (Core Audio Tap on output) ---

typedef struct {
    const pid_t *pids;        // process IDs to tap; NULL = all processes
    uint32_t pid_count;       // number of PIDs; 0 when pids is NULL
    float sample_rate;        // desired sample rate (e.g. 48000.0)
    uint32_t channels;        // 1 (mono) or 2 (stereo)
    int mute;                 // if nonzero, mute tapped audio (don't play through speakers)
    audiotap_callback_t callback;
    void *userdata;
} audiotap_system_config_t;

audiotap_t *audiotap_create_system(const audiotap_system_config_t *config);

// --- Microphone input (default input device) ---

typedef struct {
    float sample_rate;        // desired sample rate
    uint32_t channels;        // 1 (mono) or 2 (stereo)
    audiotap_callback_t callback;
    void *userdata;
} audiotap_mic_config_t;

audiotap_t *audiotap_create_mic(const audiotap_mic_config_t *config);

// --- Lifecycle ---

int  audiotap_start(audiotap_t *tap);   // returns 0 on success, nonzero OSStatus on failure
void audiotap_stop(audiotap_t *tap);
void audiotap_destroy(audiotap_t *tap); // stops if running, frees all resources

// --- Permissions ---

typedef enum {
    AUDIOTAP_PERMISSION_UNKNOWN = 0,   // not yet determined
    AUDIOTAP_PERMISSION_GRANTED = 1,
    AUDIOTAP_PERMISSION_DENIED  = 2,
} audiotap_permission_t;

// Check current microphone permission status (non-blocking).
audiotap_permission_t audiotap_mic_permission_status(void);

// Request microphone permission.  Blocks until the user responds to the
// system dialog.  Returns the resulting permission status.
audiotap_permission_t audiotap_request_mic_permission(void);

// --- Utility ---

const char *audiotap_error_string(int status); // human-readable error for OSStatus
int audiotap_is_running(const audiotap_t *tap);

#ifdef __cplusplus
}
#endif

#endif // AUDIOTAP_H
