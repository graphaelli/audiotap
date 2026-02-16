#include "bridge.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Ring buffer capacity in floats.  65536 floats ≈ 4 s at 16 kHz mono.
#define RING_SIZE  (1 << 16)
#define RING_MASK  (RING_SIZE - 1)

struct bridge_state {
    float ring[RING_SIZE];
    _Atomic uint32_t write_pos;
    _Atomic uint32_t read_pos;
    int notify_fd;   // write end of self-pipe (non-blocking)
    int wait_fd;     // read end of self-pipe (blocking)
    _Atomic int closed;
};

bridge_state_t *bridge_create(void) {
    bridge_state_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    int fd[2];
    if (pipe(fd) != 0) { free(s); return NULL; }
    fcntl(fd[1], F_SETFL, O_NONBLOCK);

    s->wait_fd   = fd[0];
    s->notify_fd = fd[1];
    atomic_store(&s->write_pos, 0);
    atomic_store(&s->read_pos,  0);
    atomic_store(&s->closed, 0);
    return s;
}

void bridge_close(bridge_state_t *s) {
    if (!s) return;
    atomic_store(&s->closed, 1);
    char b = 1;
    (void)write(s->notify_fd, &b, 1);
}

void bridge_destroy(bridge_state_t *s) {
    if (!s) return;
    close(s->wait_fd);
    close(s->notify_fd);
    free(s);
}

// Real-time safe callback — called from the audio IO thread.
static void bridge_callback(const float *samples, uint32_t frame_count,
                            uint32_t channels, uint64_t host_time,
                            void *userdata) {
    (void)host_time;
    bridge_state_t *s = (bridge_state_t *)userdata;
    if (!s || atomic_load_explicit(&s->closed, memory_order_relaxed)) return;

    uint32_t count = frame_count * channels;
    uint32_t w = atomic_load_explicit(&s->write_pos, memory_order_relaxed);
    uint32_t r = atomic_load_explicit(&s->read_pos,  memory_order_acquire);

    // Drop oldest samples when the ring is full.
    uint32_t avail = RING_SIZE - (w - r);
    if (count > avail) {
        atomic_store_explicit(&s->read_pos, r + (count - avail),
                              memory_order_release);
    }

    // Copy into ring (may wrap).
    uint32_t pos   = w & RING_MASK;
    uint32_t first = RING_SIZE - pos;
    if (first >= count) {
        memcpy(&s->ring[pos], samples, count * sizeof(float));
    } else {
        memcpy(&s->ring[pos], samples, first * sizeof(float));
        memcpy(&s->ring[0], samples + first, (count - first) * sizeof(float));
    }
    atomic_store_explicit(&s->write_pos, w + count, memory_order_release);

    // Wake the reader (non-blocking; harmless if the pipe is full).
    char b = 1;
    (void)write(s->notify_fd, &b, 1);
}

int bridge_read(bridge_state_t *s, float *dst, uint32_t max_floats) {
    if (!s) return -1;

    for (;;) {
        uint32_t w = atomic_load_explicit(&s->write_pos, memory_order_acquire);
        uint32_t r = atomic_load_explicit(&s->read_pos,  memory_order_relaxed);
        uint32_t n = w - r;

        if (n > 0) {
            if (n > max_floats) n = max_floats;
            uint32_t pos   = r & RING_MASK;
            uint32_t first = RING_SIZE - pos;
            if (first >= n) {
                memcpy(dst, &s->ring[pos], n * sizeof(float));
            } else {
                memcpy(dst, &s->ring[pos], first * sizeof(float));
                memcpy(dst + first, &s->ring[0], (n - first) * sizeof(float));
            }
            atomic_store_explicit(&s->read_pos, r + n, memory_order_release);
            return (int)n;
        }

        if (atomic_load_explicit(&s->closed, memory_order_acquire))
            return 0;

        // Block until the callback writes something.
        char buf[64];
        ssize_t ret = read(s->wait_fd, buf, sizeof(buf));
        if (ret < 0 && errno != EINTR)
            return -1;
    }
}

void bridge_write_samples(bridge_state_t *s,
    const float *samples, uint32_t count) {
    bridge_callback(samples, count, 1, 0, s);
}

// --- Convenience constructors ---

audiotap_t *bridge_create_system(bridge_state_t *state,
    const pid_t *pids, uint32_t pid_count,
    float sample_rate, uint32_t channels, int mute) {

    audiotap_system_config_t cfg = {
        .pids        = pids,
        .pid_count   = pid_count,
        .sample_rate = sample_rate,
        .channels    = channels,
        .mute        = mute,
        .callback    = bridge_callback,
        .userdata    = state,
    };
    return audiotap_create_system(&cfg);
}

audiotap_t *bridge_create_mic(bridge_state_t *state,
    float sample_rate, uint32_t channels) {

    audiotap_mic_config_t cfg = {
        .sample_rate = sample_rate,
        .channels    = channels,
        .callback    = bridge_callback,
        .userdata    = state,
    };
    return audiotap_create_mic(&cfg);
}
