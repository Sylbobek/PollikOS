#include "audio.h"
#include "audio_chime.h"
#ifdef POLLIK_X64
#include "arch/x86_64/audio_platform.h"
#else
#include "hw.h"
#include "klog.h"
#include "mem.h"
#include "pmm.h"
#include "vfs.h"
#include "hal.h"
static void *audio_dma_alloc(uintptr_t *physical){*physical=pmm_alloc_page();return (void *)*physical;}
static void audio_dma_free(void *mapped,uintptr_t physical){if(mapped)pmm_free_page(physical);}
#endif

typedef short s16;
typedef int s32;

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

/* AC'97 Mixer register offsets */
#define AC97_MIX_RESET          0x00
#define AC97_MIX_MASTER_VOL     0x02
#define AC97_MIX_PCM_VOL        0x18
#define AC97_MIX_EXT_AUDIO_ID   0x2C
#define AC97_MIX_EXT_AUDIO_CTRL 0x2E
#define AC97_MIX_FRONT_DAC_RATE 0x32

/* AC'97 Bus Master offsets (PCM Out = 0x10) */
#define AC97_PO_BDBAR           0x10
#define AC97_PO_CIV             0x14
#define AC97_PO_LVI             0x15
#define AC97_PO_SR              0x16
#define AC97_PO_PICB            0x18
#define AC97_PO_PIP             0x1A
#define AC97_PO_CR              0x1B
#define AC97_GLOB_CNT           0x2C
#define AC97_GLOB_STA           0x30

/* Control Register bits */
#define AC97_CR_RP              0x01  /* Run / Pause */
#define AC97_CR_RR              0x02  /* Reset Registers */

/* Status Register bits */
#define AC97_SR_DCH             0x01  /* DMA Controller Halted */
#define AC97_SR_CELV            0x02  /* Current Equals Last Valid */
#define AC97_SR_BCIS            0x04  /* Buffer Completion */

/* BDL Entry Flags */
#define AC97_BDL_BUP            (1 << 14) /* Buffer Underrun Policy */
#define AC97_BDL_IOC            (1 << 15) /* Interrupt On Completion */

typedef struct __attribute__((packed)) {
    u32 ptr;
    u16 samples;
    u16 flags;
} Ac97BdlEntry;

#define NUM_BDL_ENTRIES 32
#define SAMPLES_PER_BUFFER 1024

static u16 s_nambar = 0;
static u16 s_nabmbar = 0;
static int s_ac97_present = 0;
static int selected_output=-1;
int audio_output(void){return selected_output<0?s_ac97_present:selected_output;}
int audio_select_output(int device){
    if(device<0||device>1||(device==1&&!s_ac97_present))return 0;
    selected_output=device;return 1;
}
static u8  s_volume = 85;

static Ac97BdlEntry *s_bdl = 0;
static uintptr_t s_bdl_phys = 0;
static s16 *s_pcm_buf = 0;
static uintptr_t s_pcm_phys = 0;
#ifdef POLLIK_X64
static PciDevice dma_controller;
#endif

static void ac97_write_mixer(u8 reg, u16 val) {
    if (!s_nambar) return;
    outw((u16)(s_nambar + reg), val);
}

static u16 ac97_read_mixer(u8 reg) {
    if (!s_nambar) return 0;
    return inw((u16)(s_nambar + reg));
}

