#include "desktop_items.h"
#include "vfs.h"
#include "trash.h"
#include "ui.h"
#include "ui_animation.h"
#include "shell_internal.h"
#include "graphics.h"
#include "klog.h"
#include "ui_data.h"
#include "sample_media.h"

#define GRID_START_X  24
#define GRID_START_Y  44
#define GRID_CELL_W   104
#define GRID_CELL_H   96

static DesktopItem g_desktop_items[DESKTOP_MAX_ITEMS];
static int g_desktop_item_count = 0;
static int g_selected_item = -1;

/* Drag state */
static int g_drag_active = 0;
static int g_drag_item = -1;
static int g_drag_moved = 0;
static int g_drag_start_mx = 0, g_drag_start_my = 0;
static int g_drag_cur_mx = 0, g_drag_cur_my = 0;
static int g_trash_hovered = 0;

/* Marquee state */
static int g_marquee_active = 0;
static int g_marquee_start_x = 0, g_marquee_start_y = 0;
static int g_marquee_cur_x = 0, g_marquee_cur_y = 0;

typedef struct {
    char name[64];
    int col;
    int row;
} LayoutEntry;

#define MAX_LAYOUT_ENTRIES 64
static LayoutEntry g_layout[MAX_LAYOUT_ENTRIES];
static int g_layout_count = 0;

