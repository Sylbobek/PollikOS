#include "trash.h"
#include "vfs.h"
#include "klog.h"
#include "audio.h"

static TrashRecord g_trash_records[TRASH_MAX_RECORDS];
static int g_trash_count = 0;
static int g_trash_initialized = 0;

static const char *get_filename_part(const char *path) {
    if (!path) return "";
    const char *last_slash = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') last_slash = p + 1;
        p++;
    }
    return last_slash;
}

static void trash_save_metadata(void) {
    int fd = vfs_open("/home/Trash/.trashinfo", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    vfs_write(fd, &g_trash_count, sizeof(int));
    if (g_trash_count > 0) {
        vfs_write(fd, g_trash_records, (u32)g_trash_count * sizeof(TrashRecord));
    }
    vfs_close(fd);
}

static void trash_load_metadata(void) {
    memset(g_trash_records, 0, sizeof(g_trash_records));
    g_trash_count = 0;
    int fd = vfs_open("/home/Trash/.trashinfo", O_RDONLY);
    if (fd < 0) return;
    int count = 0;
    if (vfs_read(fd, &count, sizeof(int)) == sizeof(int)) {
        if (count > 0 && count <= TRASH_MAX_RECORDS) {
            u32 sz = (u32)count * sizeof(TrashRecord);
            if (vfs_read(fd, g_trash_records, sz) == (int)sz) {
                g_trash_count = count;
            }
        }
    }
    vfs_close(fd);
}

void trash_init(void) {
    if (g_trash_initialized) return;
    vfs_mkdir("/home");
    vfs_mkdir("/home/Trash");
    trash_load_metadata();

    /* Verify if files actually exist in /home/Trash */
    int fd = vfs_open("/home/Trash", O_RDONLY);
    int real_files = 0;
    if (fd >= 0) {
        vfs_dirent_t dent;
        while (vfs_readdir(fd, &dent) > 0) {
            if (dent.name[0] != '.') {
                real_files++;
            }
        }
        vfs_close(fd);
    }
    if (real_files == 0) {
        g_trash_count = 0;
        trash_save_metadata();
    }
    g_trash_initialized = 1;
    KLOG_INFO(KLOG_CAT_BOOT, "Trash system initialized");
}

int trash_has_items(void) {
    return g_trash_count > 0;
}

int trash_count(void) {
    return g_trash_count;
}

const TrashRecord *trash_get_record(int idx) {
    if (idx >= 0 && idx < g_trash_count) return &g_trash_records[idx];
    return 0;
}

int trash_move_item(const char *src_path) {
    if (!src_path || !src_path[0]) return -1;
    trash_init();

    if (g_trash_count >= TRASH_MAX_RECORDS) return -1;

    const char *base_name = get_filename_part(src_path);
    if (!base_name[0]) return -1;

    char trash_name[64];
    char dest_path[128];

    /* Copy base_name to trash_name */
    u32 i = 0;
    while (base_name[i] && i < 63) {
        trash_name[i] = base_name[i];
        i++;
    }
    trash_name[i] = 0;

    /* Build destination path: /home/Trash/<trash_name> */
    const char *prefix = "/home/Trash/";
    u32 p = 0;
    while (prefix[p]) { dest_path[p] = prefix[p]; p++; }
    u32 n = 0;
    while (trash_name[n]) { dest_path[p++] = trash_name[n++]; }
    dest_path[p] = 0;

    /* Check if dest_path exists; if so, append suffix */
    vfs_stat_t st;
    int suffix = 1;
    while (vfs_stat(dest_path, &st) == 0 && suffix < 100) {
        char sbuf[12];
        number(sbuf, suffix++);
        p = 0;
        while (prefix[p]) { dest_path[p] = prefix[p]; p++; }
        n = 0;
        while (base_name[n] && n < 48) { dest_path[p++] = base_name[n++]; }
        dest_path[p++] = '_';
        u32 sidx = 0;
        while (sbuf[sidx]) { dest_path[p++] = sbuf[sidx++]; }
        dest_path[p] = 0;
    }

    /* Move via VFS rename */
    if (vfs_rename(src_path, dest_path) < 0) {
        return -1;
    }

    /* Record metadata */
    TrashRecord *rec = &g_trash_records[g_trash_count];
    memset(rec, 0, sizeof(TrashRecord));
    
    /* Copy original path */
    i = 0;
    while (src_path[i] && i < 127) { rec->original_path[i] = src_path[i]; i++; }
    rec->original_path[i] = 0;

    /* Copy trash name */
    const char *final_trash_name = get_filename_part(dest_path);
    i = 0;
    while (final_trash_name[i] && i < 63) { rec->trash_name[i] = final_trash_name[i]; i++; }
    rec->trash_name[i] = 0;

    rec->deletion_time = ticks;
    g_trash_count++;
    trash_save_metadata();
    audio_play_sound(SOUND_TRASH);

    return 0;
}

int trash_restore_item(const char *trash_name) {
    if (!trash_name || !trash_name[0]) return -1;
    trash_init();

    int found_idx = -1;
    for (int k = 0; k < g_trash_count; k++) {
        int match = 1;
        for (int c = 0; trash_name[c] || g_trash_records[k].trash_name[c]; c++) {
            if (trash_name[c] != g_trash_records[k].trash_name[c]) { match = 0; break; }
        }
        if (match) { found_idx = k; break; }
    }
    if (found_idx < 0) return -1;

    TrashRecord *rec = &g_trash_records[found_idx];
    char trash_path[128];
    const char *prefix = "/home/Trash/";
    u32 p = 0;
    while (prefix[p]) { trash_path[p] = prefix[p]; p++; }
    u32 n = 0;
    while (rec->trash_name[n]) { trash_path[p++] = rec->trash_name[n++]; }
    trash_path[p] = 0;

    char target_path[128];
    p = 0;
    while (rec->original_path[p]) { target_path[p] = rec->original_path[p]; p++; }
    target_path[p] = 0;

    /* If original target already exists, append _restored */
    vfs_stat_t st;
    if (vfs_stat(target_path, &st) == 0) {
        const char *rest = "_restored";
        u32 r = 0;
        while (rest[r] && p < 127) { target_path[p++] = rest[r++]; }
        target_path[p] = 0;
    }

    if (vfs_rename(trash_path, target_path) < 0) {
        return -1;
    }

    /* Remove record by shifting subsequent records down */
    for (int k = found_idx; k < g_trash_count - 1; k++) {
        g_trash_records[k] = g_trash_records[k + 1];
    }
    g_trash_count--;
    memset(&g_trash_records[g_trash_count], 0, sizeof(TrashRecord));
    trash_save_metadata();

    return 0;
}

static int trash_remove_path_recursive(const char *path) {
    if (!path || !path[0]) return -1;
    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0) return -1;

    if (st.type == VFS_DIR) {
        int fd = vfs_open(path, O_RDONLY);
        if (fd >= 0) {
            vfs_dirent_t dent;
            while (vfs_readdir(fd, &dent) > 0) {
                if (dent.name[0] == '.') continue;
                char sub[128];
                u32 p = 0;
                while (path[p] && p < 110) { sub[p] = path[p]; p++; }
                if (p > 0 && sub[p - 1] != '/') sub[p++] = '/';
                u32 n = 0;
                while (dent.name[n] && p < 127) { sub[p++] = dent.name[n++]; }
                sub[p] = 0;
                trash_remove_path_recursive(sub);
            }
            vfs_close(fd);
        }
        return vfs_rmdir(path);
    } else {
        return vfs_unlink(path);
    }
}

