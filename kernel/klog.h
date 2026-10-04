#ifndef POLLIK_KLOG_H
#define POLLIK_KLOG_H

#ifdef POLLIK_X64
#include "arch/x86_64/fs_platform.h"
#else
#include "system.h"
#endif

#define KLOG_CAT_BOOT "BOOT"
#define KLOG_CAT_MEM  "MEM"
#define KLOG_CAT_PMM  "PMM"
#define KLOG_CAT_VMM  "VMM"
#define KLOG_CAT_PF   "PF"
#define KLOG_CAT_PROC "PROC"
#define KLOG_CAT_FS   "FS"
#define KLOG_CAT_NET  "NET"
#define KLOG_CAT_GUI  "GUI"
#define KLOG_CAT_SYSTEM "SYS"

#define KLOG_DEBUG(cat, msg) klog(cat, "DEBUG", msg)
#define KLOG_INFO(cat, msg)  klog(cat, "INFO",  msg)
#define KLOG_WARN(cat, msg)  klog(cat, "WARN",  msg)
#define KLOG_ERROR(cat, msg) klog(cat, "ERROR", msg)

void klog(const char *cat, const char *level, const char *msg);
void klog_hex(const char *cat, const char *prefix, u32 val);
void klog_dec(const char *cat, const char *prefix, u32 val);
void dump_registers(const void *frame_ptr, u32 cr2);
void panic(const char *msg, const void *frame_ptr);

#endif