static int str_equal(const char *a, const char *b) {
    int i = 0;
    while (a[i] || b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return 1;
}

static int str_ends_with(const char *s, const char *suffix) {
    int slen = 0, suflen = 0;
    while (s[slen]) slen++;
    while (suffix[suflen]) suflen++;
    if (slen < suflen) return 0;
    for (int i = 0; i < suflen; i++) {
        if (s[slen - suflen + i] != suffix[i]) return 0;
    }
    return 1;
}

static int is_cell_occupied(int col, int row, int exclude_item) {
    for (int i = 0; i < g_desktop_item_count; i++) {
        if (i == exclude_item) continue;
        if (g_desktop_items[i].col == col && g_desktop_items[i].row == row) {
            return 1;
        }
    }
    return 0;
}

static void find_free_cell(int *out_col, int *out_row, int exclude_item) {
    int max_rows = (shell.height - 160) / GRID_CELL_H;
    if (max_rows < 1) max_rows = 1;
    int max_cols = (shell.width - 48) / GRID_CELL_W;
    if (max_cols < 1) max_cols = 1;

    for (int col = 0; col < max_cols; col++) {
        for (int row = 0; row < max_rows; row++) {
            if (!is_cell_occupied(col, row, exclude_item)) {
                *out_col = col;
                *out_row = row;
                return;
            }
        }
    }
    *out_col = 0;
    *out_row = 0;
}

static void load_layout(void) {
    g_layout_count = 0;
    int fd = vfs_open("/home/Desktop/.layout", O_RDONLY);
    if (fd < 0) return;
    int count = 0;
    if (vfs_read(fd, &count, sizeof(int)) == sizeof(int)) {
        if (count > 0 && count <= MAX_LAYOUT_ENTRIES) {
            u32 sz = (u32)count * sizeof(LayoutEntry);
            if (vfs_read(fd, g_layout, sz) == (int)sz) {
                g_layout_count = count;
            }
        }
    }
    vfs_close(fd);
}

static void save_layout(void) {
    g_layout_count = 0;
    for (int i = 0; i < g_desktop_item_count && g_layout_count < MAX_LAYOUT_ENTRIES; i++) {
        DesktopItem *item = &g_desktop_items[i];
        LayoutEntry *entry = &g_layout[g_layout_count++];
        memset(entry, 0, sizeof(LayoutEntry));
        u32 c = 0;
        while (item->name[c] && c < 63) { entry->name[c] = item->name[c]; c++; }
        entry->name[c] = 0;
        entry->col = item->col;
        entry->row = item->row;
    }
    int fd = vfs_open("/home/Desktop/.layout", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    vfs_write(fd, &g_layout_count, sizeof(int));
    if (g_layout_count > 0) {
        vfs_write(fd, g_layout, (u32)g_layout_count * sizeof(LayoutEntry));
    }
    vfs_close(fd);
}

static void add_item(DesktopItemType type, const char *name, const char *path, int app_id) {
    if (g_desktop_item_count >= DESKTOP_MAX_ITEMS) return;
    DesktopItem *item = &g_desktop_items[g_desktop_item_count];
    memset(item, 0, sizeof(DesktopItem));
    item->type = type;
    item->app_id = app_id;
    item->col = -1;
    item->row = -1;
    item->selected = 0;

    u32 i = 0;
    while (name[i] && i < 63) { item->name[i] = name[i]; i++; }
    item->name[i] = 0;

    i = 0;
    while (path[i] && i < 127) { item->path[i] = path[i]; i++; }
    item->path[i] = 0;

    /* Match layout */
    for (int k = 0; k < g_layout_count; k++) {
        if (str_equal(g_layout[k].name, item->name)) {
            if (!is_cell_occupied(g_layout[k].col, g_layout[k].row, g_desktop_item_count)) {
                item->col = g_layout[k].col;
                item->row = g_layout[k].row;
                break;
            }
        }
    }

    if (item->col < 0) {
        find_free_cell(&item->col, &item->row, g_desktop_item_count);
    }

    item->x = GRID_START_X + item->col * GRID_CELL_W;
    item->y = GRID_START_Y + item->row * GRID_CELL_H;
    item->w = GRID_CELL_W;
    item->h = GRID_CELL_H;

    g_desktop_item_count++;
}

void desktop_items_scan(void) {
    g_desktop_item_count = 0;
    load_layout();

    /* 1. Add applications to desktop */
    add_item(ITEM_APP, "Files", "/bin/files", APP_FILES);
    add_item(ITEM_APP, "Terminal", "/bin/terminal", APP_TERMINAL);
    add_item(ITEM_APP, "Notes", "/bin/notes", APP_NOTES);
    add_item(ITEM_APP, "Web", "/bin/browser", APP_BROWSER);
    add_item(ITEM_APP, "Settings", "/bin/settings", APP_SETTINGS);
    add_item(ITEM_APP, "PollikMark", "/bin/pollikmark", APP_POLLIKMARK);

    /* 2. Read /home/Desktop real items via VFS */
    int fd = vfs_open("/home/Desktop", O_RDONLY);
    if (fd >= 0) {
        vfs_dirent_t dent;
        while (vfs_readdir(fd, &dent) > 0) {
            if (dent.name[0] == '.') continue; /* Skip hidden (including .layout) */

            char full_path[128];
            const char *prefix = "/home/Desktop/";
            u32 p = 0;
            while (prefix[p]) { full_path[p] = prefix[p]; p++; }
            u32 n = 0;
            while (dent.name[n] && p < 127) { full_path[p++] = dent.name[n++]; }
            full_path[p] = 0;

            DesktopItemType t = ITEM_FILE;
            if (dent.type == VFS_DIR) {
                t = ITEM_DIR;
            } else if (str_ends_with(dent.name, ".txt")) {
                t = ITEM_TXT;
            }
            add_item(t, dent.name, full_path, -1);
        }
        vfs_close(fd);
    }

    /* 3. Add Trash */
    add_item(ITEM_TRASH, "Trash", "/home/Trash", -1);

    for (int k = 0; k < g_desktop_item_count; k++) {
        char cbuf[12], rbuf[12], xbuf[12], ybuf[12];
        number(cbuf, (u32)g_desktop_items[k].col);
        number(rbuf, (u32)g_desktop_items[k].row);
        number(xbuf, (u32)g_desktop_items[k].x);
        number(ybuf, (u32)g_desktop_items[k].y);
        serial("DESKTOP_ITEM: ");
        serial(g_desktop_items[k].name);
        serial(" col="); serial(cbuf);
        serial(" row="); serial(rbuf);
        serial(" x="); serial(xbuf);
        serial(" y="); serial(ybuf);
        serial("\n");
    }

    save_layout();
    request_scene_redraw();
}

/* Drop ready-to-open demo media (PNG, animated GIF, PKV clip) on the desktop
 * the first time, so images and video can be launched straight from there. */
static void seed_sample_media(void) {
    for (unsigned i = 0; i < sizeof(sample_media) / sizeof(sample_media[0]); i++) {
        char path[96];
        int p = 0;
        const char *pfx = "/home/Desktop/";
        while (pfx[p]) { path[p] = pfx[p]; p++; }
        const char *n = sample_media[i].name;
        while (*n && p < 90) path[p++] = *n++;
        path[p] = 0;
        if (vfs_stat(path, &(vfs_stat_t){0}) == 0) continue;
        int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) continue;
        vfs_write(fd, sample_media[i].data, sample_media[i].len);
        vfs_close(fd);
    }
}

void desktop_items_init(void) {
    trash_init();
    vfs_mkdir("/home");
    vfs_mkdir("/home/Desktop");
    vfs_mkdir("/Applications");
    seed_sample_media();

    /* Desktop shortcuts, sample media and Trash live in PollikFS; native GUI
     * clients remain kernel components, while /Applications stores .pol ELF. */
    desktop_items_scan();
    KLOG_INFO(KLOG_CAT_BOOT, "Desktop items initialized");
}

int desktop_items_count(void) {
    return g_desktop_item_count;
}

const DesktopItem *desktop_items_get(int idx) {
    if (idx >= 0 && idx < g_desktop_item_count) return &g_desktop_items[idx];
    return 0;
}

int desktop_items_hit_test(int mx, int my) {
    for (int i = 0; i < g_desktop_item_count; i++) {
        DesktopItem *item = &g_desktop_items[i];
        if (mx >= item->x && mx < item->x + item->w &&
            my >= item->y && my < item->y + item->h) {
            return i;
        }
    }
    return -1;
}

/* Multi-selection */
void desktop_items_clear_selection(void) {
    g_selected_item = -1;
    for (int i = 0; i < g_desktop_item_count; i++) {
        g_desktop_items[i].selected = 0;
    }
    request_scene_redraw();
}

void desktop_items_select_single(int idx) {
    g_selected_item = idx;
    for (int i = 0; i < g_desktop_item_count; i++) {
        g_desktop_items[i].selected = (i == idx);
    }
    request_scene_redraw();
}

void desktop_items_toggle_select(int idx) {
    if (idx >= 0 && idx < g_desktop_item_count) {
        g_desktop_items[idx].selected = !g_desktop_items[idx].selected;
        if (g_desktop_items[idx].selected) {
            g_selected_item = idx;
        } else if (g_selected_item == idx) {
            g_selected_item = desktop_items_get_first_selected();
        }
        request_scene_redraw();
    }
}

int desktop_items_is_selected(int idx) {
    if (idx >= 0 && idx < g_desktop_item_count) {
        return g_desktop_items[idx].selected;
    }
    return 0;
}

int desktop_items_selected_count(void) {
    int count = 0;
    for (int i = 0; i < g_desktop_item_count; i++) {
        if (g_desktop_items[i].selected) count++;
    }
    return count;
}

int desktop_items_get_first_selected(void) {
    for (int i = 0; i < g_desktop_item_count; i++) {
        if (g_desktop_items[i].selected) return i;
    }
    return -1;
}

void desktop_items_select(int idx) {
    if (idx < 0) desktop_items_clear_selection();
    else desktop_items_select_single(idx);
}

int desktop_items_get_selected(void) {
    return desktop_items_get_first_selected();
}

void desktop_items_open(int idx) {
    if (idx < 0 || idx >= g_desktop_item_count) return;
    DesktopItem *item = &g_desktop_items[idx];

    if (item->type == ITEM_APP) {
        if (item->app_id >= 0) {
            ui_anim_dock_launch(item->app_id);
            open_app(item->app_id);
        }
    } else if (item->type == ITEM_DIR) {
        extern void files_open_path(const char *path);
        files_open_path(item->path);
        open_app(APP_FILES);
    } else if (item->type == ITEM_TXT) {
        extern int notes_open_path(const char *path);
        notes_open_path(item->path);
        open_app(APP_NOTES);
    } else if (item->type == ITEM_TRASH) {
        extern void files_open_path(const char *path);
        files_open_path("/home/Trash");
        open_app(APP_FILES);
    } else if (item->type == ITEM_FILE) {
        if (files_is_media(item->name)) {
            files_open_image(item->path, item->name);
            open_app(APP_FILES);
        } else {
            notes_open_path(item->path);
            open_app(APP_NOTES);
        }
    }
}

int desktop_items_create_folder(void) {
    char name[64];
    char path[128];
    const char *base = "New Folder";
    u32 i = 0;
    while (base[i]) { name[i] = base[i]; i++; }
    name[i] = 0;

    int suffix = 1;
    vfs_stat_t st;
    for (;;) {
        const char *pfx = "/home/Desktop/";
        u32 p = 0;
        while (pfx[p]) { path[p] = pfx[p]; p++; }
        u32 n = 0;
        while (name[n]) { path[p++] = name[n++]; }
        path[p] = 0;

        if (vfs_stat(path, &st) != 0) break;

        suffix++;
        char sbuf[12];
        number(sbuf, suffix);
        p = 0;
        while (base[p]) { name[p] = base[p]; p++; }
        name[p++] = ' ';
        n = 0;
        while (sbuf[n]) { name[p++] = sbuf[n++]; }
        name[p] = 0;
    }

    int res = vfs_mkdir(path);
    desktop_items_scan();
    for (int k = 0; k < g_desktop_item_count; k++) {
        if (str_equal(g_desktop_items[k].name, name)) {
            desktop_items_select_single(k);
            break;
        }
    }
    request_scene_redraw();
    return res;
}

int desktop_items_create_text_file(void) {
    char name[64];
    char path[128];
    const char *base = "New Text File";
    u32 i = 0;
    while (base[i]) { name[i] = base[i]; i++; }
    name[i] = 0;

    int suffix = 1;
    vfs_stat_t st;
    for (;;) {
        const char *pfx = "/home/Desktop/";
        u32 p = 0;
        while (pfx[p]) { path[p] = pfx[p]; p++; }
        u32 n = 0;
        while (name[n]) { path[p++] = name[n++]; }
        const char *ext = ".txt";
        u32 e = 0;
        while (ext[e]) { path[p++] = ext[e++]; }
        path[p] = 0;

        if (vfs_stat(path, &st) != 0) break;

        suffix++;
        char sbuf[12];
        number(sbuf, suffix);
        p = 0;
        while (base[p]) { name[p] = base[p]; p++; }
        name[p++] = ' ';
        n = 0;
        while (sbuf[n]) { name[p++] = sbuf[n++]; }
        name[p] = 0;
    }

    /* Create 0-byte file */
    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0) {
        vfs_close(fd);
    }
    desktop_items_scan();
    char full_name[64];
    u32 fn = 0;
    while (name[fn]) { full_name[fn] = name[fn]; fn++; }
    full_name[fn++] = '.'; full_name[fn++] = 't'; full_name[fn++] = 'x'; full_name[fn++] = 't'; full_name[fn] = 0;
    for (int k = 0; k < g_desktop_item_count; k++) {
        if (str_equal(g_desktop_items[k].name, full_name)) {
            desktop_items_select_single(k);
            break;
        }
    }
    request_scene_redraw();
    return (fd >= 0) ? 0 : -1;
}

