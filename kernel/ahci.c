#include "ahci.h"
#include "vmm.h"
#include "pmm.h"
#include "klog.h"

#define PCI_ADDR 0xcf8
#define PCI_DATA 0xcfc
#define HBA_GHC_AE (1u << 31)
#define HBA_PxCMD_ST 1u
#define HBA_PxCMD_FRE (1u << 4)
#define HBA_PxCMD_FR (1u << 14)
#define HBA_PxCMD_CR (1u << 15)
#define HBA_PxIS_TFES (1u << 30)

typedef struct {
    volatile u32 clb, clbu, fb, fbu, is, ie, cmd, reserved0, tfd, sig;
    volatile u32 ssts, sctl, serr, sact, ci, sntf, fbs;
    volatile u32 reserved1[11], vendor[4];
} HbaPort;

typedef struct {
    volatile u32 cap, ghc, is, pi, vs, ccc_ctl, ccc_pts, em_loc, em_ctl, cap2, bohc;
    u8 reserved[0xa0 - 0x2c];
    u8 vendor[0x100 - 0xa0];
    HbaPort ports[32];
} HbaMem;

typedef struct __attribute__((packed)) {
    u16 flags, prdtl;
    volatile u32 prdbc;
    u32 ctba, ctbau, reserved[4];
} CommandHeader;

typedef struct __attribute__((packed)) {
    u32 dba, dbau, reserved, dbc;
} Prdt;

typedef struct __attribute__((packed)) {
    u8 cfis[64], acmd[16], reserved[48];
    Prdt prdt[1];
} CommandTable;

static HbaMem *hba;
static HbaPort *port;
static uintptr_t dma_page;
static CommandHeader *headers;
static CommandTable *table;
static u8 *bounce;
static u32 sectors;
static int initialized;

static u32 pci_read(u8 dev, u8 fn, u8 reg) {
    outl(PCI_ADDR, 0x80000000u | ((u32)dev << 11) | ((u32)fn << 8) | (reg & 0xfcu));
    return inl(PCI_DATA);
}
static void pci_write(u8 dev, u8 fn, u8 reg, u32 value) {
    outl(PCI_ADDR, 0x80000000u | ((u32)dev << 11) | ((u32)fn << 8) | (reg & 0xfcu));
    outl(PCI_DATA, value);
}

static int wait_clear(volatile u32 *reg, u32 mask) {
    for (u32 i = 0; i < 10000000u; ++i) if (!(*reg & mask)) return 1;
    return 0;
}

static int issue(u8 command, u32 lba, int write, int data) {
    if (!port || !headers || !table) return 0;
    if (!wait_clear(&port->tfd, 0x88u)) return 0;
    memset(headers, 0, 1024);
    memset(table, 0, sizeof(*table));
    headers[0].flags = 5u | (write ? (1u << 6) : 0);
    headers[0].prdtl = data ? 1 : 0;
    headers[0].ctba = (u32)(uintptr_t)table;
    if (data) {
        table->prdt[0].dba = (u32)(uintptr_t)bounce;
        table->prdt[0].dbc = 511u | (1u << 31);
    }
    u8 *fis = table->cfis;
    fis[0] = 0x27;
    fis[1] = 1u << 7;
    fis[2] = command;
    fis[4] = (u8)lba; fis[5] = (u8)(lba >> 8); fis[6] = (u8)(lba >> 16);
    fis[7] = 1u << 6;
    fis[8] = (u8)(lba >> 24);
    fis[12] = data ? 1 : 0;
    port->is = 0xffffffffu;
    port->ci = 1u;
    for (u32 i = 0; i < 20000000u; ++i) {
        if (!(port->ci & 1u)) return !(port->is & HBA_PxIS_TFES);
        if (port->is & HBA_PxIS_TFES) return 0;
    }
    return 0;
}

