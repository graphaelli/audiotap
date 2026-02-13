#import <AVFoundation/AVCaptureDevice.h>
#include <dispatch/dispatch.h>
#include "audiotap.h"

#ifdef AUDIOTAP_TESTING

static audiotap_permission_t mock_mic_permission = AUDIOTAP_PERMISSION_UNKNOWN;

audiotap_permission_t audiotap_mic_permission_status(void)
{
    return mock_mic_permission;
}

audiotap_permission_t audiotap_request_mic_permission(void)
{
    return mock_mic_permission;
}

#else

audiotap_permission_t audiotap_mic_permission_status(void)
{
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    switch (status) {
        case AVAuthorizationStatusAuthorized:
            return AUDIOTAP_PERMISSION_GRANTED;
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:
            return AUDIOTAP_PERMISSION_DENIED;
        default:
            return AUDIOTAP_PERMISSION_UNKNOWN;
    }
}

audiotap_permission_t audiotap_request_mic_permission(void)
{
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (status == AVAuthorizationStatusAuthorized)
        return AUDIOTAP_PERMISSION_GRANTED;
    if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted)
        return AUDIOTAP_PERMISSION_DENIED;

    // Status is not determined — trigger the system permission dialog.
    __block BOOL granted = NO;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL g) {
        granted = g;
        dispatch_semaphore_signal(sem);
    }];
    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

    return granted ? AUDIOTAP_PERMISSION_GRANTED : AUDIOTAP_PERMISSION_DENIED;
}

#endif