int desktop_items_rename(int idx, const char *new_name) {
    if (idx < 0 || idx >= g_desktop_item_count || !new_name || !new_name[0]) return -1;
    DesktopItem *item = &g_desktop_items[idx];
    if (item->type == ITEM_APP || item->type == ITEM_TRASH) return -1;

    char new_path[128];
    const char *pfx = "/home/Desktop/";
    u32 p = 0;
    while (pfx[p]) { new_path[p] = pfx[p]; p++; }
    u32 n = 0;
    while (new_name[n] && p < 127) { new_path[p++] = new_name[n++]; }
    new_path[p] = 0;

    int res = vfs_rename(item->path, new_path);
    desktop_items_scan();
    for (int k = 0; k < g_desktop_item_count; k++) {
        if (str_equal(g_desktop_items[k].name, new_name)) {
            desktop_items_select_single(k);
            break;
        }
    }
    return res;
}

int desktop_items_move_to_trash(int idx) {
    if (idx < 0 || idx >= g_desktop_item_count) return -1;
    DesktopItem *item = &g_desktop_items[idx];
    if (item->type == ITEM_APP || item->type == ITEM_TRASH) return -1;

    int res = trash_move_item(item->path);
    desktop_items_scan();
    return res;
}

int desktop_items_move_selected_to_trash(void) {
    int moved_any = 0;
    for (int i = 0; i < g_desktop_item_count; i++) {
        if (g_desktop_items[i].selected) {
            DesktopItemType t = g_desktop_items[i].type;
            if (t == ITEM_APP || t == ITEM_TRASH) continue;
            trash_move_item(g_desktop_items[i].path);
            moved_any = 1;
        }
    }
    if (moved_any) {
        desktop_items_scan();
        return 0;
    }
    return -1;
}

