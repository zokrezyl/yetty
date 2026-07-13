/* Stub for AOSP's log/log.h, which the Android NDK does not ship.
 * fdk-aac's libSBRdec includes it under __ANDROID__ and calls
 * android_errorWriteLog() to report a decoder security event to the
 * platform log; outside an AOSP platform build a no-op is correct. */
#ifndef YETTY_FDK_AAC_ANDROID_COMPAT_LOG_LOG_H
#define YETTY_FDK_AAC_ANDROID_COMPAT_LOG_LOG_H

#define android_errorWriteLog(tag, sub_tag) ((void)0)

#endif /* YETTY_FDK_AAC_ANDROID_COMPAT_LOG_LOG_H */
