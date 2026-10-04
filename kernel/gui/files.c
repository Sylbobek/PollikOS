#include "app_internal.h"
#include "../vfs.h"
#include "../trash.h"
#include "../media.h"
#include "../process.h"
#include "../ui.h"

#define FILES_MAX_ITEMS 32

typedef struct {
    char name[64];
    u32 size;
    int is_dir;
} FilesEntry;

static FilesEntry g_files_entries[FILES_MAX_ITEMS];
static int g_files_count = 0;
static int g_files_selected = -1;
static int first_row = 0;
static char g_files_current_path[128] = "/home";
static int g_files_last_click_valid;
static char g_files_last_click_path[128];
static u32 g_files_last_click_tick;
static AppRect files_list(int width, int height);

/* Image / animated-GIF preview state. */
static int g_preview_active;
static u8 *g_preview_img;
static int g_preview_w, g_preview_h;
static MediaGif *g_preview_gif;
static MediaClip *g_preview_clip;
static u8 *g_preview_raw;
static char g_preview_name[64];

static int str_equal(const char *a, const char *b) {
    int i = 0;
    while (a[i] || b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return 1;
}

static void files_refresh_entries(void) {
    g_files_count = 0;
    g_files_selected = -1;
    first_row = 0;
    g_files_last_click_valid = 0;

    int fd = vfs_open(g_files_current_path, O_RDONLY);
    if (fd >= 0) {
        vfs_dirent_t dent;
        while (vfs_readdir(fd, &dent) > 0 && g_files_count < FILES_MAX_ITEMS) {
            if (dent.name[0] == '.') continue;
            FilesEntry *entry = &g_files_entries[g_files_count];
            memset(entry, 0, sizeof(FilesEntry));
            u32 n = 0;
            while (dent.name[n] && n < 63) { entry->name[n] = dent.name[n]; n++; }
            entry->name[n] = 0;
            entry->is_dir = (dent.type == VFS_DIR);

            char sub_path[128];
            u32 p = 0;
            while (g_files_current_path[p]) { sub_path[p] = g_files_current_path[p]; p++; }
            if (p > 0 && sub_path[p - 1] != '/') sub_path[p++] = '/';
            n = 0;
            while (dent.name[n] && p < 127) { sub_path[p++] = dent.name[n++]; }
            sub_path[p] = 0;

            vfs_stat_t st;
            if (vfs_stat(sub_path, &st) == 0) {
                entry->size = st.size;
            } else {
                entry->size = 0;
            }
            g_files_count++;
        }
        vfs_close(fd);
    }
}

void files_open_path(const char *path) {
    if (!path || !path[0]) return;
    if (g_preview_active) files_close();
    u32 p = 0;
    while (path[p] && p < 127) { g_files_current_path[p] = path[p]; p++; }
    g_files_current_path[p] = 0;

    files_refresh_entries();
    app_host_open(APP_FILES);
    app_host_invalidate(APP_FILES);
}

static int files_has_extension(const char *name, const char *extension) {
    int name_len = len(name), ext_len = len(extension);
    if (name_len < ext_len) return 0;
    for (int i = 0; i < ext_len; ++i) {
        char a = name[name_len - ext_len + i];
        char b = extension[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static void files_entry_path(const FilesEntry *entry, char path[128]) {
    u32 p = 0;
    while (g_files_current_path[p] && p < 127) { path[p] = g_files_current_path[p]; p++; }
    if (p > 0 && path[p - 1] != '/' && p < 127) path[p++] = '/';
    u32 n = 0;
    while (entry->name[n] && p < 127) path[p++] = entry->name[n++];
    path[p] = 0;
}

static void files_launch_pol(const char *path) {
    char elf_ident[5];
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0 || vfs_read(fd, elf_ident, sizeof(elf_ident)) != (int)sizeof(elf_ident)) {
        if (fd >= 0) vfs_close(fd);
        ui_notify("Pollik App", "The .pol file is unreadable", ICON_WARNING);
        return;
    }
    vfs_close(fd);

    if (elf_ident[0] != 0x7f || elf_ident[1] != 'E' || elf_ident[2] != 'L' || elf_ident[3] != 'F') {
        ui_notify("Pollik App", ".pol must be a PollikOS ELF program", ICON_WARNING);
        return;
    }
    if ((u8)elf_ident[4] == 2) {
        ui_notify("Pollik App", "64-bit .pol needs the x86_64 desktop", ICON_WARNING);
        return;
    }
    if ((u8)elf_ident[4] != 1) {
        ui_notify("Pollik App", "Unsupported .pol program format", ICON_WARNING);
        return;
    }

    int pid = process_spawn_elf_path(path);
    if (pid < 0) {
        ui_notify("Pollik App", "Program is invalid or process slots are full", ICON_WARNING);
        return;
    }
    char message[48] = "Started as process ";
    char pid_text[12];
    number(pid_text, (u32)pid);
    append_str(message, pid_text, sizeof(message));
    ui_notify("Pollik App", message, ICON_APP);
}

static void files_open_entry(const FilesEntry *entry, const char *path) {
    if (entry->is_dir) {
        files_open_path(path);
    } else if (files_has_extension(entry->name, ".pol")) {
        files_launch_pol(path);
    } else if (files_is_media(entry->name)) {
        files_open_image(path, entry->name);
    } else {
        notes_open_path(path);
    }
}

int files_is_media(const char *name) {
    const char *dot = 0;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    if (!dot) return 0;
    const char *e = dot + 1;
    return str_equal(e, "png") || str_equal(e, "jpg") || str_equal(e, "jpeg") ||
           str_equal(e, "bmp") || str_equal(e, "gif") || str_equal(e, "tga") ||
           str_equal(e, "psd") || str_equal(e, "pnm") || str_equal(e, "ppm") ||
           str_equal(e, "pkv");
}

void files_open_image(const char *path, const char *name) {
    files_close();   /* never let a previous image/GIF/clip leak into this one */
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return;
    vfs_stat_t st;
    u32 size = (vfs_stat(path, &st) == 0 && st.size > 0) ? st.size : 0;
    if (size == 0 || size > 48u * 1024u * 1024u) { vfs_close(fd); return; }
    u8 *raw = kmalloc(size);
    if (!raw) { vfs_close(fd); return; }
    int got = vfs_read(fd, raw, size);
    vfs_close(fd);
    if (got <= 0) { kfree(raw); return; }

    MediaClip *clip = media_clip_open(raw, (u32)got);
    MediaGif *gif = clip ? 0 : media_gif_open(raw, (u32)got);
    if (clip) {
        g_preview_clip = clip;
        g_preview_raw = raw;
        g_preview_w = media_clip_width(clip);
        g_preview_h = media_clip_height(clip);
    } else if (gif) {
        g_preview_gif = gif;
        g_preview_raw = raw;
        g_preview_w = media_gif_width(gif);
        g_preview_h = media_gif_height(gif);
    } else {
        int w = 0, h = 0;
        u8 *img = media_decode(raw, (u32)got, &w, &h);
        kfree(raw);
        if (!img) return;
        g_preview_img = img;
        g_preview_w = w;
        g_preview_h = h;
    }
    copy(g_preview_name, name);
    g_preview_active = 1;
    app_host_invalidate(APP_FILES);
}

void files_close(void) {
    if (g_preview_img) { media_free(g_preview_img); g_preview_img = 0; }
    if (g_preview_gif) { media_gif_close(g_preview_gif); g_preview_gif = 0; }
    if (g_preview_clip) { media_clip_close(g_preview_clip); g_preview_clip = 0; }
    if (g_preview_raw) { kfree(g_preview_raw); g_preview_raw = 0; }
    g_preview_active = 0;
    g_preview_w = g_preview_h = 0;
    g_preview_name[0] = 0;
}

void files_key(u8 code) {
    if ((code == 72 || code == 80) && !g_preview_active) { /* Up / Down */
        if (g_files_count == 0) files_refresh_entries();
        if (g_files_count == 0) return;
        if (g_files_selected < 0) g_files_selected = code == 72 ? g_files_count - 1 : 0;
        else if (code == 72 && g_files_selected > 0) g_files_selected--;
        else if (code == 80 && g_files_selected + 1 < g_files_count) g_files_selected++;

        GuiAppSize s = gui_app_size(APP_FILES);
        AppRect list = files_list(s.width, s.height);
        int visible_rows = list.h / 31;
        if (visible_rows < 1) visible_rows = 1;
        if (g_files_selected < first_row) first_row = g_files_selected;
        else if (g_files_selected >= first_row + visible_rows)
            first_row = g_files_selected - visible_rows + 1;
        app_host_invalidate(APP_FILES);
    } else if (code == 28 && !g_preview_active) { /* Enter */
        files_open_selected();
    } else if (code == 1 && g_preview_active) {   /* Esc */
        files_close();
        app_host_invalidate(APP_FILES);
    }
}

static AppRect files_preview_image_rect(int width, int height) {
    int iw = g_preview_w, ih = g_preview_h;
    int ay = 100, aw = width - 48, ah = height - 150;
    if (aw < 16) aw = 16;
    if (ah < 16) ah = 16;
    int scale = (int)((long)aw * 1000 / iw);
    int scale_y = (int)((long)ah * 1000 / ih);
    if (scale_y < scale) scale = scale_y;
    if (scale > 1000) scale = 1000;
    if (scale < 1) scale = 1;
    int draw_w = (int)((long)iw * scale / 1000);
    int draw_h = (int)((long)ih * scale / 1000);
    if (draw_w < 1) draw_w = 1;
    if (draw_h < 1) draw_h = 1;
    return (AppRect){(width - draw_w) / 2, ay + (ah - draw_h) / 2, draw_w, draw_h};
}

int files_poll(void) {
    if (!g_preview_active || (!g_preview_gif && !g_preview_clip)) return 0;
    int due = (g_preview_gif && media_gif_due(g_preview_gif)) ||
              (g_preview_clip && media_clip_due(g_preview_clip));
    if (!due) return 0;
    GuiAppSize size = gui_app_size(APP_FILES);
    AppRect frame = files_preview_image_rect(size.width, size.height);
    app_host_invalidate_partial_region(APP_FILES, frame.x, frame.y, frame.w, frame.h);
    return 0;
}

int files_preview_active(void) { return g_preview_active; }

static AppRect files_list(int width, int height) {
    int left = width < 600 ? 146 : 196;
    int rows = (height - 162) / 31;
    if (rows < 1) rows = 1;
    if (rows > FILES_MAX_ITEMS) rows = FILES_MAX_ITEMS;
    if (first_row > g_files_count - rows) first_row = g_files_count - rows;
    if (first_row < 0) first_row = 0;
    return (AppRect){left, 108, width - left - 32, rows * 31};
}

void files_scroll(int delta) {
    GuiAppSize s = gui_app_size(APP_FILES);
    AppRect list = files_list(s.width, s.height);
    int max = g_files_count - list.h / 31;
    if (max < 0) max = 0;
    if (delta > max - first_row) first_row = max;
    else if (delta < -first_row) first_row = 0;
    else first_row += delta;
    app_host_invalidate(APP_FILES);
}

void files_render(int width, int height, int active) {
    (void)active;
    int dark = ui_is_dark();
    if (g_preview_active) {
        roundrect(12, 54, width - 24, height - 70, 20, dark ? 0x0d1018 : 0xeae6f0);
        roundrect(20, 62, 84, 28, 10, dark ? 0x222a3d : 0xe0d6ef);
        centered(20, 68, 84, "< Back", dark ? 0x93c5fd : 0x6d538f, 1);
        text(116, 68, g_preview_name, dark ? 0xf1f5f9 : 0x40354f, 1);

        const u8 *src;
        if (g_preview_clip) src = media_clip_canvas(g_preview_clip, 1);
        else if (g_preview_gif) src = media_gif_canvas(g_preview_gif, 1);
        else src = g_preview_img;
        int iw = g_preview_w, ih = g_preview_h;
        if (src && iw > 0 && ih > 0) {
            AppRect frame = files_preview_image_rect(width, height);
            ui_bridge_blit_rgba(frame.x, frame.y, frame.w, frame.h, src, iw, ih);
        }
        char info[64] = "Image  ";
        char num[16];
        number(num, (u32)iw); append_str(info, num, sizeof(info));
        append_str(info, " x ", sizeof(info));
        number(num, (u32)ih); append_str(info, num, sizeof(info));
        if ((g_preview_clip && media_clip_animating(g_preview_clip)) ||
            (g_preview_gif && media_gif_animating(g_preview_gif)))
            append_str(info, "   (playing)", sizeof(info));
        app_label(24, height - 40, width - 48, info, dark ? 0x94a3b8 : 0x8c8099, 1);
        app_label(24, height - 24, width - 48, "Esc or < Back to return", dark ? 0x64748b : 0x8c8099, 1);
        return;
    }
    if (g_files_count == 0) files_refresh_entries();

    AppRect list = files_list(width, height);
    int sidebar_w = list.x - 38;

    /* Sidebar */
    roundrect(12, 54, sidebar_w, height - 70, 20, dark ? 0x111420 : 0xf0edf5);
    app_label(27, 69, sidebar_w - 28, "Places", dark ? 0x94a3b8 : 0x978aa3, 1);

    int is_home = str_equal(g_files_current_path, "/home");
    int is_desktop = str_equal(g_files_current_path, "/home/Desktop");
    int is_docs = str_equal(g_files_current_path, "/home/Desktop/Documents");
    int is_trash = str_equal(g_files_current_path, "/home/Trash");
    int is_apps = str_equal(g_files_current_path, "/Applications");

    /* Home */
    roundrect(20, 94, sidebar_w - 16, 28, 10, is_home ? (dark ? 0x222a3d : 0xe0d6ef) : (dark ? 0x111420 : 0xf0edf5));
    text(32, 100, "Home", is_home ? (dark ? 0x93c5fd : 0x6d538f) : (dark ? 0x94a3b8 : 0x7e718d), 1);

    /* Desktop */
    roundrect(20, 126, sidebar_w - 16, 28, 10, is_desktop ? (dark ? 0x222a3d : 0xe0d6ef) : (dark ? 0x111420 : 0xf0edf5));
    text(32, 132, "Desktop", is_desktop ? (dark ? 0x93c5fd : 0x6d538f) : (dark ? 0x94a3b8 : 0x7e718d), 1);

    /* Documents */
    roundrect(20, 158, sidebar_w - 16, 28, 10, is_docs ? (dark ? 0x222a3d : 0xe0d6ef) : (dark ? 0x111420 : 0xf0edf5));
    text(32, 164, "Documents", is_docs ? (dark ? 0x93c5fd : 0x6d538f) : (dark ? 0x94a3b8 : 0x7e718d), 1);

    /* Trash */
    roundrect(20, 190, sidebar_w - 16, 28, 10, is_trash ? (dark ? 0x222a3d : 0xe0d6ef) : (dark ? 0x111420 : 0xf0edf5));
    text(32, 196, "Trash", is_trash ? (dark ? 0x93c5fd : 0x6d538f) : (dark ? 0x94a3b8 : 0x7e718d), 1);

    /* Installed applications */
    roundrect(20, 222, sidebar_w - 16, 28, 10, is_apps ? (dark ? 0x222a3d : 0xe0d6ef) : (dark ? 0x111420 : 0xf0edf5));
    text(32, 228, "Applications", is_apps ? (dark ? 0x93c5fd : 0x6d538f) : (dark ? 0x94a3b8 : 0x7e718d), 1);

    /* Main view header */
    if (is_trash) {
        text(list.x + 2, 67, "Trash", dark ? 0xf1f5f9 : 0x40354f, 3);
        /* Empty Trash button */
        roundrect(width - 144, 62, 112, 28, 12, dark ? 0x3b1820 : 0xf6e6e6);
        centered(width - 144, 68, 112, "Empty Trash", dark ? 0xf87171 : 0xa93226, 1);
    } else {
        app_label(list.x + 2, 67, list.w - 10, g_files_current_path, dark ? 0xf1f5f9 : 0x40354f, 2);
    }

    /* File entries */
    int rows_visible = list.h / 31;
    for (int row = 0; row < rows_visible; row++) {
        int i = first_row + row;
        if (i >= g_files_count) break;
        int y = list.y + row * 31;
        FilesEntry *entry = &g_files_entries[i];

        int is_sel = (i == g_files_selected);
        roundrect(list.x, y, list.w, 27, 11, is_sel ? (dark ? 0x222a3d : 0xeae2f4) : (dark ? 0x141824 : 0xf3f0f7));
        if (is_sel && dark) {
            rect(list.x + 8, y, list.w - 16, 1, 0x384561);
        }

        /* Icon badge */
        int ix = list.x + 8, iy = y + 5;
        if (entry->is_dir) {
            roundrect(ix, iy, 8, 4, 1, 0xd97706);
            roundrect(ix, iy + 3, 18, 13, 2, 0xf59e0b);
            roundrect(ix + 1, iy + 3, 16, 1, 1, 0xfef3c7);
            text(list.x + 34, y + 5, entry->name, dark ? 0xf1f5f9 : 0x423352, 1);
            text(width - 90, y + 5, "folder", dark ? 0x94a3b8 : 0x9a8ba8, 1);
        } else {
        int is_app_file = files_has_extension(entry->name, ".pol");
            roundrect(ix + 1, iy, 14, 16, 2, is_app_file ? (dark ? 0x263b67 : 0xdce8ff) : 0xf8fafc);
            rect(ix + 11, iy, 4, 4, is_app_file ? 0x75a7ff : 0xcfd8dc);
            rect(ix + 3, iy + 2, 6, 2, is_app_file ? 0x3875df : 0x6366f1);
            rect(ix + 3, iy + 6, 8, 1, is_app_file ? 0x78a7f3 : 0x94a3b8);
            rect(ix + 3, iy + 9, 6, 1, is_app_file ? 0x78a7f3 : 0x94a3b8);
            rect(ix + 3, iy + 12, 7, 1, is_app_file ? 0x78a7f3 : 0xcbd5e1);
            text(list.x + 34, y + 5, entry->name, dark ? 0xf1f5f9 : 0x423352, 1);

            char size[12];
            number(size, entry->size);
            text(width - 134, y + 5, size, dark ? 0x94a3b8 : 0x9a8ba8, 1);
            text(width - 90, y + 5, is_app_file ? "app" : "bytes", dark ? 0x94a3b8 : 0x9a8ba8, 1);
        }

        /* If in Trash, show Restore button on right */
        if (is_trash) {
            roundrect(width - 210, y + 2, 64, 22, 9, dark ? 0x1e2638 : 0xe8e2f0);
            centered(width - 210, y + 5, 64, "Restore", dark ? 0x93c5fd : 0x6e5288, 1);
        }
    }

    /* Bottom status */
    char count_str[16];
    number(count_str, (u32)g_files_count);
    app_label(list.x + 7, height - 45, list.w - 14, is_trash ? "Items in Trash" : g_files_current_path, dark ? 0x94a3b8 : 0x8c8099, 1);
    app_label(list.x + 7, height - 24, list.w - 14, "Arrows select; Enter or double-click opens. Drag to desktop.", dark ? 0x64748b : 0x8c8099, 1);
}

void files_click(int x, int y) {
    if (g_preview_active) {
        if (x >= 20 && x < 104 && y >= 62 && y < 90) {
            files_close();
            app_host_invalidate(APP_FILES);
        }
        return;
    }
    GuiAppSize s = gui_app_size(APP_FILES);
    AppRect list = files_list(s.width, s.height);
    int sidebar_w = list.x - 38;

    /* Check sidebar clicks */
    if (x >= 20 && x < 20 + sidebar_w - 16) {
        if (y >= 94 && y < 122) { files_open_path("/home"); return; }
        if (y >= 126 && y < 154) { files_open_path("/home/Desktop"); return; }
        if (y >= 158 && y < 186) { files_open_path("/home/Desktop/Documents"); return; }
        if (y >= 190 && y < 218) { files_open_path("/home/Trash"); return; }
        if (y >= 222 && y < 250) { files_open_path("/Applications"); return; }
    }

    /* Check Empty Trash button */
    if (str_equal(g_files_current_path, "/home/Trash")) {
        if (x >= s.width - 144 && x < s.width - 32 && y >= 62 && y < 90) {
            trash_empty();
            files_refresh_entries();
            extern void desktop_items_scan(void);
            desktop_items_scan();
            app_host_invalidate(APP_FILES);
            return;
        }
    }

    /* Check list item clicks */
    if (app_hit(list, x, y) && (y - list.y) % 31 < 27) {
        int idx = first_row + (y - list.y) / 31;
        if (idx >= 0 && idx < g_files_count) {
            FilesEntry *entry = &g_files_entries[idx];
            g_files_selected = idx;

            /* Check if Restore button was clicked in Trash view */
            if (str_equal(g_files_current_path, "/home/Trash")) {
                if (x >= s.width - 210 && x < s.width - 146) {
                    trash_restore_item(entry->name);
                    files_refresh_entries();
                    extern void desktop_items_scan(void);
                    desktop_items_scan();
                    app_host_invalidate(APP_FILES);
                    return;
                }
            }

            char full_path[128];
            files_entry_path(entry, full_path);
            int double_click = g_files_last_click_valid &&
                str_equal(g_files_last_click_path, full_path) &&
                (u32)(ticks - g_files_last_click_tick) <= 75u;
            g_files_last_click_valid = !double_click;
            if (double_click) {
                files_open_entry(entry, full_path);
            } else {
                copy(g_files_last_click_path, full_path);
                g_files_last_click_tick = ticks;
                app_host_invalidate(APP_FILES);
            }
        }
    }
}

int files_select_at(int x, int y) {
    GuiAppSize s = gui_app_size(APP_FILES);
    AppRect list = files_list(s.width, s.height);
    if (app_hit(list, x, y) && (y - list.y) % 31 < 27) {
        int idx = first_row + (y - list.y) / 31;
        if (idx >= 0 && idx < g_files_count) {
            g_files_selected = idx;
            return idx;
        }
    }
    return -1;
}

void files_open_selected(void) {
    if (g_files_selected >= 0 && g_files_selected < g_files_count) {
        FilesEntry *entry = &g_files_entries[g_files_selected];
        char full_path[128];
        files_entry_path(entry, full_path);
        files_open_entry(entry, full_path);
    }
}

void files_delete_selected(void) {
    if (g_files_selected >= 0 && g_files_selected < g_files_count) {
        FilesEntry *entry = &g_files_entries[g_files_selected];
        char full_path[128];
        u32 p = 0;
        while (g_files_current_path[p]) { full_path[p] = g_files_current_path[p]; p++; }
        if (p > 0 && full_path[p - 1] != '/') full_path[p++] = '/';
        u32 n = 0;
        while (entry->name[n] && p < 127) { full_path[p++] = entry->name[n++]; }
        full_path[p] = 0;

if (str_equal(g_files_current_path, "/home/Trash")) {
            trash_delete_permanent(entry->name);
            files_refresh_entries();
            extern void desktop_items_scan(void);
            desktop_items_scan();
            app_host_invalidate(APP_FILES);
            return;
        }

        trash_move_item(full_path);
        files_refresh_entries();
        app_host_invalidate(APP_FILES);
    }
}

const char *files_selected_name(void) {
    if (g_files_selected >= 0 && g_files_selected < g_files_count) {
        return g_files_entries[g_files_selected].name;
    }
    return 0;
}