/* Drag & Drop */
int desktop_items_is_dragging(void) {
    return g_drag_active;
}

int desktop_items_drag_moved(void) {
    return g_drag_moved;
}

int desktop_items_is_trash_hovered(void) {
    return g_trash_hovered;
}

int desktop_items_trash_hit(int mx, int my) {
    for (int i = 0; i < g_desktop_item_count; i++) {
        if (g_desktop_items[i].type == ITEM_TRASH) {
            DesktopItem *tr = &g_desktop_items[i];
            if (mx >= tr->x && mx < tr->x + tr->w && my >= tr->y && my < tr->y + tr->h) {
                return 1;
            }
        }
    }
    return 0;
}

void desktop_items_drag_start(int idx, int mx, int my) {
    if (idx < 0 || idx >= g_desktop_item_count) return;
    g_drag_active = 1;
    g_drag_item = idx;
    g_drag_moved = 0;
    g_drag_start_mx = mx;
    g_drag_start_my = my;
    g_drag_cur_mx = mx;
    g_drag_cur_my = my;
    g_trash_hovered = 0;

    if (!g_desktop_items[idx].selected) {
        desktop_items_select_single(idx);
    }
}

/* Bounding box of the selected items' source cells unioned with their ghost
 * cells at (dx,dy). Used to repaint only the moved pixels instead of the whole
 * scene on every PS/2 packet. */
static void drag_bounds_for_offset(int dx, int dy, int *ox, int *oy, int *ow, int *oh) {
    int minx = 0x7fffffff, miny = 0x7fffffff, maxx = -0x7fffffff, maxy = -0x7fffffff, any = 0;
    for (int i = 0; i < g_desktop_item_count; i++) {
        if (!g_desktop_items[i].selected) continue;
        DesktopItem *it = &g_desktop_items[i];
        int gx = it->x + dx, gy = it->y + dy;
        int x1 = it->x < gx ? it->x : gx;
        int y1 = it->y < gy ? it->y : gy;
        int x2 = it->x + it->w; if (gx + it->w > x2) x2 = gx + it->w;
        int y2 = it->y + it->h; if (gy + it->h > y2) y2 = gy + it->h;
        if (x1 < minx) minx = x1;
        if (y1 < miny) miny = y1;
        if (x2 > maxx) maxx = x2;
        if (y2 > maxy) maxy = y2;
        any = 1;
    }
    if (!any) { *ox = *oy = *ow = *oh = 0; return; }
    *ox = minx; *oy = miny; *ow = maxx - minx; *oh = maxy - miny;
}

static void invalidate_trash_cell(void) {
    for (int i = 0; i < g_desktop_item_count; i++) {
        if (g_desktop_items[i].type != ITEM_TRASH) continue;
        DesktopItem *tr = &g_desktop_items[i];
        request_partial_redraw(tr->x - 2, tr->y - 2, tr->w + 4, tr->h + 4);
    }
}

/* Keep the dragged ghost inside the usable desktop: below the top bar (32px)
 * and above the Dock shelf. Without this an icon could be dragged over the
 * blue bar or under the Dock, which the drop logic then could not represent. */
static void drag_clamp_ghost(int *dx, int *dy) {
    int min_x = 0x7fffffff, min_y = 0x7fffffff, max_x = -0x7fffffff, max_y = -0x7fffffff;
    for (int i = 0; i < g_desktop_item_count; i++) {
        if (!g_desktop_items[i].selected) continue;
        DesktopItem *it = &g_desktop_items[i];
        if (it->x < min_x) min_x = it->x;
        if (it->y < min_y) min_y = it->y;
        if (it->x + it->w > max_x) max_x = it->x + it->w;
        if (it->y + it->h > max_y) max_y = it->y + it->h;
    }
    if (min_x > max_x) return;
    int top = 36;
    int bottom = shell.height - 145;
    if (min_y + *dy < top) *dy = top - min_y;
    if (max_y + *dy > bottom) *dy = bottom - max_y;
    if (min_x + *dx < 4) *dx = 4 - min_x;
    if (max_x + *dx > shell.width - 4) *dx = shell.width - 4 - max_x;
}

void desktop_items_drag_move(int mx, int my) {
    if (!g_drag_active) return;
    int old_dx = g_drag_cur_mx - g_drag_start_mx;
    int old_dy = g_drag_cur_my - g_drag_start_my;
    int was_moved = g_drag_moved;
    g_drag_cur_mx = mx;
    g_drag_cur_my = my;
    int dx = mx - g_drag_start_mx;
    int dy = my - g_drag_start_my;
    drag_clamp_ghost(&dx, &dy);
    if (!g_drag_moved && (dx <= -DRAG_THRESHOLD || dx >= DRAG_THRESHOLD ||
                          dy <= -DRAG_THRESHOLD || dy >= DRAG_THRESHOLD)) {
        g_drag_moved = 1;
    }
    if (g_drag_moved) {
        int prev_trash = g_trash_hovered;
        g_trash_hovered = desktop_items_trash_hit(mx, my);
        int ax, ay, aw, ah, bx, by, bw, bh;
        drag_bounds_for_offset(was_moved ? old_dx : 0, was_moved ? old_dy : 0, &ax, &ay, &aw, &ah);
        drag_bounds_for_offset(dx, dy, &bx, &by, &bw, &bh);
        int ux1 = ax < bx ? ax : bx;
        int uy1 = ay < by ? ay : by;
        int ux2 = (ax + aw) > (bx + bw) ? ax + aw : bx + bw;
        int uy2 = (ay + ah) > (by + bh) ? ay + ah : by + bh;
        if (ux2 > ux1 && uy2 > uy1)
            request_partial_redraw(ux1 - 4, uy1 - 4, (ux2 - ux1) + 8, (uy2 - uy1) + 8);
        if (prev_trash != g_trash_hovered) invalidate_trash_cell();
    }
}

