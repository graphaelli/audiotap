#include <stdlib.h>
#include <CoreAudio/CoreAudio.h>
#include "audiotap_internal.h"

audiotap_t *audiotap_active_system_tap = NULL;

int audiotap_start(audiotap_t *tap)
{
    if (!tap)
        return -1;
    if (tap->running)
        return 0;

    switch (tap->type) {
    case AUDIOTAP_TYPE_SYSTEM:
        return audiotap_system_start(tap);
    case AUDIOTAP_TYPE_MIC:
        return audiotap_mic_start(tap);
    }
    return -1;
}

void audiotap_stop(audiotap_t *tap)
{
    if (!tap || !tap->running)
        return;

    switch (tap->type) {
    case AUDIOTAP_TYPE_SYSTEM:
        audiotap_system_stop(tap);
        break;
    case AUDIOTAP_TYPE_MIC:
        audiotap_mic_stop(tap);
        break;
    }
}

void audiotap_destroy(audiotap_t *tap)
{
    if (!tap)
        return;

    if (tap->running)
        audiotap_stop(tap);

    switch (tap->type) {
    case AUDIOTAP_TYPE_SYSTEM:
        audiotap_system_destroy(tap);
        break;
    case AUDIOTAP_TYPE_MIC:
        audiotap_mic_destroy(tap);
        break;
    }

    free(tap);
}

const char *audiotap_error_string(int status)
{
    switch (status) {
    case 0:                          return "success";
    case kAudioHardwareNotRunningError:    return "hardware not running";
    case kAudioHardwareUnspecifiedError:   return "unspecified hardware error";
    case kAudioHardwareUnknownPropertyError: return "unknown property";
    case kAudioHardwareBadPropertySizeError: return "bad property size";
    case kAudioHardwareIllegalOperationError: return "illegal operation";
    case kAudioHardwareBadObjectError:    return "bad audio object";
    case kAudioHardwareBadDeviceError:    return "bad device";
    case kAudioHardwareBadStreamError:    return "bad stream";
    case kAudioHardwareUnsupportedOperationError: return "unsupported operation";
    case kAudioDeviceUnsupportedFormatError: return "unsupported format";
    case kAudioDevicePermissionsError:    return "permission denied";
    default:                              return "unknown error";
    }
}

int audiotap_is_running(const audiotap_t *tap)
{
    if (!tap)
        return 0;
    return tap->running;
}