static int ac97_reset_output(void){
    outb((u16)(s_nabmbar+AC97_PO_CR),AC97_CR_RR);
    for(unsigned n=0;n<10000;n++){
        if(!(inb((u16)(s_nabmbar+AC97_PO_CR))&AC97_CR_RR))return 1;
        __asm__ volatile("pause");
    }
    KLOG_WARN(KLOG_CAT_BOOT,"Audio: DMA reset timeout");return 0;
}
#ifdef POLLIK_X64
unsigned audio_bus_master_port(void){return s_ac97_present?s_nabmbar:0;}
int audio_reset_dma(void){return s_ac97_present&&ac97_reset_output();}
int audio_quiesce_dma(void){
    if(!s_ac97_present)return 1;
    outb((u16)(s_nabmbar+AC97_PO_CR),0);
    for(unsigned n=0;n<10000;n++)if(inw((u16)(s_nabmbar+AC97_PO_SR))&AC97_SR_DCH)return 1;
    /* A wedged controller must lose bus-master permission before its DMA
     * memory can be reclaimed. Do not advertise it as ready afterwards. */
    u32 command=pci_config_read32(dma_controller.bus,dma_controller.dev,dma_controller.fn,4);
    pci_config_write32(dma_controller.bus,dma_controller.dev,dma_controller.fn,4,(command&0xffffu)&~4u);
    s_ac97_present=0;KLOG_ERROR(KLOG_CAT_BOOT,"Audio: DMA halt timeout; bus mastering disabled");return 0;
}
#endif
int audio_init(void) {
    if(s_ac97_present)return 1;
    PciDevice devs[MAX_PCI_DEVICES];
    int count = pci_scan_bus(devs, MAX_PCI_DEVICES);
    int found_idx = -1;

    for (int i = 0; i < count; i++) {
        /* Register layout implemented here: Intel ICH AC97, not arbitrary
         * multimedia/audio devices (HDA and USB audio require other drivers). */
        if (devs[i].vendor_id == 0x8086 &&
            (devs[i].device_id == 0x2415 || devs[i].device_id == 0x2425 ||
             devs[i].device_id == 0x2445 || devs[i].device_id == 0x2485 ||
             devs[i].device_id == 0x24c5 || devs[i].device_id == 0x24d5)) {
            found_idx = i;
            break;
        }
    }

    if (found_idx < 0) {
        KLOG_INFO(KLOG_CAT_BOOT, "Audio: No AC'97 controller detected; PC speaker fallback enabled");
        return 0;
    }

    PciDevice *pci = &devs[found_idx];
#ifdef POLLIK_X64
    dma_controller=*pci;
#endif

    /* Enable I/O Space (bit 0) and Bus Master (bit 2) */
    u32 cmd = pci_config_read32(pci->bus, pci->dev, pci->fn, 0x04);
    pci_config_write32(pci->bus, pci->dev, pci->fn, 0x04, cmd | 0x05);

    /* Read BAR0 (NAMBAR) and BAR1 (NABMBAR) */
    u32 bar0 = pci_config_read32(pci->bus, pci->dev, pci->fn, 0x10);
    u32 bar1 = pci_config_read32(pci->bus, pci->dev, pci->fn, 0x14);

    s_nambar = (u16)(bar0 & ~0x1u);
    s_nabmbar = (u16)(bar1 & ~0x1u);

    if (!(bar0&1) || !(bar1&1) || bar0>65535 || bar1>65535 || !s_nambar || !s_nabmbar) {
        KLOG_WARN(KLOG_CAT_BOOT, "Audio: Invalid AC'97 BARs");
        return 0;
    }

    /* Allocate physical pages for BDL and PCM buffers */
    s_bdl = audio_dma_alloc(&s_bdl_phys);
    s_pcm_buf = audio_dma_alloc(&s_pcm_phys);
    if (!s_bdl || !s_pcm_buf) {
        audio_dma_free(s_bdl,s_bdl_phys);audio_dma_free(s_pcm_buf,s_pcm_phys);
        s_bdl=0;s_pcm_buf=0;s_bdl_phys=s_pcm_phys=0;
        KLOG_ERROR(KLOG_CAT_BOOT, "Audio: Failed to allocate DMA buffers");
        return 0;
    }

    memset(s_bdl, 0, PAGE_SIZE);
    memset(s_pcm_buf, 0, PAGE_SIZE);

    /* Cold reset the controller */
    outl((u16)(s_nabmbar + AC97_GLOB_CNT), 0x02);
    for (volatile int d = 0; d < 10000; d++) { __asm__ volatile("pause"); }

    /* Reset AC'97 mixer */
    ac97_write_mixer(AC97_MIX_RESET, 0x0000);

    /* Configure volume: 0x0000 = max volume, 0x1F1F = muted/attenuated */
    s_ac97_present = 1;
    audio_set_volume(s_volume);

    /* Set standard sample rate (48000 Hz) if supported */
    u16 ext_id = ac97_read_mixer(AC97_MIX_EXT_AUDIO_ID);
    if (ext_id & 1) {
        /* VRA (Variable Rate Audio) supported */
        ac97_write_mixer(AC97_MIX_EXT_AUDIO_CTRL, 1);
        ac97_write_mixer(AC97_MIX_FRONT_DAC_RATE, 48000);
    }

    s_ac97_present = 1;
    KLOG_INFO(KLOG_CAT_BOOT, "Audio: AC'97 hardware initialized (Intel 82801AA/ICH)");
    return 1;
}