void desktop_items_drag_end(int mx, int my) {
    if (!g_drag_active) return;
    (void)mx; (void)my;

    if (g_drag_moved) {
        if (g_trash_hovered) {
            int moved_any = 0;
            for (int i = 0; i < g_desktop_item_count; i++) {
                if (g_desktop_items[i].selected) {
                    DesktopItemType t = g_desktop_items[i].type;
                    if (t == ITEM_APP || t == ITEM_TRASH) continue;
                    trash_move_item(g_desktop_items[i].path);
                    moved_any = 1;
                }
            }
            if (moved_any) {
                desktop_items_scan();
                ui_notify("Trash", "Items moved to Trash", ICON_TRASH);
            }
        } else {
            int max_rows = (shell.height - 160) / GRID_CELL_H;
            if (max_rows < 1) max_rows = 1;
            int max_cols = (shell.width - 48) / GRID_CELL_W;
            if (max_cols < 1) max_cols = 1;

            if (g_drag_item >= 0 && g_drag_item < g_desktop_item_count) {
                DesktopItem *primary = &g_desktop_items[g_drag_item];
                int target_col = (primary->x + (g_drag_cur_mx - g_drag_start_mx) - GRID_START_X + GRID_CELL_W / 2) / GRID_CELL_W;
                int target_row = (primary->y + (g_drag_cur_my - g_drag_start_my) - GRID_START_Y + GRID_CELL_H / 2) / GRID_CELL_H;
                if (target_col < 0) target_col = 0;
                if (target_col >= max_cols) target_col = max_cols - 1;
                if (target_row < 0) target_row = 0;
                if (target_row >= max_rows) target_row = max_rows - 1;

                int delta_col = target_col - primary->col;
                int delta_row = target_row - primary->row;

                int target_cols[DESKTOP_MAX_ITEMS];
                int target_rows[DESKTOP_MAX_ITEMS];
                for (int i = 0; i < g_desktop_item_count; i++) {
                    target_cols[i] = -1;
                    target_rows[i] = -1;
                }

                /* Step 1: compute ideal relative target cells */
                for (int i = 0; i < g_desktop_item_count; i++) {
                    if (g_desktop_items[i].selected) {
                        int nc = g_desktop_items[i].col + delta_col;
                        int nr = g_desktop_items[i].row + delta_row;
                        if (nc < 0) nc = 0;
                        if (nc >= max_cols) nc = max_cols - 1;
                        if (nr < 0) nr = 0;
                        if (nr >= max_rows) nr = max_rows - 1;
                        target_cols[i] = nc;
                        target_rows[i] = nr;
                    }
                }

                /* Step 2: resolve collisions with unselected items and among selected items */
                for (int i = 0; i < g_desktop_item_count; i++) {
                    if (!g_desktop_items[i].selected) continue;
                    int nc = target_cols[i];
                    int nr = target_rows[i];

                    int collides = 0;
                    for (int k = 0; k < g_desktop_item_count; k++) {
                        if (k == i) continue;
                        if (!g_desktop_items[k].selected) {
                            if (g_desktop_items[k].col == nc && g_desktop_items[k].row == nr) {
                                collides = 1; break;
                            }
                        } else if (k < i) {
                            if (target_cols[k] == nc && target_rows[k] == nr) {
                                collides = 1; break;
                            }
                        }
                    }

                    if (collides) {
                        int found = 0;
                        for (int col = 0; col < max_cols && !found; col++) {
                            for (int row = 0; row < max_rows && !found; row++) {
                                int occ = 0;
                                for (int k = 0; k < g_desktop_item_count; k++) {
                                    if (k == i) continue;
                                    if (!g_desktop_items[k].selected) {
                                        if (g_desktop_items[k].col == col && g_desktop_items[k].row == row) {
                                            occ = 1; break;
                                        }
                                    } else if (k < i) {
                                        if (target_cols[k] == col && target_rows[k] == row) {
                                            occ = 1; break;
                                        }
                                    }
                                }
                                if (!occ) {
                                    nc = col;
                                    nr = row;
                                    found = 1;
                                }
                            }
                        }
                    }
                    target_cols[i] = nc;
                    target_rows[i] = nr;
                }

                /* Step 3: commit positions and update pixels */
                for (int i = 0; i < g_desktop_item_count; i++) {
                    if (g_desktop_items[i].selected) {
                        g_desktop_items[i].col = target_cols[i];
                        g_desktop_items[i].row = target_rows[i];
                        g_desktop_items[i].x = GRID_START_X + target_cols[i] * GRID_CELL_W;
                        g_desktop_items[i].y = GRID_START_Y + target_rows[i] * GRID_CELL_H;
                    }
                }
                save_layout();
                for (int k = 0; k < g_desktop_item_count; k++) {
                    char cbuf[12], rbuf[12], xbuf[12], ybuf[12];
                    number(cbuf, (u32)g_desktop_items[k].col);
                    number(rbuf, (u32)g_desktop_items[k].row);
                    number(xbuf, (u32)g_desktop_items[k].x);
                    number(ybuf, (u32)g_desktop_items[k].y);
                    serial("DESKTOP_ITEM: ");
                    serial(g_desktop_items[k].name);
                    serial(" col="); serial(cbuf);
                    serial(" row="); serial(rbuf);
                    serial(" x="); serial(xbuf);
                    serial(" y="); serial(ybuf);
                    serial("\n");
                }
            }
        }
    } else {
        if (!input_ctrl_held() && g_drag_item >= 0) {
            desktop_items_select_single(g_drag_item);
        }
    }

    g_drag_active = 0;
    g_drag_moved = 0;
    g_drag_item = -1;
    g_trash_hovered = 0;
    request_scene_redraw();
}

