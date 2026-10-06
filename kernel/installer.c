#include "installer.h"
#include "storage.h"
#include "system.h"
#include "pollikfs.h"

extern u8 _binary_build_install_runtime_prefix_bin_start[];
extern u8 _binary_build_install_runtime_prefix_bin_end[];
extern u8 _binary_build_install_wallpapers_pkg_bin_start[];
extern u8 _binary_build_install_wallpapers_pkg_bin_end[];
extern u8 _binary_build_install_icons_pkg_bin_start[];
extern u8 _binary_build_install_icons_pkg_bin_end[];
static int installer_install_icons(void) {
    const u8 *p=_binary_build_install_icons_pkg_bin_start;
    u32 size=(u32)(_binary_build_install_icons_pkg_bin_end-p),offset=8;
    if(size<8||memcmp(p,"ICPK",4)||*(const u32 *)(p+4)!=12)return 0;
    if(vfs_mkdir("/usr/share/icons")<0)return 0;
    for(unsigned i=0;i<12;i++) {
        if(size-offset<68)return 0;
        const char *path=(const char *)(p+offset);unsigned n=0;while(n<64&&path[n])n++;
        if(n==64||memcmp(path,"/usr/share/icons/",17))return 0;
        u32 count=*(const u32 *)(p+offset+64);offset+=68;
        if(!count||count>size-offset)return 0;
        vfs_file_t file;if(pollikfs_open(path,O_WRONLY|O_CREAT|O_EXCL,&file)<0)return 0;
        u32 written=0;
        while(written<count) {
            u32 chunk=count-written;if(chunk>4096)chunk=4096;
            int got=pollikfs_write(&file,p+offset+written,chunk);
            if(got<=0||(u32)got>chunk){pollikfs_close(&file);return 0;}
            written+=(u32)got;
        }
        if(pollikfs_close(&file)<0)return 0;offset+=count;
    }
    if(offset!=size)return 0;serial("INSTALL: 12 system icons installed\n");return 1;
}

int installer_install_wallpapers(void) {
    const u8 *package = _binary_build_install_wallpapers_pkg_bin_start;
    u32 package_size = (u32)(_binary_build_install_wallpapers_pkg_bin_end - package);
    if (package_size < 12 || memcmp(package, "WLPK", 4)) return 0;
    u32 light_size = *(const u32 *)(package + 4);
    u32 dark_size = *(const u32 *)(package + 8);
    if (!light_size || !dark_size || light_size > package_size - 12 ||
        dark_size != package_size - 12 - light_size) return 0;
    if (vfs_mkdir("/usr") < 0 || vfs_mkdir("/usr/share") < 0 ||
        vfs_mkdir("/usr/share/wallpapers") < 0) return 0;
    const char *paths[2] = {"/usr/share/wallpapers/light.png", "/usr/share/wallpapers/dark.png"};
    const u8 *sources[2] = {package + 12, package + 12 + light_size};
    u32 sizes[2] = {light_size, dark_size};
    for (int i = 0; i < 2; ++i) {
        vfs_file_t file;
        if (pollikfs_open(paths[i], O_WRONLY | O_CREAT | O_EXCL, &file) < 0) return 0;
        u32 offset = 0;
        while (offset < sizes[i]) {
            u32 count = sizes[i] - offset;
            if (count > 4096) count = 4096;
            int written = pollikfs_write(&file, sources[i] + offset, count);
            if (written <= 0 || (u32)written > count) { pollikfs_close(&file); return 0; }
            offset += (u32)written;
        }
        if (pollikfs_close(&file) < 0) return 0;
    }
    serial("INSTALL: wallpapers installed to PollikFS\n");
    return installer_install_icons();
}

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