int audio_is_available(void) {
    return s_ac97_present;
}

void audio_set_volume(u8 vol_0_to_100) {
    if (vol_0_to_100 > 100) vol_0_to_100 = 100;
    s_volume = vol_0_to_100;

    if (!s_ac97_present) return;

    /* AC'97 attenuation: 0 = 0 dB (loudest), 31 = -46.5 dB (quietest) */
    u32 atten = 31 - (vol_0_to_100 * 31 / 100);
    u16 val = (u16)((atten << 8) | atten);
    if (vol_0_to_100 == 0) val = 0x8000; /* Mute */

    ac97_write_mixer(AC97_MIX_MASTER_VOL, val);
    ac97_write_mixer(AC97_MIX_PCM_VOL, val);
}

u8 audio_get_volume(void) {
    return s_volume;
}

void audio_play_tone(u32 freq_hz, u32 duration_ms) {
    if (freq_hz == 0 || duration_ms == 0 || !s_volume || sound_is_muted()) return;

    if (!audio_output()) {
        speaker_beep(freq_hz, duration_ms);
        return;
    }
    /* Stop the previous asynchronous DMA before replacing its buffer. */
    if(!ac97_reset_output())return;

    /* Fill PCM buffer with 48 kHz stereo/mono tone (square/triangle wave for fast generation) */
    u32 sample_rate = 48000;
    u32 period_samples = sample_rate / freq_hz;
    if (period_samples == 0) period_samples = 1;
    u32 half_period=period_samples/2;if(!half_period)half_period=1;

    u32 total_samples = (sample_rate * duration_ms) / 1000;
    if (total_samples > SAMPLES_PER_BUFFER) total_samples = SAMPLES_PER_BUFFER;

    s16 amplitude = 12000;
    for (u32 i = 0; i < total_samples; i++) {
        /* Triangle waveform */
        u32 phase = i % period_samples;
        s16 val = (phase < half_period)
            ? (s16)(-amplitude + (2 * amplitude * (s32)phase) / (s32)half_period)
            : (s16)(amplitude - (2 * amplitude * (s32)(phase - half_period)) / (s32)half_period);
#ifdef POLLIK_X64
        s_pcm_buf[i*2]=s_pcm_buf[i*2+1]=val;
#else
        s_pcm_buf[i] = val;
#endif
    }

    /* Setup BDL entry 0 */
    s_bdl[0].ptr = (u32)s_pcm_phys;
    s_bdl[0].samples = (u16)total_samples;
#ifdef POLLIK_X64
    s_bdl[0].samples=(u16)(total_samples*2); /* AC97 counts channel samples. */
#endif
    s_bdl[0].flags = (u16)AC97_BDL_IOC;

    /* Reset PCM Out DMA channel */

    /* Set BDL base address */
    outl((u16)(s_nabmbar + AC97_PO_BDBAR), (u32)s_bdl_phys);

    /* Set Last Valid Index (entry 0 only) */
    outb((u16)(s_nabmbar + AC97_PO_LVI), 0);

    /* Clear status register */
    outw((u16)(s_nabmbar + AC97_PO_SR), 0x1C);

    /* Start DMA playback */
    outb((u16)(s_nabmbar + AC97_PO_CR), AC97_CR_RP);

    #ifndef POLLIK_X64
    /* Wait for playback completion or timeout */
    u32 timeout = duration_ms * 4000;
    while (!(inw((u16)(s_nabmbar + AC97_PO_SR)) & AC97_SR_CELV) && --timeout) {
        __asm__ volatile("pause");
    }

    /* Stop DMA */
    outb((u16)(s_nabmbar + AC97_PO_CR), 0);
    #endif
}