/* Marquee Selection */
void desktop_items_marquee_start(int mx, int my) {
    g_marquee_active = 1;
    g_marquee_start_x = mx;
    g_marquee_start_y = my;
    g_marquee_cur_x = mx;
    g_marquee_cur_y = my;
}

int desktop_items_marquee_is_active(void) {
    return g_marquee_active;
}

void desktop_items_marquee_get_rect(int *rx, int *ry, int *rw, int *rh) {
    int x1 = g_marquee_start_x < g_marquee_cur_x ? g_marquee_start_x : g_marquee_cur_x;
    int y1 = g_marquee_start_y < g_marquee_cur_y ? g_marquee_start_y : g_marquee_cur_y;
    int x2 = g_marquee_start_x > g_marquee_cur_x ? g_marquee_start_x : g_marquee_cur_x;
    int y2 = g_marquee_start_y > g_marquee_cur_y ? g_marquee_start_y : g_marquee_cur_y;
    if (rx) *rx = x1;
    if (ry) *ry = y1;
    if (rw) *rw = x2 - x1;
    if (rh) *rh = y2 - y1;
}

/* Marquee movement only changes pixels inside the old/new rubber band and the
 * selection rings of items whose state flipped. Repaint those bounds instead
 * of forcing a full-scene pass on every PS/2 packet. */
static void marquee_accumulate(int *x1, int *y1, int *x2, int *y2,
                               int ax, int ay, int aw, int ah) {
    if (aw <= 0 || ah <= 0) return;
    if (ax < *x1) *x1 = ax;
    if (ay < *y1) *y1 = ay;
    if (ax + aw > *x2) *x2 = ax + aw;
    if (ay + ah > *y2) *y2 = ay + ah;
}

void desktop_items_marquee_update(int mx, int my, int ctrl_held) {
    if (!g_marquee_active) return;
    int orx, ory, orw, orh;
    desktop_items_marquee_get_rect(&orx, &ory, &orw, &orh);
    g_marquee_cur_x = mx;
    g_marquee_cur_y = my;
    int rx, ry, rw, rh;
    desktop_items_marquee_get_rect(&rx, &ry, &rw, &rh);

    int ux1 = 0x7fffffff, uy1 = 0x7fffffff, ux2 = -0x7fffffff, uy2 = -0x7fffffff;
    marquee_accumulate(&ux1, &uy1, &ux2, &uy2, orx, ory, orw, orh);
    marquee_accumulate(&ux1, &uy1, &ux2, &uy2, rx, ry, rw, rh);

    for (int i = 0; i < g_desktop_item_count; i++) {
        DesktopItem *item = &g_desktop_items[i];
        int old_sel = item->selected;
        int hit = (rx < item->x + item->w && rx + rw > item->x &&
                   ry < item->y + item->h && ry + rh > item->y);
        if (ctrl_held) {
            if (hit) item->selected = 1;
        } else {
            item->selected = hit;
        }
        if (item->selected != old_sel)
            marquee_accumulate(&ux1, &uy1, &ux2, &uy2, item->x, item->y, item->w, item->h);
    }
    if (ux2 > ux1 && uy2 > uy1)
        request_partial_redraw(ux1 - 6, uy1 - 6, (ux2 - ux1) + 12, (uy2 - uy1) + 12);
}

void desktop_items_marquee_end(void) {
    if (!g_marquee_active) return;
    int rx, ry, rw, rh;
    desktop_items_marquee_get_rect(&rx, &ry, &rw, &rh);
    g_marquee_active = 0;
    if (rw > 0 && rh > 0) request_partial_redraw(rx - 6, ry - 6, rw + 12, rh + 12);
}

static void __attribute__((unused)) draw_desktop_folder(int x, int y, int s) {
    /* Soft ambient drop shadow */
    rounded(x + 2, y + s - 4, s - 4, 5, 3, 0x000000, 75);

    /* Back cover tab (cerulean azure) */
    int tab_w = s * 5 / 11;
    roundrect(x + 2, y + 4, tab_w, 10, 4, 0x1d4ed8);
    /* Back cover plate */
    roundrect(x + 2, y + 8, s - 4, s - 11, 5, 0x2563eb);

    /* Clean white document sheets peeking out of folder */
    roundrect(x + 6, y + 5, s - 12, 10, 3, 0xffffff);
    rect(x + 9, y + 7, s - 18, 2, 0x3b82f6);
    rect(x + 9, y + 10, s - 22, 1, 0x93c5fd);

    /* Front flap dark drop shadow */
    rect(x + 2, y + 13, s - 4, 2, 0x1e40af);

    /* Front flap with vibrant 3D gradient look */
    roundrect(x, y + 14, s, s - 16, 5, 0x3b82f6);
    /* Top luminous highlight rim */
    roundrect(x + 1, y + 14, s - 2, 2, 1, 0x93c5fd);
    /* Bottom subtle shadow curve */
    rect(x + 3, y + s - 3, s - 6, 1, 0x1d4ed8);
}

