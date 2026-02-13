#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "audiotap.h"

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void system_callback(const float *samples, uint32_t frame_count,
                             uint32_t channels, uint64_t host_time, void *userdata)
{
    (void)host_time;
    FILE *fp = (FILE *)userdata;
    fwrite(samples, sizeof(float), frame_count * channels, fp);
}

static void mic_callback(const float *samples, uint32_t frame_count,
                          uint32_t channels, uint64_t host_time, void *userdata)
{
    (void)host_time;
    FILE *fp = (FILE *)userdata;
    fwrite(samples, sizeof(float), frame_count * channels, fp);
}

int main(int argc, char *argv[])
{
    int duration = 10;
    if (argc > 1)
        duration = atoi(argv[1]);
    if (duration <= 0)
        duration = 10;

    float sample_rate = 48000.0f;
    uint32_t sys_channels = 2;
    uint32_t mic_channels = 1;

    FILE *sys_file = fopen("system.pcm", "wb");
    FILE *mic_file = fopen("mic.pcm", "wb");
    if (!sys_file || !mic_file) {
        fprintf(stderr, "Failed to open output files\n");
        return 1;
    }

    audiotap_system_config_t sys_config = {
        .pids = NULL,
        .pid_count = 0,
        .sample_rate = sample_rate,
        .channels = sys_channels,
        .mute = 0,
        .callback = system_callback,
        .userdata = sys_file,
    };

    audiotap_mic_config_t mic_config = {
        .sample_rate = sample_rate,
        .channels = mic_channels,
        .callback = mic_callback,
        .userdata = mic_file,
    };

    printf("Requesting microphone permission...\n");
    audiotap_permission_t perm = audiotap_request_mic_permission();
    if (perm != AUDIOTAP_PERMISSION_GRANTED) {
        fprintf(stderr, "Microphone permission not granted (status=%d).\n"
                        "Grant access in System Settings > Privacy & Security > Microphone.\n",
                (int)perm);
    }

    printf("Creating system audio tap...\n");
    audiotap_t *sys_tap = audiotap_create_system(&sys_config);
    if (!sys_tap) {
        fprintf(stderr, "Failed to create system tap (need macOS 14.2+ and audio permissions)\n");
    }

    printf("Creating microphone tap...\n");
    audiotap_t *mic_tap = audiotap_create_mic(&mic_config);
    if (!mic_tap) {
        fprintf(stderr, "Failed to create mic tap (no input device?)\n");
    }

    if (!sys_tap && !mic_tap) {
        fprintf(stderr, "No audio sources available. Exiting.\n");
        fclose(sys_file);
        fclose(mic_file);
        return 1;
    }

    if (sys_tap) {
        int err = audiotap_start(sys_tap);
        if (err)
            fprintf(stderr, "System tap start error: %s (%d)\n", audiotap_error_string(err), err);
        else
            printf("System audio capture started.\n");
    }

    if (mic_tap) {
        int err = audiotap_start(mic_tap);
        if (err)
            fprintf(stderr, "Mic tap start error: %s (%d)\n", audiotap_error_string(err), err);
        else
            printf("Microphone capture started.\n");
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Recording for %d seconds (Ctrl+C to stop early)...\n", duration);
    for (int i = 0; i < duration && g_running; i++)
        sleep(1);

    printf("\nStopping...\n");

    if (sys_tap)
        audiotap_destroy(sys_tap);
    if (mic_tap)
        audiotap_destroy(mic_tap);

    fclose(sys_file);
    fclose(mic_file);

    printf("Done. Playback with:\n");
    printf("  ffplay -f f32le -ar %.0f -ac %u system.pcm\n", sample_rate, sys_channels);
    printf("  ffplay -f f32le -ar %.0f -ac %u mic.pcm\n", sample_rate, mic_channels);

    return 0;
}