#ifndef POLLIK_X64
static s16 *s_chime_pcm;
#define CHIME_PAGES 24u
static int audio_play_chime(int login) {
    if (!audio_output()) return 0;
    /* Stop the previous DMA before touching its descriptors/sample memory. */
    if (!ac97_reset_output()) return 1;
    if (!s_chime_pcm) s_chime_pcm = (s16 *)pmm_alloc_pages(CHIME_PAGES);
    if (!s_chime_pcm) return 0;
    u32 frames = audio_chime_frames(login);
    u32 blocks = (frames + 1023u) / 1024u;
    for (u32 i = 0; i < frames; i++) {
        s16 sample = audio_chime_sample(login, i);
        s_chime_pcm[i * 2] = s_chime_pcm[i * 2 + 1] = sample;
    }
    for (u32 i = 0; i < blocks; i++) {
        u32 count = frames - i * 1024u;
        if (count > 1024u) count = 1024u;
        s_bdl[i].ptr = (u32)(uintptr_t)(s_chime_pcm + i * 2048u);
        s_bdl[i].samples = (u16)(count * 2u);
        s_bdl[i].flags = AC97_BDL_IOC;
    }
    if (ac97_read_mixer(AC97_MIX_EXT_AUDIO_ID) & 1u)
        ac97_write_mixer(AC97_MIX_FRONT_DAC_RATE, 48000);
    outl((u16)(s_nabmbar + AC97_PO_BDBAR), (u32)s_bdl_phys);
    outb((u16)(s_nabmbar + AC97_PO_LVI), (u8)(blocks - 1u));
    outw((u16)(s_nabmbar + AC97_PO_SR), 0x1c);
    outb((u16)(s_nabmbar + AC97_PO_CR), AC97_CR_RP);
    KLOG_INFO(KLOG_CAT_BOOT, login ? "Audio: login chime started" : "Audio: startup chime started");
    return 1;
}
#endif

void audio_play_sound(SoundEffect s) {
    if (!s_volume || sound_is_muted()) return;
    switch (s) {
        case SOUND_STARTUP:
#ifndef POLLIK_X64
            if (audio_play_chime(0)) break;
#endif
            audio_play_tone(392, 24);
            audio_play_tone(587, 36);
            break;
        case SOUND_LOGIN:
#ifndef POLLIK_X64
            if (audio_play_chime(1)) break;
#endif
            audio_play_tone(587, 20);
            audio_play_tone(784, 30);
            break;
        case SOUND_CLICK:
            audio_play_tone(1400, 15);
            break;
        case SOUND_ALERT:
            audio_play_tone(880, 70);
            audio_play_tone(1175, 100);
            break;
        case SOUND_TRASH:
            audio_play_tone(440, 40);
            audio_play_tone(330, 40);
            audio_play_tone(220, 70);
            break;
        default:
            audio_play_tone(800, 50);
            break;
    }
}