static void __attribute__((unused)) draw_desktop_text_file(int x, int y, int s) {
    int pw = s * 2 / 3;
    int ph = s - 5;
    int px = x + (s - pw) / 2;
    int py = y + 3;

    /* Drop shadow */
    rounded(px + 2, py + ph - 3, pw - 4, 5, 3, 0x000000, 60);

    /* Main document paper */
    roundrect(px, py, pw, ph, 4, 0xf8fafc);
    /* 1px crisp outline */
    rect(px + 4, py, pw - 8, 1, 0xcbd5e1);
    rect(px + 4, py + ph - 1, pw - 8, 1, 0xcbd5e1);
    rect(px, py + 4, 1, ph - 8, 0xcbd5e1);
    rect(px + pw - 1, py + 4, 1, ph - 8, 0xcbd5e1);

    /* Dog-ear fold top right */
    int fold_sz = 8;
    roundrect(px + pw - fold_sz, py, fold_sz, fold_sz, 2, 0xe2e8f0);
    rect(px + pw - fold_sz, py + fold_sz, fold_sz, 1, 0x94a3b8);

    /* Document header accent badge (vibrant indigo) */
    roundrect(px + 4, py + 4, pw - fold_sz - 5, 3, 1, 0x6366f1);

    /* Document preview text lines */
    rect(px + 4, py + 11, pw - 8, 2, 0x64748b);
    rect(px + 4, py + 16, pw - 10, 2, 0x94a3b8);
    rect(px + 4, py + 21, pw - 8, 2, 0x94a3b8);
    rect(px + 4, py + 26, pw - 12, 2, 0xcbd5e1);
    if (ph > 36) rect(px + 4, py + 31, pw - 9, 2, 0xcbd5e1);
}

static void __attribute__((unused)) draw_desktop_generic_file(int x, int y, int s) {
    int pw = s * 2 / 3;
    int ph = s - 5;
    int px = x + (s - pw) / 2;
    int py = y + 3;

    /* Drop shadow */
    rounded(px + 2, py + ph - 3, pw - 4, 5, 3, 0x000000, 60);

    /* Main document paper */
    roundrect(px, py, pw, ph, 4, 0xf1f5f9);
    rect(px + 4, py, pw - 8, 1, 0x94a3b8);
    rect(px + 4, py + ph - 1, pw - 8, 1, 0x94a3b8);
    rect(px, py + 4, 1, ph - 8, 0x94a3b8);
    rect(px + pw - 1, py + 4, 1, ph - 8, 0x94a3b8);

    /* Dog-ear fold top right */
    int fold_sz = 8;
    roundrect(px + pw - fold_sz, py, fold_sz, fold_sz, 2, 0xcbd5e1);
    rect(px + pw - fold_sz, py + fold_sz, fold_sz, 1, 0x64748b);

    /* Cyan header tag */
    roundrect(px + 4, py + 4, pw - fold_sz - 5, 3, 1, 0x06b6d4);

    /* File content lines */
    rect(px + 4, py + 11, pw - 8, 2, 0x0891b2);
    rect(px + 4, py + 16, pw - 10, 2, 0x64748b);
    rect(px + 4, py + 21, pw - 8, 2, 0x94a3b8);
    rect(px + 4, py + 26, pw - 12, 2, 0xcbd5e1);
}

static void __attribute__((unused)) draw_desktop_trash(int x, int y, int s, int has_items) {
    /* Shadow */
    rounded(x + 6, y + s - 4, s - 12, 5, 3, 0x000000, 70);

    /* Metallic top rim */
    u32 rim_col = has_items ? 0xa0aec0 : 0x94a3b8;
    roundrect(x + 5, y + 6, s - 10, 4, 2, rim_col);
    rect(x + 8, y + 7, s - 16, 1, 0xe2e8f0);

    /* Frosted acrylic wastebasket body */
    int bx = x + 7, by = y + 9, bw = s - 14, bh = s - 12;
    rounded(bx, by, bw, bh, 5, ui_is_dark() ? 0x182030 : 0x483e58, 200);
    rect(bx + 2, by + bh - 1, bw - 4, 1, 0x334155);

    /* Subtle vertical glass ribs */
    int rib_col = ui_is_dark() ? 0x243248 : 0x675a7c;
    rect(x + s / 2 - 6, by + 3, 2, bh - 6, rib_col);
    rect(x + s / 2 - 1, by + 3, 2, bh - 6, rib_col);
    rect(x + s / 2 + 4, by + 3, 2, bh - 6, rib_col);

    /* Glossy glass reflection */
    rect(bx + 2, by + 2, 1, bh - 5, 0x60a5fa);

    /* Bottom base */
    roundrect(bx + 1, by + bh - 2, bw - 2, 3, 1, 0x0f172a);

    if (has_items) {
        /* Colorful crumpled paper sheets inside */
        roundrect(x + 10, y + 8, 7, 7, 2, 0xffffff);
        roundrect(x + 17, y + 6, 8, 8, 2, 0x60a5fa);
        roundrect(x + 24, y + 9, 7, 7, 2, 0xfbbf24);
        roundrect(x + 13, y + 13, 14, 10, 3, 0xf1f5f9);
        /* Red notification dot badge */
        roundrect(x + s - 12, y + 3, 10, 10, 5, 0xef4444);
        rect(x + s - 8, y + 7, 2, 2, 0xffffff);
    }
}

static void __attribute__((unused)) draw_desktop_golden_folder(int x, int y, int s) {
    rounded(x + 2, y + s - 4, s - 4, 5, 3, 0x000000, 75);
    int tab_w = s * 5 / 11;
    roundrect(x + 2, y + 4, tab_w, 10, 4, 0xb45309);
    roundrect(x + 2, y + 8, s - 4, s - 11, 5, 0xd97706);
    roundrect(x + 6, y + 5, s - 12, 10, 3, 0xffffff);
    rect(x + 9, y + 7, s - 18, 2, 0xf59e0b);
    rect(x + 9, y + 10, s - 22, 1, 0xfde68a);
    rect(x + 2, y + 13, s - 4, 2, 0x92400e);
    roundrect(x, y + 14, s, s - 16, 5, 0xf59e0b);
    roundrect(x + 1, y + 14, s - 2, 2, 1, 0xfef3c7);
    rect(x + 3, y + s - 3, s - 6, 1, 0xb45309);
}

