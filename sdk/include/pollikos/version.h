#ifndef POLLIKOS_VERSION_H
#define POLLIKOS_VERSION_H
/* PollikOS C SDK version. This identifies the host-side SDK package, not the
 * kernel ABI; see the SDK README for the ABI versions it targets. */
#define POLLIKOS_SDK_VERSION_MAJOR 1
#define POLLIKOS_SDK_VERSION_MINOR 0
#define POLLIKOS_SDK_VERSION_PATCH 0
const char *pollikos_sdk_version(void);
/* Kernel ABI versions this SDK was built for (startup/syscall/stat/dirent). */
#define POLLIKOS_STARTUP_ABI_VERSION 1
#define POLLIKOS_STAT_ABI_VERSION 1
#define POLLIKOS_DIRENT_ABI_VERSION 1
#endif
