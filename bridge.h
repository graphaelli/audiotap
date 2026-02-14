#ifndef AUDIOTAP_BRIDGE_H
#define AUDIOTAP_BRIDGE_H

#include "audiotap.h"
#include <stdint.h>

// Opaque bridge state — contains a lock-free SPSC ring buffer and
// a self-pipe for blocking reads from Go.
typedef struct bridge_state bridge_state_t;

bridge_state_t *bridge_create(void);
void bridge_destroy(bridge_state_t *state);

// Signal the reader to stop. Safe to call from any thread.
void bridge_close(bridge_state_t *state);

// Blocking read: copies up to max_floats into dst.
// Returns the number of floats copied, 0 on close, -1 on error.
int bridge_read(bridge_state_t *state, float *dst, uint32_t max_floats);

// Convenience constructors that wire bridge_callback as the audiotap callback.
audiotap_t *bridge_create_system(bridge_state_t *state,
    const pid_t *pids, uint32_t pid_count,
    float sample_rate, uint32_t channels, int mute);

audiotap_t *bridge_create_mic(bridge_state_t *state,
    float sample_rate, uint32_t channels);

#endif