int trash_delete_permanent(const char *trash_name) {
    if (!trash_name || !trash_name[0]) return -1;
    trash_init();

    char trash_path[128];
    const char *prefix = "/home/Trash/";
    u32 p = 0;
    while (prefix[p]) { trash_path[p] = prefix[p]; p++; }
    u32 n = 0;
    while (trash_name[n]) { trash_path[p++] = trash_name[n++]; }
    trash_path[p] = 0;

    if (trash_remove_path_recursive(trash_path) < 0) {
        if (vfs_unlink(trash_path) < 0 && vfs_rmdir(trash_path) < 0)
            return -1;
    }

    int found_idx = -1;
    for (int k = 0; k < g_trash_count; k++) {
        int match = 1;
        for (int c = 0; trash_name[c] || g_trash_records[k].trash_name[c]; c++) {
            if (trash_name[c] != g_trash_records[k].trash_name[c]) { match = 0; break; }
        }
        if (match) { found_idx = k; break; }
    }
    if (found_idx >= 0) {
        for (int k = found_idx; k < g_trash_count - 1; k++) {
            g_trash_records[k] = g_trash_records[k + 1];
        }
        g_trash_count--;
        memset(&g_trash_records[g_trash_count], 0, sizeof(TrashRecord));
        trash_save_metadata();
    }
    return 0;
}

int trash_empty(void) {
    trash_init();
    int fd = vfs_open("/home/Trash", O_RDONLY);
    if (fd >= 0) {
        vfs_dirent_t dent;
        while (vfs_readdir(fd, &dent) > 0) {
            if (dent.name[0] != '.') {
                char trash_path[128];
                const char *prefix = "/home/Trash/";
                u32 p = 0;
                while (prefix[p]) { trash_path[p] = prefix[p]; p++; }
                u32 n = 0;
                while (dent.name[n]) { trash_path[p++] = dent.name[n++]; }
                trash_path[p] = 0;
                trash_remove_path_recursive(trash_path);
            }
        }
        vfs_close(fd);
    }
    memset(g_trash_records, 0, sizeof(g_trash_records));
    g_trash_count = 0;
    vfs_unlink("/home/Trash/.trashinfo");
    audio_play_sound(SOUND_TRASH);
    return 0;
}