static void draw_item_icon(const DesktopItem *item, int x, int y, int size) {
    if (item->type == ITEM_DIR) {
        sprite(x, y, size, size, desktop_icons_index[0], desktop_icons_alpha[0], desktop_icons_palette[0], 72, 72);
    } else if (item->type == ITEM_APP && item->app_id == APP_FILES) {
        sprite(x, y, size, size, desktop_icons_index[3], desktop_icons_alpha[3], desktop_icons_palette[3], 72, 72);
    } else if (item->type == ITEM_TXT) {
        sprite(x, y, size, size, desktop_icons_index[1], desktop_icons_alpha[1], desktop_icons_palette[1], 72, 72);
    } else if (item->type == ITEM_FILE) {
        sprite(x, y, size, size, desktop_icons_index[1], desktop_icons_alpha[1], desktop_icons_palette[1], 72, 72);
    } else if (item->type == ITEM_TRASH) {
        sprite(x, y, size, size, desktop_icons_index[2], desktop_icons_alpha[2], desktop_icons_palette[2], 72, 72);
    } else if (item->type == ITEM_APP) {
        if (item->app_id >= 0 && item->app_id < APP_COUNT) {
            draw_app_vector_icon(item->app_id, x, y, size);
        }
    }
}

void desktop_items_draw(void) {
    ThemeColors *th = ui_theme();
    for (int i = 0; i < g_desktop_item_count; i++) {
        DesktopItem *item = &g_desktop_items[i];
        int is_dragged = (g_drag_active && g_drag_moved && item->selected);
        if (is_dragged) continue;

        /* Selection highlight: glassy accent tint, no hard edge line. */
        if (item->selected) {
            rounded(item->x + 4, item->y + 2, item->w - 8, item->h - 4, 12,
                    th->accent, 40);
            rounded(item->x + 4, item->y + 2, item->w - 8, item->h - 4, 12,
                    th->selection, 120);
        }

        /* Trash hover highlight */
        if (item->type == ITEM_TRASH && g_trash_hovered) {
            rounded(item->x + 6, item->y + 4, item->w - 12, item->h - 8, 12, 0xe74c3c, 120);
        }

        /* Draw Icon (48x48 centered horizontally) */
        int ico_sz = 48;
        int ix = item->x + (item->w - ico_sz) / 2;
        int iy = item->y + 6;
        draw_item_icon(item, ix, iy, ico_sz);

        /* Draw label below icon */
        int tx = item->x;
        int ty = item->y + 58;
        int w = item->w;

        int tw = text_width(item->name, 1);
        if (tw <= w - 4) {
            centered(tx + 1, ty + 1, w, item->name, 0x161320, 1);
            centered(tx, ty, w, item->name, 0xffffff, 1);
        } else {
            int split_pos = -1;
            for (int k = 0; item->name[k]; k++) {
                if (item->name[k] == ' ' || item->name[k] == '.') split_pos = k;
            }
            if (split_pos > 0 && split_pos <= 12) {
                char line1[32], line2[32];
                u32 c = 0;
                while (c < (u32)split_pos && c < 31) { line1[c] = item->name[c]; c++; }
                line1[c] = 0;
                u32 c2 = 0;
                int start2 = (item->name[split_pos] == ' ') ? split_pos + 1 : split_pos;
                while (item->name[start2] && c2 < 12) { line2[c2++] = item->name[start2++]; }
                if (item->name[start2]) { line2[c2++] = '.'; line2[c2++] = '.'; }
                line2[c2] = 0;

                centered(tx + 1, ty - 2 + 1, w, line1, 0x161320, 1);
                centered(tx, ty - 2, w, line1, 0xffffff, 1);
                centered(tx + 1, ty + 11 + 1, w, line2, 0x161320, 1);
                centered(tx, ty + 11, w, line2, 0xffffff, 1);
            } else {
                char trunc[20];
                u32 c = 0;
                while (item->name[c] && c < 10) { trunc[c] = item->name[c]; c++; }
                trunc[c++] = '.'; trunc[c++] = '.';
                trunc[c] = 0;
                centered(tx + 1, ty + 1, w, trunc, 0x161320, 1);
                centered(tx, ty, w, trunc, 0xffffff, 1);
            }
        }
    }

    /* Draw Drag Ghosts for all dragged items */
    if (g_drag_active && g_drag_moved) {
        int dx = g_drag_cur_mx - g_drag_start_mx;
        int dy = g_drag_cur_my - g_drag_start_my;
        for (int i = 0; i < g_desktop_item_count; i++) {
            DesktopItem *item = &g_desktop_items[i];
            if (item->selected) {
                int gx = item->x + dx + (item->w - 48) / 2;
                int gy = item->y + dy + 6;
                rounded(gx - 4, gy - 4, 56, 56, 12, th->selection, 160);
                draw_item_icon(item, gx, gy, 48);
            }
        }
    }

    /* Draw Marquee Selection Rectangle */
    if (g_marquee_active) {
        int rx, ry, rw, rh;
        desktop_items_marquee_get_rect(&rx, &ry, &rw, &rh);
        if (rw > 2 && rh > 2) {
            rounded(rx, ry, rw, rh, 4, th->selection, 60);
            rect(rx, ry, rw, 1, th->accent);
            rect(rx, ry + rh - 1, rw, 1, th->accent);
            rect(rx, ry, 1, rh, th->accent);
            rect(rx + rw - 1, ry, 1, rh, th->accent);
        }
    }
}
