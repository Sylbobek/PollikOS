#ifndef POLLIK_HW_H
#define POLLIK_HW_H

#if __has_include("system.h")
#include "system.h"
#elif __has_include("../system.h")
#include "../system.h"
#else
#include "system.h"
#endif

typedef struct {
    u8 second;
    u8 minute;
    u8 hour;
    u8 day;
    u8 month;
    u16 year;
} RtcTime;

typedef struct {
    u8 bus;
    u8 dev;
    u8 fn;
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
} PciDevice;

#define MAX_PCI_DEVICES 16

/* RTC */
void rtc_init(void);
void rtc_get_time(RtcTime *t);
void rtc_format_time(char *buf, int max_len);

/* Power */
void power_shutdown(void);
void power_reboot(void);

/* Speaker */
void speaker_beep(u32 freq_hz, u32 duration_ms);
int sound_is_muted(void);
void sound_set_muted(int muted);
u32 sound_get_freq(void);
void sound_set_freq(u32 freq);

/* PCI */
int pci_scan_bus(PciDevice *out_devs, int max_devs);
const char *pci_class_name(u8 class_code, u8 subclass);

#endif