int audio_play_wav(const u8 *data, u32 len) {
    if(!audio_output())return 0; /* PC speaker is not a PCM sound output. */
    if (!data || len < 44) return 0;
    if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F') return 0;
    if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E') return 0;

    u32 offset = 12;
    u16 format = 0;
    u16 channels = 0;
    u32 sample_rate = 44100;
    u16 bits_per_sample = 16;
    u32 data_offset = 0;
    u32 data_len = 0;

    while (offset + 8 <= len) {
        char id0 = (char)data[offset];
        char id1 = (char)data[offset + 1];
        char id2 = (char)data[offset + 2];
        char id3 = (char)data[offset + 3];
        u32 chunk_sz = (u32)data[offset + 4] |
                       ((u32)data[offset + 5] << 8) |
                       ((u32)data[offset + 6] << 16) |
                       ((u32)data[offset + 7] << 24);
        offset += 8;

        if (id0 == 'f' && id1 == 'm' && id2 == 't' && id3 == ' ') {
            if (chunk_sz >= 16 && offset + 16 <= len) {
                format = (u16)data[offset] | ((u16)data[offset + 1] << 8);
                channels = (u16)data[offset + 2] | ((u16)data[offset + 3] << 8);
                sample_rate = (u32)data[offset + 4] |
                              ((u32)data[offset + 5] << 8) |
                              ((u32)data[offset + 6] << 16) |
                              ((u32)data[offset + 7] << 24);
                bits_per_sample = (u16)data[offset + 14] | ((u16)data[offset + 15] << 8);
            }
        } else if (id0 == 'd' && id1 == 'a' && id2 == 't' && id3 == 'a') {
            data_offset = offset;
            data_len = chunk_sz;
            if (data_offset + data_len > len) data_len = len - data_offset;
            break;
        }
        offset += chunk_sz;
    }

    if (format != 1 || data_offset == 0 || data_len == 0 || channels == 0) return 0;
    if (bits_per_sample != 8 && bits_per_sample != 16) return 0;

    if (!s_ac97_present) {
        u32 dur_ms = (data_len * 1000) / (sample_rate * channels * (bits_per_sample / 8));
        speaker_beep(523, dur_ms > 1000 ? 1000 : dur_ms);
        return 1;
    }

    u16 ext_id = ac97_read_mixer(AC97_MIX_EXT_AUDIO_ID);
    if (ext_id & 1) {
        ac97_write_mixer(AC97_MIX_FRONT_DAC_RATE, (u16)sample_rate);
    }

    u32 bytes_per_sample = (u32)(bits_per_sample / 8);
    u32 total_samples = data_len / (bytes_per_sample * channels);
    u32 sample_idx = 0;

    while (sample_idx < total_samples) {
        if(!ac97_reset_output())return 0;
        u32 chunk = total_samples - sample_idx;
        if (chunk > SAMPLES_PER_BUFFER) chunk = SAMPLES_PER_BUFFER;

        for (u32 s = 0; s < chunk; s++) {
            u32 src_idx = (sample_idx + s) * channels * bytes_per_sample;
            s16 pcm_val = 0;
            if (bits_per_sample == 16) {
                const u8 *sp = data + data_offset + src_idx;
                pcm_val = (s16)((u16)sp[0] | ((u16)sp[1] << 8));
            } else if (bits_per_sample == 8) {
                u8 val8 = data[data_offset + src_idx];
                pcm_val = (s16)(((s32)val8 - 128) << 8);
            }
            s_pcm_buf[s] = pcm_val;
        }

        s_bdl[0].ptr = (u32)s_pcm_phys;
        s_bdl[0].samples = (u16)chunk;
        s_bdl[0].flags = (u16)AC97_BDL_IOC;

        outl((u16)(s_nabmbar + AC97_PO_BDBAR), (u32)s_bdl_phys);
        outb((u16)(s_nabmbar + AC97_PO_LVI), 0);
        outw((u16)(s_nabmbar + AC97_PO_SR), 0x1C);
        outb((u16)(s_nabmbar + AC97_PO_CR), AC97_CR_RP);

        u32 chunk_ms = (chunk * 1000) / sample_rate;
        if (chunk_ms < 5) chunk_ms = 5;
        u32 timeout = chunk_ms * 4000;
        while (!(inw((u16)(s_nabmbar + AC97_PO_SR)) & AC97_SR_CELV) && --timeout) {
            __asm__ volatile("pause");
        }
        outb((u16)(s_nabmbar + AC97_PO_CR), 0);

        sample_idx += chunk;
    }

    return 1;
}

#ifndef POLLIK_X64
int audio_play_wav_file(const char *path) {
    if (!path || !path[0]) return 0;
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return 0;

    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0 || st.size < 44 || st.size > 2 * 1024 * 1024) {
        vfs_close(fd);
        return 0;
    }

    u8 *buf = (u8 *)kmalloc(st.size);
    if (!buf) {
        vfs_close(fd);
        return 0;
    }

    int read_bytes = vfs_read(fd, buf, st.size);
    vfs_close(fd);

    int ok = 0;
    if (read_bytes == (int)st.size) {
        ok = audio_play_wav(buf, st.size);
    }
    kfree(buf);
    return ok;
}
#endif
