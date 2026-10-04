#include "installer.h"
#include "storage.h"
#include "system.h"

extern u8 _binary_build_install_runtime_prefix_bin_start[];
extern u8 _binary_build_install_runtime_prefix_bin_end[];

int installer_target_available(void) {
    u32 sectors;
    return storage_install_target_info(&sectors);
}

int installer_write_system(void) {
    u32 sectors;
    if (!storage_install_target_info(&sectors)) {
        serial("INSTALL: no supported primary-master ATA target\n");
        return 0;
    }
    u32 bytes = (u32)(_binary_build_install_runtime_prefix_bin_end -
                      _binary_build_install_runtime_prefix_bin_start);
    u32 count = (bytes + 511u) / 512u;
    if (!bytes || count >= POLLIK_INSTALLED_FS_LBA || count > sectors) {
        serial("INSTALL: runtime payload does not fit target layout\n");
        return 0;
    }
    u8 tail[512];
    for (u32 lba = 0; lba < count; ++lba) {
        const u8 *source = _binary_build_install_runtime_prefix_bin_start + lba * 512u;
        if (lba + 1 == count && bytes % 512u) {
            memset(tail, 0, sizeof(tail));
            memcpy(tail, source, bytes % 512u);
            source = tail;
        }
        if (!storage_install_write_sector(lba, source)) {
            serial("INSTALL: ATA write failed\n");
            return 0;
        }
    }
    if (!storage_install_flush()) {
        serial("INSTALL: ATA flush failed\n");
        return 0;
    }
    storage_use_install_target();
    serial("INSTALL: bootloader and runtime written to target\n");
    return 1;
}