static int identify(void) {
    if (!issue(0xec, 0, 0, 1)) return 0;
    const u16 *id = (const u16 *)bounce;
    if (id[83] & (1u << 10)) {
        u64 count = (u64)id[100] | ((u64)id[101] << 16) |
                    ((u64)id[102] << 32) | ((u64)id[103] << 48);
        sectors = count > 0xffffffffu ? 0xffffffffu : (u32)count;
    } else sectors = (u32)id[60] | ((u32)id[61] << 16);
    return sectors != 0;
}

int ahci_init(void) {
    if (initialized) return port != 0;
    initialized = 1;
    u8 found_dev = 0xff, found_fn = 0xff;
    u32 abar = 0;
    for (u8 dev = 0; dev < 32 && !abar; ++dev) for (u8 fn = 0; fn < 8; ++fn) {
        u32 id = pci_read(dev, fn, 0);
        if ((id & 0xffffu) == 0xffffu) { if (!fn) break; else continue; }
        u32 class_reg = pci_read(dev, fn, 8);
        if ((class_reg >> 24) == 1 && ((class_reg >> 16) & 0xffu) == 6 &&
            ((class_reg >> 8) & 0xffu) == 1) {
            abar = pci_read(dev, fn, 0x24) & ~0x0fu;
            found_dev = dev; found_fn = fn; break;
        }
    }
    if (!abar || abar >= 0xfffff000u) return 0;
    u32 command = pci_read(found_dev, found_fn, 4);
    pci_write(found_dev, found_fn, 4, command | 6u);
    for (u32 off = 0; off < 8192; off += PAGE_SIZE)
        if (!map_page(vmm_get_kernel_directory(), abar + off, abar + off,
                      PAGE_PRESENT | PAGE_RW | PAGE_NOCACHE)) return 0;
    hba = (HbaMem *)(uintptr_t)abar;
    hba->ghc |= HBA_GHC_AE;
    for (u32 i = 0; i < 32; ++i) {
        if (!(hba->pi & (1u << i))) continue;
        HbaPort *candidate = &hba->ports[i];
        u32 ssts = candidate->ssts;
        if ((ssts & 0x0fu) == 3 && ((ssts >> 8) & 0x0fu) == 1 && candidate->sig == 0x00000101u) {
            port = candidate; break;
        }
    }
    if (!port) return 0;
    port->cmd &= ~(HBA_PxCMD_ST | HBA_PxCMD_FRE);
    if (!wait_clear(&port->cmd, HBA_PxCMD_CR | HBA_PxCMD_FR)) { port = 0; return 0; }
    dma_page = pmm_alloc_page();
    if (!dma_page) { port = 0; return 0; }
    memset((void *)dma_page, 0, 4096);
    headers = (CommandHeader *)dma_page;
    table = (CommandTable *)(dma_page + 1280);
    bounce = (u8 *)(dma_page + 2048);
    port->clb = (u32)dma_page; port->clbu = 0;
    port->fb = (u32)(dma_page + 1024); port->fbu = 0;
    port->serr = 0xffffffffu; port->is = 0xffffffffu;
    port->cmd |= HBA_PxCMD_FRE | HBA_PxCMD_ST;
    if (!identify()) { port = 0; return 0; }
    KLOG_INFO(KLOG_CAT_BOOT, "AHCI SATA disk initialized");
    return 1;
}

int ahci_present(void) { return ahci_init(); }
u32 ahci_sector_count(void) { return ahci_init() ? sectors : 0; }
int ahci_read_sector(u32 lba, void *buffer) {
    if (!buffer || !ahci_init() || lba >= sectors || !issue(0x25, lba, 0, 1)) return 0;
    memcpy(buffer, bounce, 512); return 1;
}
int ahci_write_sector(u32 lba, const void *buffer) {
    if (!buffer || !ahci_init() || lba >= sectors) return 0;
    memcpy(bounce, buffer, 512); return issue(0x35, lba, 1, 1);
}
int ahci_flush(void) { return ahci_init() && issue(0xea, 0, 0, 0); }
