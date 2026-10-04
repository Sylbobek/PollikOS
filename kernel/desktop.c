#include "desktop.h"
#include "shell_internal.h"
#include "graphics.h"
#include "ui.h"
#include "gui/apps.h"
#include "gui/app_host.h"
#include "klog.h"
#include "pmm.h"
#include "mem.h"
#include "input_dispatch.h"
#include "desktop_items.h"
#include "ui_animation.h"
#include "trash.h"
#include "vfs.h"
#include "hw.h"
#include "net/net_manager.h"
#include "auth.h"
#include "media.h"

static int desktop_ready;
static char g_wallpaper_choice[56];
static char g_wallpaper_names[8][56];
static int g_wallpaper_count;
static int g_wallpaper_scan_done;
static int g_wallpaper_failure_logged;
static int desktop_present(void);
enum { BAR_SYSTEM, BAR_APP, BAR_FILE, BAR_WINDOW, BAR_HELP, BAR_ITEM_COUNT };
static int g_bar_menu_open = -1;
static int g_bar_menu_hover = -1;
static int g_bar_only_dirty;
static int g_bar_clock_valid;
static int g_bar_network_state = -1;
static u32 g_bar_clock_poll_ms;
static char g_bar_date[8] = "--- --";
static char g_bar_time[6] = "--:--";
static int g_dock_motion_goal[APP_COUNT];
static int g_dock_motion_from[APP_COUNT];
static u32 g_dock_motion_start[APP_COUNT];
static int g_dock_motion_active[APP_COUNT];
static int g_dock_motion_initialized;
static int g_dock_motion_frozen;
static u32 g_dock_motion_freeze_ms;

static int dock_ease_out_back(int t) {
    if (t <= 0) return 0;
    if (t >= 256) return 256;
    u64 q = (u32)(256 - t);
    int cubic = (int)((q * q * q * 691u) / (256u * 65536u));
    int square = (int)((q * q * 435u) / (256u * 256u));
    return 256 - cubic + square;
}

ShellState shell = {
    .width = 1024, .height = 768, .dirty = 1, .scene_dirty = 1,
    .animations_mode = 1, .dock_zoom = 1, .drag_app = -1, .resizing = -1,
    .last_title_click_id = -1, .context_app = -1, .hover = -1
};
_Static_assert(APP_COUNT == NUM_APPS, "Registry and WM slots must agree");
void request_scene_redraw(void) { shell.dirty = 1; shell.scene_dirty = 1; }
void focus_app(int id) { wm_focus(id); request_scene_redraw(); }

static int g_dock_pinned[APP_COUNT];
static int g_dock_initialized = 0;
static int g_settings_initialized = 0;

static void desktop_scan_wallpapers(void) {
    if (g_wallpaper_scan_done) return;
    g_wallpaper_scan_done = 1;
    int fd = vfs_open("/usr/share/wallpapers", O_RDONLY);
    if (fd < 0) return;
    vfs_dirent_t entry;
    while (g_wallpaper_count < 8 && vfs_readdir(fd, &entry) > 0) {
        int n = 0;
        while (n < VFS_MAX_NAME && entry.name[n]) ++n;
        if (entry.type != VFS_FILE || n < 5 || n >= (int)sizeof(g_wallpaper_names[0]) ||
            entry.name[n-4] != '.') continue;
        char a = entry.name[n-3], b = entry.name[n-2], c = entry.name[n-1];
        if (!((a == 'p' || a == 'P') && (b == 'n' || b == 'N') &&
              (c == 'g' || c == 'G'))) continue;
        memcpy(g_wallpaper_names[g_wallpaper_count], entry.name, (u32)n);
        g_wallpaper_names[g_wallpaper_count][n] = 0;
        ++g_wallpaper_count;
    }
    vfs_close(fd);
}

int app_host_wallpaper_count(void) { desktop_scan_wallpapers(); return g_wallpaper_count; }
const char *app_host_wallpaper_name(int index) {
    desktop_scan_wallpapers();
    return index >= 0 && index < g_wallpaper_count ? g_wallpaper_names[index] : 0;
}
const char *app_host_selected_wallpaper(void) {
    if (g_wallpaper_choice[0]) return g_wallpaper_choice;
    return shell.theme ? "light.png" : "dark.png";
}
int app_host_set_wallpaper(int index) {
    const char *name = app_host_wallpaper_name(index);
    if (!name) return 0;
    int i = 0;
    while (name[i] && i < (int)sizeof(g_wallpaper_choice) - 1) {
        g_wallpaper_choice[i] = name[i];
        ++i;
    }
    if (name[i]) return 0;
    g_wallpaper_choice[i] = 0;
    app_host_save_settings();
    compositor_wallpaper_changed();
    return 1;
}
int app_host_pointer_acceleration(void) { return input_get_pointer_acceleration(); }
void app_host_set_pointer_acceleration(int enabled) {
    input_set_pointer_acceleration(enabled);
    app_host_save_settings();
}

void app_host_save_settings(void) {
    vfs_mkdir("/home");
    vfs_mkdir("/home/.config");
    int fd = vfs_open("/home/.config/appearance.conf", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    char buf[256];
    char num[16];
    int len = 0;

    #define WRITE_SETTING(k, v) do { \
        const char *ks = (k); \
        while (*ks) buf[len++] = *ks++; \
        number(num, (v)); \
        const char *ns = num; \
        while (*ns) buf[len++] = *ns++; \
        buf[len++] = '\n'; \
    } while(0)

    #define WRITE_TEXT_SETTING(k, v) do { \
        const char *ks = (k); while (*ks) buf[len++] = *ks++; \
        const char *vs = (v); while (*vs) buf[len++] = *vs++; \
        buf[len++] = '\n'; \
    } while(0)

    WRITE_SETTING("theme=", shell.theme);
    WRITE_SETTING("accent=", ui_get_accent_index());
    WRITE_SETTING("animations=", shell.animations_mode);
    WRITE_SETTING("dock_zoom=", shell.dock_zoom);
    WRITE_SETTING("target_fps=", wm_get_target_fps());
    WRITE_SETTING("sound_freq=", sound_get_freq());
    WRITE_SETTING("sound_muted=", sound_is_muted());
    if (g_wallpaper_choice[0]) WRITE_TEXT_SETTING("wallpaper=", g_wallpaper_choice);
    WRITE_SETTING("pointer_accel=", input_get_pointer_acceleration());

    #undef WRITE_SETTING
    #undef WRITE_TEXT_SETTING

    vfs_write(fd, buf, len);
    vfs_close(fd);
}

static void desktop_load_settings(void) {
    if (g_settings_initialized) return;
    g_settings_initialized = 1;

    int fd = vfs_open("/home/.config/appearance.conf", O_RDONLY);
    if (fd < 0) {
        app_host_save_settings();
        return;
    }
    char buf[512];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;
    buf[bytes] = 0;

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        if (*p) { *p = 0; p++; }
        while (*p == '\n' || *p == '\r') p++;

        if (memcmp(line, "theme=", 6) == 0) {
            int val = line[6] - '0';
            shell.theme = val;
            ui_set_theme_mode(val ? THEME_LIGHT : THEME_DARK);
        } else if (memcmp(line, "accent=", 7) == 0) {
            int val = line[7] - '0';
            ui_set_accent_index(val);
        } else if (memcmp(line, "animations=", 11) == 0) {
            int val = line[11] - '0';
            shell.animations_mode = val;
        } else if (memcmp(line, "dock_zoom=", 10) == 0) {
            int val = line[10] - '0';
            shell.dock_zoom = val;
        } else if (memcmp(line, "target_fps=", 11) == 0) {
            int val = 0;
            char *fp = line + 11;
            while (*fp >= '0' && *fp <= '9') { val = val * 10 + (*fp - '0'); fp++; }
            if (val >= 60) wm_set_target_fps(val);
        } else if (memcmp(line, "sound_freq=", 11) == 0) {
            u32 val = 0;
            char *fp = line + 11;
            while (*fp >= '0' && *fp <= '9') { val = val * 10 + (*fp - '0'); fp++; }
            if (val > 0) sound_set_freq(val);
        } else if (memcmp(line, "sound_muted=", 12) == 0) {
            int val = line[12] - '0';
            sound_set_muted(val);
        } else if (memcmp(line, "wallpaper=", 10) == 0) {
            int n = 0;
            while (line[10+n] && n < (int)sizeof(g_wallpaper_choice)-1) {
                char c = line[10+n];
                if (c == '/' || c == '\\' || c == ':' || c == '.') {
                    if (c == '.' && n > 0) { g_wallpaper_choice[n++] = c; continue; }
                    break;
                }
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-')) break;
                g_wallpaper_choice[n++] = c;
            }
            g_wallpaper_choice[n] = 0;
        } else if (memcmp(line, "pointer_accel=", 14) == 0) {
            input_set_pointer_acceleration(line[14] == '1');
        }
    }
}

static void dock_default_pins(void) {
    for (int i = 0; i < APP_COUNT; i++) g_dock_pinned[i] = 1;
}

static void dock_save_config(void) {
    vfs_mkdir("/home");
    vfs_mkdir("/home/.config");
    int fd = vfs_open("/home/.config/dock.conf", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    for (int i = 0; i < APP_COUNT; i++) {
        if (i == APP_FILES || g_dock_pinned[i]) {
            const char *name = gui_app_get(i)->name;
            int nlen = 0;
            while (name[nlen]) nlen++;
            vfs_write(fd, name, nlen);
            vfs_write(fd, "\n", 1);
        }
    }
    vfs_close(fd);
}

static void dock_load_config(void) {
    desktop_load_settings();
    dock_default_pins();
    int fd = vfs_open("/home/.config/dock.conf", O_RDONLY);
    if (fd < 0) {
        dock_save_config();
        g_dock_initialized = 1;
        return;
    }
    for (int i = 0; i < APP_COUNT; i++) g_dock_pinned[i] = 0;
    g_dock_pinned[APP_FILES] = 1; /* Safety guard */

    char buf[512];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes > 0) {
        buf[bytes] = 0;
        char line[64];
        int lp = 0;
        for (int i = 0; i <= bytes; i++) {
            if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == 0) {
                if (lp > 0) {
                    line[lp] = 0;
                    for (int a = 0; a < APP_COUNT; a++) {
                        const char *aname = gui_app_get(a)->name;
                        int eq = 1, k = 0;
                        while (aname[k] || line[k]) {
                            if (aname[k] != line[k]) { eq = 0; break; }
                            k++;
                        }
                        if (eq) { g_dock_pinned[a] = 1; break; }
                    }
                    lp = 0;
                }
            } else if (lp < 63) {
                line[lp++] = buf[i];
            }
        }
    }
    g_dock_pinned[APP_FILES] = 1; /* Safety guard: Files is always pinned */
    g_dock_initialized = 1;
}

int dock_is_pinned(int app_id) {
    if (app_id == APP_FILES) return 1;
    if (app_id < 0 || app_id >= APP_COUNT) return 0;
    if (!g_dock_initialized) dock_load_config();
    return g_dock_pinned[app_id];
}

void dock_set_pinned(int app_id, int pinned) {
    if (app_id == APP_FILES) return; /* Cannot unpin Files */
    if (app_id < 0 || app_id >= APP_COUNT) return;
    if (!g_dock_initialized) dock_load_config();
    g_dock_pinned[app_id] = pinned ? 1 : 0;
    dock_save_config();
    compositor_invalidate_dock();
}

int dock_get_visible_apps(int *out_apps, int max_apps) {
    if (!g_dock_initialized) dock_load_config();
    int count = 0;
    /* Files is the permanent leftmost item; the Dock renderer separates it
     * visually from the user's pins and running applications. */
    if (max_apps > 0 && APP_FILES < APP_COUNT) out_apps[count++] = APP_FILES;
    /* 2. All other pinned apps */
    for (int i = 0; i < APP_COUNT && count < max_apps; i++) {
        if (i != APP_FILES && g_dock_pinned[i]) {
            out_apps[count++] = i;
        }
    }
    /* 3. Unpinned running or launching apps */
    for (int i = 0; i < APP_COUNT && count < max_apps; i++) {
        if (i != APP_FILES && !g_dock_pinned[i]) {
            if (windows[i].open || ui_anim_dock_is_launching(i)) {
                out_apps[count++] = i;
            }
        }
    }
    return count;
}

static int dock_item_center(int count, int index) {
    int span = (count - 1) * 68 + (count > 1 ? DOCK_FILES_GAP : 0);
    return (shell.width - span) / 2 + index * 68 + (index > 0 ? DOCK_FILES_GAP : 0);
}

void dock_get_app_center(int id, int *cx, int *cy) {
    int visible[APP_COUNT];
    int count = dock_get_visible_apps(visible, APP_COUNT);
    if (cy) *cy = shell.height - 96 + 36;
    for (int i = 0; i < count; i++) {
        if (visible[i] == id) {
            if (cx) *cx = dock_item_center(count, i);
            return;
        }
    }
    if (cx) *cx = shell.width / 2;
}

void open_app(int id) {
    if (!gui_app_get(id) || !wm_get_window(id)) return;
    cancel_interaction(-1);
    Window *w = wm_get_window(id);
    if (w->open && w->minimized) {
        int cx, cy;
        dock_get_app_center(id, &cx, &cy);
        wm_unminimize(id);
        ui_anim_start_restore(id, cx, cy, w->x, w->y, w->width, w->height);
        focus_app(id);
        request_scene_redraw();
        return;
    }
    if (w->open) {
        focus_app(id);
        return;
    }
    ui_anim_dock_launch(id);
    wm_open(id);
    gui_app_resized(id, window_width(id), window_height(id));
    gui_app_opened(id);
    compositor_invalidate(id);
    ui_anim_start_open(id, w->x, w->y, w->width, w->height);
    request_scene_redraw();
}
void dock_activate_app(int id) {
    Window *w = wm_get_window(id);
    if (w && w->open && !w->minimized && active_app() == id) {
        minimize_app(id);
        return;
    }
    open_app(id);
}
void toggle_maximize(int id) {
    if (!wm_get_window(id)) return;
    cancel_interaction(id);
    wm_toggle_maximize(id);
    compositor_invalidate(id);
    gui_app_resized(id, window_width(id), window_height(id));
    request_scene_redraw();
}
void minimize_app(int id) {
    Window *w = wm_get_window(id);
    if (!w || !w->open || !w->visible || w->minimized) return;
    cancel_interaction(id);
    int cx, cy;
    dock_get_app_center(id, &cx, &cy);
    ui_anim_start_minimize(id, cx, cy);
    request_scene_redraw();
}
void close_app(int id) {
    Window *w = wm_get_window(id);
    if (!w) return;
    cancel_interaction(id);
    ui_anim_start_close(id);
    int was_open = w->open;
    wm_close(id);
    if (was_open) gui_app_closed(id);
    request_scene_redraw();
}
void app_host_open(int id) { open_app(id); }
void app_host_close(int id) { close_app(id); }
void app_host_invalidate(int id) { compositor_invalidate(id); request_scene_redraw(); }
void app_host_invalidate_partial(int id) {
    compositor_invalidate_animated(id);
    shell.dirty = 1;
    shell.partial = 1;
}
void app_host_invalidate_partial_region(int id, int x, int y, int w, int h) {
    compositor_invalidate_client_region(id, x, y, w, h);
    if (id >= 0 && id < APP_COUNT && windows[id].open && !windows[id].minimized) {
        shell.dirty = 1;
        shell.partial = 1;
    }
}
void request_partial_redraw(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    compositor_invalidate_region(x, y, w, h);
    shell.dirty = 1;
    shell.partial = 1;
}
int app_host_theme(void) { return shell.theme; }
void app_host_set_theme(int value) {
    if (shell.theme == value) return;
    shell.theme = value;
    ui_set_theme_mode(value ? THEME_LIGHT : THEME_DARK);
    compositor_invalidate_all_surfaces();
    wm_invalidate_all();
    compositor_invalidate_dock();
    request_scene_redraw();
    shell.dirty = 1;
    app_host_save_settings();
}
int app_host_accent(void) { return ui_get_accent_index(); }
void app_host_set_accent(int index) {
    ui_set_accent_index(index);
    compositor_invalidate_all_surfaces();
    wm_invalidate_all();
    compositor_invalidate_dock();
    request_scene_redraw();
    shell.dirty = 1;
    app_host_save_settings();
}
u32 app_host_accent_color(void) { return ui_theme()->accent; }
u32 app_host_accent_hover(void) { return ui_theme()->accent_hover; }
int app_host_animations(void) { return shell.animations_mode; }
void app_host_set_animations(int enabled) {
    shell.animations_mode = enabled;
    shell.dirty = 1;
    app_host_save_settings();
}
int app_host_dock_zoom(void) { return shell.dock_zoom; }
void app_host_set_dock_zoom(int enabled) {
    shell.dock_zoom = enabled;
    shell.dirty = 1;
    app_host_save_settings();
}
int app_host_target_fps(void) { return wm_get_target_fps(); }
void app_host_set_target_fps(int fps) {
    wm_set_target_fps(fps);
    app_host_save_settings();
}
int app_host_sound_muted(void) { return sound_is_muted(); }
void app_host_set_sound_muted(int muted) {
    sound_set_muted(muted);
    app_host_save_settings();
}
u32 app_host_sound_freq(void) { return sound_get_freq(); }
void app_host_set_sound_freq(u32 freq) {
    sound_set_freq(freq);
    app_host_save_settings();
}
void app_host_stop_minimize(void) { compositor_cancel_minimize(-1); request_scene_redraw(); }
void app_host_perf_summary(char *out, int capacity) { wm_perf_summary(out, capacity); }
u64 app_host_time_us(void) { return wm_time_us(); }
void app_host_metrics(AppPerfView *out) {
    if (!out) return;
    GuiPerfStats s;
    wm_perf_snapshot(&s);
    *out = (AppPerfView){1, s.frame_count, s.presents_sec, s.clock_resolution_us,
        s.paint_time_us, s.compose_time_us, s.present_time_us, s.total_time_us};
}
u32 app_host_free_bytes(void) { return pmm_get_free_memory(); }
void *app_host_alloc(u32 bytes) {
    if (!bytes || bytes > 0xfffff000u) return 0;
    return (void *)pmm_alloc_pages((bytes + 4095u) / 4096u);
}
void app_host_free(void *p, u32 bytes) {
    if (p && bytes && bytes <= 0xfffff000u)
        pmm_free_pages((uintptr_t)p, (bytes + 4095u) / 4096u);
}

static void on_rename_dialog_result(const char *new_name) {
    shell.dirty = 1;
    if (new_name && new_name[0]) {
        int sel = desktop_items_get_selected();
        if (sel >= 0) {
            desktop_items_rename(sel, new_name);
        }
    }
}

static void on_delete_dialog_result(int confirmed) {
    shell.dirty = 1;
    if (confirmed) {
        if (desktop_items_selected_count() > 0) {
            desktop_items_move_selected_to_trash();
            ui_notify("Trash", "Items moved to Trash", ICON_TRASH);
        } else if (shell.context_app == APP_FILES) {
            files_delete_selected();
            ui_notify("Trash", "Item moved to Trash", ICON_TRASH);
        }
    }
}

void desktop_prompt_rename_selected(void) {
    int sel = desktop_items_get_selected();
    if (sel >= 0) {
        const DesktopItem *it = desktop_items_get(sel);
        if (it && it->type != ITEM_APP && it->type != ITEM_TRASH) {
            ui_dialog_input("Rename", "Enter new name:", it->name, ICON_FILE, on_rename_dialog_result);
            shell.dirty = 1;
        }
    }
}

void desktop_prompt_delete_selected(void) {
    if (desktop_items_selected_count() > 0) {
        ui_dialog_confirm("Move to Trash?", "Are you sure you want to move\nthe selected items to Trash?",
            "Move to Trash", "Cancel", 1, ICON_TRASH, on_delete_dialog_result);
        shell.dirty = 1;
    } else if (shell.context_app == APP_FILES) {
        ui_dialog_confirm("Move to Trash?", "Are you sure you want to move\nthe selected file to Trash?",
            "Move to Trash", "Cancel", 1, ICON_TRASH, on_delete_dialog_result);
        shell.dirty = 1;
    }
}

static void on_menu_action(int action_id) {
    shell.dirty = 1;
    switch (action_id) {
        case ACTION_ABOUT_POLLIKOS:
            ui_dialog_confirm("About PollikOS",
                "PollikOS Desktop Edition\nx86 Native Compositor & Shell\nA desktop OS with Pollik UX design.",
                "OK", "Cancel", 0, ICON_APP, NULL);
            break;
        case ACTION_DISPLAY_SETTINGS:
            ui_toggle_theme_mode();
            ui_notify("Appearance", ui_is_dark() ? "Dark Theme enabled" : "Light Theme enabled", ICON_SETTINGS);
            break;
        case ACTION_DESKTOP_SETTINGS: open_app(APP_SETTINGS); break;
        case ACTION_NEW_FOLDER:
            desktop_items_create_folder();
            ui_notify("Desktop", "Created new folder", ICON_FOLDER);
            break;
        case ACTION_NEW_TEXT_FILE:
            desktop_items_create_text_file();
            ui_notify("Desktop", "Created new text file", ICON_TEXT);
            break;
        case ACTION_EMPTY_TRASH:
            trash_empty();
            desktop_items_scan();
            ui_notify("Trash", "Trash emptied", ICON_TRASH);
            break;
        case ACTION_OPEN: {
            int sel = desktop_items_get_selected();
            if (sel >= 0) {
                desktop_items_open(sel);
            } else if (shell.context_app == APP_FILES && files_selected_name()) {
                files_open_selected();
            } else if (shell.context_app >= 0) {
                open_app(shell.context_app);
            }
            break;
        }
        case ACTION_OPEN_WITH: ui_notify("Open With", "Applications: Notes, Browser", ICON_APP); break;
        case ACTION_RENAME: desktop_prompt_rename_selected(); break;
        case ACTION_COPY: ui_notify("Clipboard", "Copied to clipboard", ICON_FILE); break;
        case ACTION_CUT: ui_notify("Clipboard", "Cut to clipboard", ICON_FILE); break;
        case ACTION_DELETE: desktop_prompt_delete_selected(); break;
        case ACTION_PROPERTIES:
            ui_dialog_message("Properties", (shell.context_app == APP_FILES && files_selected_name()) ? files_selected_name() : "PollikOS Desktop Object", ICON_FILE, NULL);
            break;
        case ACTION_MINIMIZE: if (shell.context_app >= 0) minimize_app(shell.context_app); break;
        case ACTION_MAXIMIZE: if (shell.context_app >= 0) toggle_maximize(shell.context_app); break;
        case ACTION_CLOSE_WINDOW: if (shell.context_app >= 0) close_app(shell.context_app); break;
        case ACTION_DOCK_PIN:
            if (shell.context_app >= 0) {
                dock_set_pinned(shell.context_app, 1);
                ui_notify("Dock", "App pinned to Dock", ICON_APP);
            }
            break;
        case ACTION_DOCK_UNPIN:
            if (shell.context_app >= 0 && shell.context_app != APP_FILES) {
                dock_set_pinned(shell.context_app, 0);
                ui_notify("Dock", "App unpinned from Dock", ICON_APP);
            }
            break;
        case ACTION_OPEN_SETTINGS_APP: open_app(APP_SETTINGS); break;
        case ACTION_OPEN_FILES_APP: open_app(APP_FILES); break;
        default: break;
    }
}

static int desktop_bar_active_app(void) {
    int id = active_app();
    return id >= 0 && id < APP_COUNT && windows[id].open && !windows[id].minimized ? id : -1;
}

static const char *desktop_bar_app_name(void) {
    int id = desktop_bar_active_app();
    return id >= 0 ? gui_app_get(id)->name : "Desktop";
}

static int desktop_bar_item_rect(int item, int *out_x, int *out_w) {
    int x = 12, w = 24;
    if (item == BAR_SYSTEM) {
        w = text_width("Pollik OS", 1) + 24;
        x = (shell.width - w) / 2;
    } else if (item == BAR_APP) {
        w = text_width(desktop_bar_app_name(), 1) + 20;
    } else if (item >= BAR_FILE) {
        x = 12 + text_width(desktop_bar_app_name(), 1) + 28;
        if (item >= BAR_WINDOW) x += text_width("File", 1) + 24;
        if (item >= BAR_HELP) x += text_width("Window", 1) + 24;
        const char *label = item == BAR_FILE ? "File" : item == BAR_WINDOW ? "Window" : "Help";
        w = text_width(label, 1) + 24;
    }
    if (out_x) *out_x = x;
    if (out_w) *out_w = w;
    return 1;
}

static int desktop_bar_hit(int mx) {
    for (int i = 0; i < BAR_ITEM_COUNT; i++) {
        int x, w;
        desktop_bar_item_rect(i, &x, &w);
        if (mx >= x && mx < x + w) return i;
    }
    return -1;
}

static void desktop_bar_open_menu(int item) {
    UiMenuItem items[8];
    int count = 0, id = desktop_bar_active_app();
    shell.context_app = id;
    if (item == BAR_SYSTEM) {
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_ABOUT_POLLIKOS, "About PollikOS", NULL, ICON_INFO, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN_SETTINGS_APP, "System Settings...", NULL, ICON_SETTINGS, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_DISPLAY_SETTINGS, "Toggle Appearance", NULL, ICON_THEME, 1};
    } else if (item == BAR_APP) {
        if (id >= 0) {
            items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_MINIMIZE, "Minimize", "Alt+F9", ICON_APP, 1};
            items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_MAXIMIZE, "Zoom / Restore", "F11", ICON_APP, 1};
            items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
            items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_CLOSE_WINDOW, "Quit App", "Alt+F4", ICON_CLOSE, 1};
        } else {
            items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN_FILES_APP, "Open Files", NULL, ICON_FOLDER, 1};
            items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN_SETTINGS_APP, "System Settings...", NULL, ICON_SETTINGS, 1};
        }
    } else if (item == BAR_FILE) {
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN_FILES_APP, "Open Files", NULL, ICON_FOLDER, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_NEW_FOLDER, "New Folder on Desktop", NULL, ICON_FOLDER, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_NEW_TEXT_FILE, "New Text File on Desktop", NULL, ICON_TEXT, 1};
        if (id >= 0) {
            items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
            items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_CLOSE_WINDOW, "Close Window", "Alt+F4", ICON_CLOSE, 1};
        }
    } else if (item == BAR_WINDOW) {
        if (id >= 0) {
            items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_MINIMIZE, "Minimize", "Alt+F9", ICON_APP, 1};
            items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_MAXIMIZE, "Zoom / Restore", "F11", ICON_APP, 1};
            items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
            items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_CLOSE_WINDOW, "Close Window", "Alt+F4", ICON_CLOSE, 1};
        } else {
            items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN_FILES_APP, "Open Files", NULL, ICON_FOLDER, 1};
        }
    } else {
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_ABOUT_POLLIKOS, "About PollikOS", NULL, ICON_INFO, 1};
    }
    int x, w;
    desktop_bar_item_rect(item, &x, &w);
    (void)w;
    ui_menu_open(&g_active_menu, x, 29, items, count, on_menu_action);
    g_bar_menu_open = item;
    g_bar_menu_hover = -1;
    request_partial_redraw(0, 0, shell.width, 32);
}

int desktop_bar_handle_click(int mx, int my) {
    if (my >= 32) return 0;
    int target = desktop_bar_hit(mx);
    if (target >= 0 && target == g_bar_menu_open && g_active_menu.active) {
        ui_menu_close(&g_active_menu);
        g_bar_menu_open = -1;
        g_bar_menu_hover = -1;
        request_partial_redraw(0, 0, shell.width, 32);
    } else if (target >= 0) {
        desktop_bar_open_menu(target);
    } else {
        if (g_active_menu.active) ui_menu_close(&g_active_menu);
        g_bar_menu_open = -1;
        g_bar_menu_hover = -1;
        request_partial_redraw(0, 0, shell.width, 32);
    }
    return 1;
}

int desktop_bar_handle_pointer(int mx, int my) {
    if (g_active_dialog.active) return 0;
    if (!g_active_menu.active) { g_bar_menu_open = -1; g_bar_menu_hover = -1; }
    int target = my < 32 ? desktop_bar_hit(mx) : -1;
    if (g_active_menu.active && g_bar_menu_open >= 0 && target >= 0 && target != g_bar_menu_open) {
        desktop_bar_open_menu(target);
        return 1;
    }
    if (target >= 0 && g_active_menu.active) {
        if (g_bar_menu_hover != target) {
            g_bar_menu_hover = target;
            request_partial_redraw(0, 0, shell.width, 32);
            return 1;
        }
    } else if (target < 0) {
        g_bar_menu_hover = -1;
    }
    return 0;
}

static int desktop_bar_update_clock(u32 now_ms) {
    if (g_bar_clock_valid && now_ms - g_bar_clock_poll_ms < 1000u) return 0;
    g_bar_clock_poll_ms = now_ms;
    RtcTime t;
    rtc_get_time(&t);
    static const char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const char *month = t.month >= 1 && t.month <= 12 ? months[t.month - 1] : "---";
    int network_state = net_manager_is_connected();
    char date[8], time[6];
    date[0] = month[0]; date[1] = month[1]; date[2] = month[2]; date[3] = ' ';
    date[4] = (char)('0' + (t.day / 10) % 10); date[5] = (char)('0' + t.day % 10); date[6] = 0; date[7] = 0;
    time[0] = (char)('0' + (t.hour / 10) % 10); time[1] = (char)('0' + t.hour % 10); time[2] = ':';
    time[3] = (char)('0' + (t.minute / 10) % 10); time[4] = (char)('0' + t.minute % 10); time[5] = 0;
    int changed = !g_bar_clock_valid || memcmp(date, g_bar_date, sizeof(date)) ||
                  memcmp(time, g_bar_time, sizeof(time)) || network_state != g_bar_network_state;
    memcpy(g_bar_date, date, sizeof(date));
    memcpy(g_bar_time, time, sizeof(time));
    g_bar_network_state = network_state;
    g_bar_clock_valid = 1;
    return changed;
}

void open_dock_context_menu(int screen_x, int screen_y, int app_id) {
    if (app_id < 0 || app_id >= APP_COUNT) return;
    g_bar_menu_open = -1;
    g_bar_menu_hover = -1;
    shell.context_app = app_id;
    UiMenuItem items[8];
    int count = 0;

    items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN, "Open", NULL, ICON_APP, 1};

    if (app_id == APP_FILES) {
        /* Files is ALWAYS pinned: never show Unpin */
    } else if (dock_is_pinned(app_id)) {
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_DOCK_UNPIN, "Unpin from Dock", NULL, ICON_PIN, 1};
    } else {
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_DOCK_PIN, "Pin to Dock", NULL, ICON_PIN, 1};
    }

    if (windows[app_id].open) {
        items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_CLOSE_WINDOW, "Quit", NULL, ICON_CLOSE, 1};
    }

    items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
    items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN_SETTINGS_APP, "System Settings...", NULL, ICON_SETTINGS, 1};

    int menu_h = 12;
    for (int i = 0; i < count; ++i)
        menu_h += items[i].kind == MENU_ITEM_SEPARATOR ? 9 : 26;
    int my = screen_y - menu_h;
    if (my < 32) my = 32;

    ui_menu_open(&g_active_menu, screen_x, my, items, count, on_menu_action);
    shell.dirty = 1;
    serial("DOCK MENU opened\n");
}

void open_context_menu(int screen_x, int screen_y, int app_id, int file_slot) {
    g_bar_menu_open = -1;
    g_bar_menu_hover = -1;
    UiMenuItem items[12];
    int count = 0;
    if (app_id == CONTEXT_DESKTOP_ITEM) {
        int idx = file_slot;
        const DesktopItem *it = desktop_items_get(idx);
        if (it) {
            IconKind open_ico = it->type == ITEM_DIR ? ICON_FOLDER :
                               (it->type == ITEM_TXT ? ICON_TEXT :
                               (it->type == ITEM_APP ? ICON_APP :
                               (it->type == ITEM_TRASH ? ICON_TRASH : ICON_FILE)));
            items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN, "Open", "Enter", open_ico, 1};
            if (it->type == ITEM_APP) {
                shell.context_app = it->app_id;
                if (dock_is_pinned(it->app_id)) {
                    if (it->app_id != APP_FILES) {
                        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_DOCK_UNPIN, "Unpin from Dock", NULL, ICON_PIN, 1};
                    }
                } else {
                    items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_DOCK_PIN, "Pin to Dock", NULL, ICON_PIN, 1};
                }
            } else if (it->type == ITEM_TRASH) {
                items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
                items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_EMPTY_TRASH, "Empty Trash", NULL, ICON_TRASH, 1};
            } else {
                items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_RENAME, "Rename", "F2", ICON_RENAME, 1};
                items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
                items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_DELETE, "Move to Trash", "Del", ICON_TRASH, 1};
            }
        }
    } else if (app_id == APP_FILES && file_slot >= 0 && files_selected_name()) {
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN, "Open", "Enter", ICON_FILE, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN_WITH, "Open With...", NULL, ICON_APP, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_RENAME, "Rename", "F2", ICON_RENAME, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_COPY, "Copy", "Ctrl+C", ICON_COPY, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_CUT, "Cut", "Ctrl+X", ICON_CUT, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_DELETE, "Delete", "Del", ICON_TRASH, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_PROPERTIES, "Properties", "Alt+Enter", ICON_INFO, 1};
    } else if (app_id >= 0) {
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_MINIMIZE, "Minimize", "Alt+F9", ICON_APP, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_MAXIMIZE, "Maximize / Restore", "F11", ICON_APP, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_CLOSE_WINDOW, "Close Window", "Alt+F4", ICON_CLOSE, 1};
    } else {
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_NEW_FOLDER, "New Folder", "Ctrl+Shift+N", ICON_FOLDER, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_NEW_TEXT_FILE, "New Text File", NULL, ICON_TEXT, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_DISPLAY_SETTINGS, "Toggle Dark/Light Mode", NULL, ICON_THEME, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN_SETTINGS_APP, "System Settings...", NULL, ICON_SETTINGS, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_ABOUT_POLLIKOS, "About PollikOS...", NULL, ICON_INFO, 1};
    }
    ui_menu_open(&g_active_menu, screen_x, screen_y, items, count, on_menu_action);
    shell.dirty = 1;
    serial("MENU opened\n");
}

int dock_is_visible(void) {
    return 1;
}

int dock_hit(void) {
    if (!dock_is_visible()) return -1;
    int visible[APP_COUNT];
    int count = dock_get_visible_apps(visible, APP_COUNT);
    if (count <= 0) return -1;

    int mx = input_pointer_x(), my = input_pointer_y();
    if (my < shell.height - 114 || my >= shell.height - 10) return -1;
    for (int i = 0; i < count; i++) {
        int cx = dock_item_center(count, i);
        if (mx >= cx - 32 && mx < cx + 32) return visible[i];
    }
    return -1;
}

static void draw_icon_files(int x, int y, int s) {
    rounded(x + 2, y + s - 4, s - 4, 5, 3, 0x000000, 75);
    int tab_w = s * 5 / 11;
    roundrect(x + 2, y + 4, tab_w, 10, 4, 0x1d4ed8);
    roundrect(x + 2, y + 8, s - 4, s - 11, 5, 0x2563eb);
    roundrect(x + 6, y + 5, s - 12, 10, 3, 0xffffff);
    rect(x + 9, y + 7, s - 18, 2, 0x3b82f6);
    rect(x + 9, y + 10, s - 22, 1, 0x93c5fd);
    rect(x + 2, y + 13, s - 4, 2, 0x1e40af);
    roundrect(x, y + 14, s, s - 16, 5, 0x3b82f6);
    roundrect(x + 1, y + 14, s - 2, 2, 1, 0x93c5fd);
    rect(x + 3, y + s - 3, s - 6, 1, 0x1d4ed8);
}

static void draw_icon_terminal(int x, int y, int s) {
    int r = s / 5;
    rounded(x + 2, y + s - 3, s - 4, 5, 3, 0x000000, 70);
    roundrect(x, y, s, s, r, 0x0f172a);
    roundrect(x + 1, y + 1, s - 2, 2, 1, 0x334155);

    int dot_y = y + s * 7 / 48;
    int dot_sz = s / 12;
    if (dot_sz < 3) dot_sz = 3;
    roundrect(x + s * 7 / 48, dot_y, dot_sz, dot_sz, 1, 0xef4444);
    roundrect(x + s * 13 / 48, dot_y, dot_sz, dot_sz, 1, 0xf59e0b);
    roundrect(x + s * 19 / 48, dot_y, dot_sz, dot_sz, 1, 0x10b981);

    int scr_x = x + s / 10;
    int scr_y = y + s * 14 / 48;
    int scr_w = s - s / 5;
    int scr_h = s - s * 18 / 48;
    roundrect(scr_x, scr_y, scr_w, scr_h, 3, 0x020617);

    int px = scr_x + 5;
    int py = scr_y + scr_h / 2 - 4;
    for (int d = 0; d < 5; d++) {
        rect(px + d, py + d, 2, 1, 0x22c55e);
        rect(px + d, py + 8 - d, 2, 1, 0x22c55e);
    }
    rect(px + 8, py + 7, s / 6, 2, 0x4ade80);
}

static void draw_icon_notes(int x, int y, int s) {
    int r = s / 5;
    rounded(x + 2, y + s - 3, s - 4, 5, 3, 0x000000, 70);
    roundrect(x, y, s, s, r, 0xf59e0b);
    roundrect(x, y, s, s * 11 / 48, r, 0xd97706);
    roundrect(x + 1, y + 1, s - 2, 2, 1, 0xfde68a);

    int px = x + s * 4 / 48;
    int py = y + s * 12 / 48;
    int pw = s - s * 8 / 48;
    int ph = s - s * 16 / 48;
    roundrect(px, py, pw, ph, 3, 0xffffff);

    int line_spacing = s / 7;
    int line_y = py + line_spacing;
    while (line_y < py + ph - 4) {
        rect(px + 5, line_y, pw - 10, 1, 0x93c5fd);
        line_y += line_spacing;
    }
    rect(px + 9, py + 2, 1, ph - 4, 0xfca5a5);

    int pen_x = px + pw - s * 14 / 48;
    int pen_y = py + ph - s * 14 / 48;
    roundrect(pen_x, pen_y, s * 12 / 48, 4, 2, 0x4f46e5);
    rect(pen_x - 2, pen_y + 1, 2, 2, 0xfbbf24);
}

static void draw_icon_settings(int x, int y, int s) {
    int r = s / 5;
    rounded(x + 2, y + s - 3, s - 4, 5, 3, 0x000000, 70);
    roundrect(x, y, s, s, r, 0x1e293b);
    roundrect(x + 1, y + 1, s - 2, 2, 1, 0x475569);

    int cx = x + s / 2;
    int cy = y + s / 2;
    int gr = s * 14 / 48;

    int tw = s * 7 / 48, th = s * 4 / 48;
    roundrect(cx - tw / 2, cy - gr - th + 1, tw, th + 2, 1, 0xcbd5e1);
    roundrect(cx - tw / 2, cy + gr - 1, tw, th + 2, 1, 0x94a3b8);
    roundrect(cx - gr - th + 1, cy - tw / 2, th + 2, tw, 1, 0x94a3b8);
    roundrect(cx + gr - 1, cy - tw / 2, th + 2, tw, 1, 0xcbd5e1);

    int d_off = gr * 7 / 10;
    roundrect(cx - d_off - 2, cy - d_off - 2, 5, 5, 1, 0xb0bac9);
    roundrect(cx + d_off - 3, cy - d_off - 2, 5, 5, 1, 0xb0bac9);
    roundrect(cx - d_off - 2, cy + d_off - 3, 5, 5, 1, 0x94a3b8);
    roundrect(cx + d_off - 3, cy + d_off - 3, 5, 5, 1, 0x94a3b8);

    roundrect(cx - gr, cy - gr, gr * 2, gr * 2, gr, 0x94a3b8);
    roundrect(cx - gr + 1, cy - gr + 1, (gr - 1) * 2, (gr - 1) * 2, gr - 1, 0xcbd5e1);

    int ir = s * 6 / 48;
    roundrect(cx - ir, cy - ir, ir * 2, ir * 2, ir, 0x1e293b);

    int pr = s * 3 / 48;
    if (pr < 2) pr = 2;
    roundrect(cx - pr, cy - pr, pr * 2, pr * 2, pr, 0x38bdf8);
}

static void draw_icon_web(int x, int y, int s) {
    int r = s / 5;
    rounded(x + 2, y + s - 3, s - 4, 5, 3, 0x000000, 70);
    roundrect(x, y, s, s, r, 0x1d4ed8);
    roundrect(x + 1, y + 1, s - 2, 2, 1, 0x60a5fa);

    int cx = x + s / 2;
    int cy = y + s / 2;
    int gr = s * 14 / 48;

    roundrect(cx - gr, cy - gr, gr * 2, gr * 2, gr, 0x0284c7);
    roundrect(cx - gr + 1, cy - gr + 1, (gr - 1) * 2, (gr - 1) * 2, gr - 1, 0x0ea5e9);

    int mw = gr * 8 / 10;
    roundrect(cx - mw / 2, cy - gr + 2, mw, (gr - 2) * 2, mw / 2, 0x38bdf8);
    roundrect(cx - mw / 2 + 2, cy - gr + 3, mw - 4, (gr - 3) * 2, (mw - 4) / 2, 0x0ea5e9);

    rect(cx, cy - gr + 2, 1, (gr - 2) * 2, 0xe0f2fe);
    rect(cx - gr + 2, cy, (gr - 2) * 2, 2, 0xe0f2fe);

    int lat_y1 = cy - gr / 2;
    int lat_w1 = gr * 3 / 2;
    rect(cx - lat_w1 / 2, lat_y1, lat_w1, 1, 0xbae6fd);
    int lat_y2 = cy + gr / 2;
    rect(cx - lat_w1 / 2, lat_y2, lat_w1, 1, 0xbae6fd);
}

static void draw_icon_pollikmark(int x, int y, int s) {
    int r = s / 5;
    rounded(x + 2, y + s - 3, s - 4, 5, 3, 0x000000, 70);
    roundrect(x, y, s, s, r, 0x6366f1);
    roundrect(x + 1, y + 1, s - 2, 2, 1, 0xa5b4fc);

    int cx = x + s / 2;
    int cy = y + s / 2;
    int ch = s * 11 / 48;

    for (int row = 0; row <= ch; row++) {
        int half_w = row * 16 / 11;
        rect(cx - half_w, cy - ch + row, half_w * 2 + 1, 1, 0x38bdf8);
    }
    for (int col = 0; col <= ch * 16 / 11; col++) {
        int top_y = cy - ch + (col * 11 / 16);
        rect(cx - col, top_y + ch, 1, ch, 0x1e40af);
    }
    for (int col = 0; col <= ch * 16 / 11; col++) {
        int top_y = cy - ch + (col * 11 / 16);
        rect(cx + col, top_y + ch, 1, ch, 0xa855f7);
    }
    rect(cx - 1, cy - ch - 1, 3, 3, 0xffffff);
}

static void draw_icon_welcome(int x, int y, int s) {
    int r = s / 5;
    rounded(x + 2, y + s - 3, s - 4, 5, 3, 0x000000, 70);
    roundrect(x, y, s, s, r, 0x4338ca);
    roundrect(x + 1, y + 1, s - 2, 2, 1, 0x818cf8);

    int cx = x + s / 2;
    int cy = y + s / 2;
    int cr = s * 14 / 48;

    roundrect(cx - cr, cy - cr, cr * 2, cr * 2, cr, 0x6366f1);
    roundrect(cx - cr + 2, cy - cr + 2, (cr - 2) * 2, (cr - 2) * 2, cr - 2, 0x312e81);

    int nw = s * 4 / 48;
    if (nw < 2) nw = 2;
    for (int d = 0; d < cr - 4; d++) {
        int cur_w = (d * nw) / (cr - 4);
        rect(cx - cur_w, cy - d, cur_w * 2 + 1, 1, 0xef4444);
    }
    for (int d = 0; d < cr - 4; d++) {
        int cur_w = (d * nw) / (cr - 4);
        rect(cx - cur_w, cy + d, cur_w * 2 + 1, 1, 0xf1f5f9);
    }
    roundrect(cx - 3, cy - 3, 6, 6, 3, 0xfbbf24);
}

void draw_app_vector_icon(int id, int x, int y, int size) {
    const GuiApp *app = gui_app_get(id);
    if (app && app->icon.indices && id != APP_POLLIKMARK) {
        sprite(x, y, size, size, app->icon.indices, app->icon.alpha, app->icon.palette, app->icon.width, app->icon.height);
        return;
    }
    switch (id) {
        case APP_FILES:      draw_icon_files(x, y, size); break;
        case APP_TERMINAL:   draw_icon_terminal(x, y, size); break;
        case APP_NOTES:      draw_icon_notes(x, y, size); break;
        case APP_SETTINGS:   draw_icon_settings(x, y, size); break;
        case APP_BROWSER:    draw_icon_web(x, y, size); break;
        case APP_POLLIKMARK: draw_icon_pollikmark(x, y, size); break;
        case APP_WELCOME:    draw_icon_welcome(x, y, size); break;
        default: {
            if (app && app->icon.indices) {
                sprite(x, y, size, size, app->icon.indices, app->icon.alpha, app->icon.palette, app->icon.width, app->icon.height);
            }
            break;
        }
    }
}

static void app_icon(int id, int x, int y, int size) {
    draw_app_vector_icon(id, x, y, size);
}

void dock_draw_pill(void) {
    int visible[APP_COUNT];
    int count = dock_get_visible_apps(visible, APP_COUNT);
    if (count <= 0) return;
    int width = count * 68 + 48 + (count > 1 ? DOCK_FILES_GAP : 0);
    int x = (shell.width - width) / 2, y = shell.height - 96;
    int radius = 29;
    if (ui_is_dark()) {
        rounded(x - 3, y + 5, width + 6, 88, 29, 0x000000, 46);
    } else {
        rounded(x - 3, y + 5, width + 6, 88, 29, 0x514566, 28);
    }
    rounded(x, y, width, 84, radius, ui_theme()->surface, 112);
}

void dock_draw_content(void) {
    int visible[APP_COUNT];
    int count = dock_get_visible_apps(visible, APP_COUNT);
    if (count <= 0) return;
    int y = shell.height - 96, top = active_app();
    u32 now_ms = wm_time_ms();
    int is_dark = ui_is_dark();
    for (int k = 0; k < count; k++) {
        int id = visible[k];
        int level = shell.hover_level[id], size = 56 + level / 16;
        int cx = dock_item_center(count, k);
        if (k == 1) {
            /* Files is a permanent, leftmost Dock item; separate it from the
             * user's pinned and currently running applications. */
            rect(cx - (68 + DOCK_FILES_GAP) / 2, y + 22, 1, 38,
                 is_dark ? 0x3b455a : 0xd3ccdf);
        }
        int bounce_dy = ui_anim_dock_bounce_offset(id, now_ms);
        if (bounce_dy < 0) size -= bounce_dy;
        int iy = y + 12 - level / 32 + bounce_dy;
        app_icon(id, cx - size / 2, iy, size);
        if (windows[id].open && !compositor_minimizing(id)) {
            if (id == top) roundrect(cx - 5, y + 74, 10, 4, 2, ui_theme()->accent);
            else roundrect(cx - 3, y + 74, 6, 4, 2, is_dark ? 0x475569 : 0xb0a5c0);
        } else if (ui_anim_dock_is_launching(id)) {
            roundrect(cx - 3, y + 74, 6, 4, 2, ui_theme()->accent);
        }
    }
    if (shell.hover >= 0 && shell.hover_level[shell.hover] > 160) {
        int h_idx = -1;
        for (int k = 0; k < count; k++) {
            if (visible[k] == shell.hover) { h_idx = k; break; }
        }
        if (h_idx >= 0) {
            const char *name = gui_app_get(shell.hover)->name;
            int cx = dock_item_center(count, h_idx), w = text_width(name, 1) + 24;
            u32 pill_bg = is_dark ? 0x161a26 : 0xf7f4fd;
            u32 pill_txt = is_dark ? 0xf1f5f9 : 0x63536f;
            roundrect(cx - w / 2, y - 37, w, 26, 11, pill_bg);
            centered(cx - w / 2, y - 33, w, name, pill_txt, 1);
        }
    }
}
void desktop_draw_bar(void) {
    desktop_bar_update_clock(wm_time_ms());
    if (!g_active_menu.active) g_bar_menu_open = -1;
    ThemeColors *theme = ui_theme();
    u32 bar_text = theme->text;
    const char *labels[BAR_ITEM_COUNT] = {NULL, desktop_bar_app_name(), "File", "Window", "Help"};
    for (int i = 0; i < BAR_ITEM_COUNT; i++) {
        int x, w;
        desktop_bar_item_rect(i, &x, &w);
        if (i == BAR_SYSTEM) {
            if (g_bar_menu_open == i)
                rounded(x, 4, w, 24, 6, theme->selection, 180);
            centered(x, 7, w, "Pollik OS", bar_text, 1);
            continue;
        }
        if (g_bar_menu_open == i)
            rounded(x, 4, w, 24, 6, theme->selection, 180);
        text(x + 10, 7, labels[i], bar_text, 1);
    }
    /* Date and clock only: the connection dot and Online/Offline label were
     * removed per user request, leaving the status strip uncluttered. */
    int date_w = text_width(g_bar_date, 1), time_w = text_width(g_bar_time, 1);
    int status_w = date_w + time_w + 34;
    int status_x = shell.width - status_w - 16;
    text(status_x, 7, g_bar_date, theme->text_secondary, 1);
    text(status_x + date_w + 14, 7, g_bar_time, bar_text, 1);
    rect(0, 31, shell.width, 1, theme->border_subtle);
    g_bar_only_dirty = 0;
}
void desktop_draw_overlays(void) {
    if (!shell.alttab_open) return;
    int open_apps[NUM_APPS], count = 0;
    for (int z = NUM_APPS - 1; z >= 0; z--) {
        int id = z_order[z];
        if (windows[id].open) open_apps[count++] = id;
    }
    if (!count) return;
    int card_w = 64;
    for (int i = 0; i < count; i++) {
        int label_w = text_width(gui_app_get(open_apps[i])->name, 1) + 12;
        if (label_w > card_w) card_w = label_w;
    }
    int gap = 12, total_w = count * card_w + (count - 1) * gap + 32;
    if (total_w < 200) total_w = 200;
    int total_h = 100, box_x = (shell.width - total_w) / 2, box_y = (shell.height - total_h) / 2;
    rounded(box_x, box_y, total_w, total_h, 16, 0x1c1926, 220);
    rounded(box_x + 1, box_y + 1, total_w - 2, total_h - 2, 15, 0x2e293c, 160);
    int start_x = box_x + (total_w - (count * card_w + (count - 1) * gap)) / 2;
    for (int i = 0; i < count; i++) {
        int id = open_apps[i], ix = start_x + i * (card_w + gap), iy = box_y + 16;
        int is_sel = id == shell.alttab_selected;
        if (is_sel) {
            rounded(ix - 4, iy - 4, card_w + 8, 48 + 8, 12, ui_theme()->accent, 200);
            rect(ix - 3, iy + 48, card_w + 6, 2, 0xffffff);
        }
        app_icon(id, ix + (card_w - 44) / 2, iy + 2, 44);
        centered(ix, iy + 56, card_w, gui_app_get(id)->name, is_sel ? 0xffffff : 0xc8c0d8, 1);
    }
}

static u8 *desktop_decode_theme_wallpaper(int dark, int *width, int *height) {
    (void)dark;
    desktop_scan_wallpapers();
    const char *name = app_host_selected_wallpaper();
    char path[VFS_MAX_PATH];
    const char *prefix = "/usr/share/wallpapers/";
    u32 length = 0;
    while (prefix[length]) { path[length] = prefix[length]; ++length; }
    u32 n = 0;
    while (name[n] && length + n + 1 < sizeof(path)) { path[length+n] = name[n]; ++n; }
    if (!name[n] || length + n >= sizeof(path)) return 0;
    path[length+n] = 0;
    vfs_stat_t st;
    if (!vfs_is_ready() || vfs_stat(path, &st) != VFS_OK || st.type != VFS_FILE ||
        !st.size || st.size > 8u * 1024u * 1024u) return 0;
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return 0;
    u8 *encoded = (u8 *)kmalloc(st.size);
    if (!encoded) { vfs_close(fd); return 0; }
    int got = vfs_read(fd, encoded, st.size);
    vfs_close(fd);
    if (got != (int)st.size) { kfree(encoded); return 0; }
    u8 *decoded = media_decode(encoded, st.size, width, height);
    kfree(encoded);
    if (!decoded || *width <= 0 || *height <= 0 ||
        (u64)(u32)*width * (u32)*height > 16u * 1024u * 1024u) {
        if (decoded) media_free(decoded);
        return 0;
    }
    return decoded;
}

static u32 desktop_theme_wallpaper_pixel(const u8 *rgba, int image_width,
                                        int sx, int sy, int screen_y, int dark) {
    const u8 *pixel = rgba + ((sy * image_width + sx) * 4);
    u32 color = ((u32)pixel[0] << 16) | ((u32)pixel[1] << 8) | pixel[2];
    if (pixel[3] < 255)
        color = blend(dark ? 0x080a11 : 0xf5f3f8, color, (pixel[3] * 256 + 127) / 255);
    if (screen_y < 32)
        color = blend(color, dark ? 0x090b12 : 0xf7f7ff, dark ? 215 : 208);
    if (screen_y == 31)
        color = blend(color, dark ? 0x1c2130 : 0xdad4e4, 180);
    return color;
}

static void desktop_paint_theme_wallpaper(u32 *destination, const u8 *rgba,
                                          int image_width, int image_height,
                                          int opacity, int dark) {
    int width = shell.width, height = shell.height;
    if (!destination || !rgba || image_width <= 0 || image_height <= 0 || width <= 0 || height <= 0)
        return;
    int visible_width = image_width, visible_height = image_height;
    int crop_x = 0, crop_y = 0;
    if (image_width * height > image_height * width) {
        visible_width = image_height * width / height;
        crop_x = (image_width - visible_width) / 2;
    } else {
        visible_height = image_width * height / width;
        crop_y = (image_height - visible_height) / 2;
    }
    if (visible_width < 1) visible_width = 1;
    if (visible_height < 1) visible_height = 1;
    int step_x = (visible_width << 16) / width;
    int step_y = (visible_height << 16) / height;
    int source_y_q16 = 0;
    for (int y = 0; y < height; y++) {
        int sy = crop_y + (source_y_q16 >> 16);
        if (sy >= image_height) sy = image_height - 1;
        int source_x_q16 = 0;
        u32 *row = destination + y * width;
        for (int x = 0; x < width; x++) {
            int sx = crop_x + (source_x_q16 >> 16);
            if (sx >= image_width) sx = image_width - 1;
            u32 color = desktop_theme_wallpaper_pixel(rgba, image_width, sx, sy, y, dark);
            row[x] = opacity >= 256 ? color : blend(row[x], color, opacity);
            source_x_q16 += step_x;
        }
        source_y_q16 += step_y;
    }
}

static void desktop_paint_procedural_wallpaper(u32 *wallpaper, int width, int height, int dark) {
    if (dark) {
        int aura_cx = width / 2, aura_cy = height * 42 / 100;
        int max_r = width * 42 / 100, max_r2 = max_r * max_r;
        for (int y = 0; y < height; y++) {
            int t = y * 256 / height;
            u32 base = blend(0x0e111a, 0x07090f, t);
            int dy = y - aura_cy, dy2 = dy * dy * 3;
            for (int x = 0; x < width; x++) {
                int dx = x - aura_cx, dist2 = dx * dx + dy2;
                u32 c = base;
                if (dist2 < max_r2) {
                    int factor = 255 - (dist2 * 255 / max_r2);
                    c = blend(base, 0x1a2336, factor * 160 / 255);
                }
                wallpaper[y * width + x] = c;
            }
        }
    } else {
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
            int t = y * 256 / height;
            u32 c = blend(0xb8b1ef, 0x49438e, t);
            int edge = height / 5 + (x - width / 2) * (x - width / 2) / 1900 - x / 5;
            if (y > edge) {
                u32 band = blend(0x9a88d7, 0x423c89, t);
                c = blend(c, band, y - edge < 3 ? (y - edge) * 85 : 256);
            }
            edge = height / 2 + (x - width) * (x - width) / 2600 - x / 4;
            if (y > edge) {
                u32 band = blend(0xf3cbdc, 0x9e88c8, t);
                c = blend(c, band, y - edge < 3 ? (y - edge) * 85 : 256);
            }
            edge = height * 4 / 5 + x / 7;
            if (y > edge) c = blend(c, 0x514984, y - edge < 3 ? (y - edge) * 85 : 256);
            wallpaper[y * width + x] = c;
        }
    }
    for (int y = 0; y < 32 && y < height; y++) for (int x = 0; x < width; x++) {
        u32 *p = &wallpaper[y * width + x];
        *p = blend(*p, dark ? 0x090b12 : 0xf7f7ff, dark ? 215 : 208);
        if (y == 31) *p = blend(*p, dark ? 0x1c2130 : 0xdad4e4, 180);
    }
}

static void desktop_paint_wallpaper_for_mode(u32 *wallpaper, int dark) {
    int width = shell.width, height = shell.height;
    if (!wallpaper || width <= 0 || height <= 0) return;
    int image_width = 0, image_height = 0;
    u8 *image = desktop_decode_theme_wallpaper(dark, &image_width, &image_height);
    if (image) {
        desktop_paint_theme_wallpaper(wallpaper, image, image_width, image_height, 256, dark);
        media_free(image);
        return;
    }
    if (!g_wallpaper_failure_logged) {
        serial("GFX wallpaper unavailable; procedural background active\n");
        g_wallpaper_failure_logged = 1;
    }
    desktop_paint_procedural_wallpaper(wallpaper, width, height, dark);
}

void desktop_prepare_theme_wallpapers(u32 *buffer, u16 *unused, int unused_width, int unused_height) {
    (void)unused; (void)unused_width; (void)unused_height;
    if (buffer) desktop_paint_wallpaper_for_mode(buffer, ui_is_dark());
}

void desktop_paint_wallpaper(u32 *wallpaper) {
    desktop_paint_wallpaper_for_mode(wallpaper, ui_is_dark());
}

void desktop_paint_wallpaper_fallback(u32 *wallpaper, int dark) {
    desktop_paint_procedural_wallpaper(wallpaper, shell.width, shell.height, dark);
}

void desktop_init(void) {
    extern int framebuffer_width(void), framebuffer_height(void);
    shell.width = framebuffer_width(); shell.height = framebuffer_height();
    compositor_init();
}
void desktop_start(void) {
    graphics_init(shell.width, shell.height);
    ui_init();
    trash_init();
    ui_anim_init();
    desktop_items_init();
    dock_load_config();
    compositor_prepare_wallpapers();
    wm_init(shell.width, shell.height);
    if (!welcome_is_first_boot()) {
        close_app(APP_WELCOME);
    }
    compositor_paint(1);
    compositor_draw_cursor(1);
    shell.dirty = 0;
    desktop_ready = 1;
}
void app_host_service_loading(void) {
    static int servicing;
    if (!desktop_ready || servicing) return;
    servicing = 1;
    input_dispatch_poll_window_only();
    desktop_present();
    servicing = 0;
}
int desktop_poll(int network_changed) {
    /* Only poll applications that are actually on screen. A closed or hidden
     * window must not keep running its poll callback in the background. */
    u32 active_mask = 0;
    for (int id = 0; id < APP_COUNT; id++) {
        Window *w = wm_get_window(id);
        if (w->open && w->visible && !w->minimized) active_mask |= 1u << id;
    }
    u32 changed_apps = gui_apps_poll_mask(active_mask);
    for (int id = 0; id < APP_COUNT; id++) {
        Window *w = wm_get_window(id);
        if ((changed_apps & (1u << id)) && w->open && w->visible && !w->minimized) {
            /* In-window animation: repaint only the window region, not the
             * whole desktop, so the cursor stays smooth while media plays. */
            compositor_invalidate_animated(id);
            shell.dirty = 1;
            shell.partial = 1;
        }
    }
    if (network_changed) shell.dirty = 1;
    input_dispatch_poll();
    return desktop_present();
}
/* Shared frame scheduling only; never call desktop_poll from a wait callback. */
static int desktop_present(void) {
    static int was_animating;
    static u32 auth_blink;
    u32 now_ms = wm_time_ms();
    /* Animate the login caret without repainting on every frame. */
    if (auth_is_active()) {
        u32 blink = (ticks / 500u) & 1u;
        if (blink != auth_blink) {
            auth_blink = blink;
            shell.dirty = 1;
        }
    }
    int animating = compositor_animate(now_ms);
    if (animating || was_animating) {
        shell.dirty = 1;
        shell.partial = 1;
    }
    was_animating = animating;
    if (wm_has_active_animations()) shell.dirty = 1;
    wm_update_animations(now_ms);
    int is_busy = shell.drag || shell.resizing >= 0 || animating || wm_has_active_animations();
    /* Poll the RTC once per second while otherwise idle. A changed clock only
     * dirties the menu strip, so it never forces a full desktop repaint. */
    if (!auth_is_active() && !shell.dirty && !is_busy && desktop_bar_update_clock(now_ms)) {
        g_bar_only_dirty = 1;
        shell.dirty = 1;
    }
    int frame_due = wm_frame_due(now_ms);
    int dock_frozen = auth_is_active() || g_active_menu.active || g_active_dialog.active;
    if (dock_frozen && !g_dock_motion_frozen) {
        g_dock_motion_frozen = 1;
        g_dock_motion_freeze_ms = now_ms;
    } else if (!dock_frozen && g_dock_motion_frozen) {
        u32 paused = now_ms - g_dock_motion_freeze_ms;
        for (int k = 0; k < NUM_APPS; ++k)
            if (g_dock_motion_active[k]) g_dock_motion_start[k] += paused;
        g_dock_motion_frozen = 0;
    }
    /* Freeze the Dock while a menu covers it; this keeps context menus from
     * repainting the icon shelf on every mouse packet. */
    int hover_target = auth_is_active() || g_active_menu.active || g_active_dialog.active
        ? shell.hover : dock_hit();
    int dock_motion_running = 0;
    if ((frame_due || g_bar_only_dirty) &&
        (is_busy || shell.dirty || hover_target != shell.hover ||
         (!dock_frozen && g_dock_motion_initialized))) {
        int changed = 0;
        shell.hover = hover_target;
        int visible[APP_COUNT];
        int vcount = dock_get_visible_apps(visible, APP_COUNT);
        int h_slot = -1;
        for (int k = 0; k < vcount; k++) {
            if (visible[k] == shell.hover) { h_slot = k; break; }
        }
        if (!dock_frozen) {
            if (!g_dock_motion_initialized) {
                for (int k = 0; k < NUM_APPS; ++k) {
                    g_dock_motion_goal[k] = shell.hover_level[k];
                    g_dock_motion_active[k] = 0;
                }
                g_dock_motion_initialized = 1;
            }
            for (int k = 0; k < NUM_APPS; ++k) {
                int goal = 0;
                if (shell.dock_zoom && h_slot >= 0) {
                    for (int v = 0; v < vcount; v++) {
                        if (visible[v] == k) {
                            if (v == h_slot) goal = 256;
                            else if (v == h_slot - 1 || v == h_slot + 1) goal = 96;
                            break;
                        }
                    }
                }
                int old = shell.hover_level[k];
                if (goal != g_dock_motion_goal[k]) {
                    g_dock_motion_goal[k] = goal;
                    g_dock_motion_from[k] = old;
                    g_dock_motion_start[k] = now_ms;
                    g_dock_motion_active[k] = shell.animations_mode && old != goal;
                }
                if (!shell.animations_mode) {
                    shell.hover_level[k] = goal;
                    g_dock_motion_active[k] = 0;
                } else if (g_dock_motion_active[k]) {
                    u32 elapsed = now_ms - g_dock_motion_start[k];
                    if (elapsed >= 190u) {
                        shell.hover_level[k] = goal;
                        g_dock_motion_active[k] = 0;
                    } else {
                        int t = (int)(elapsed * 256u / 190u);
                        int eased = dock_ease_out_back(t);
                        shell.hover_level[k] = g_dock_motion_from[k] +
                            ((goal - g_dock_motion_from[k]) * eased >> 8);
                    }
                } else {
                    shell.hover_level[k] = goal;
                }
                if (shell.hover_level[k] != old) changed = 1;
                if (g_dock_motion_active[k]) dock_motion_running = 1;
            }
        }
        if (shell.dirty || changed || dock_motion_running) {
            int full = is_busy ? 2 : (shell.dirty ? (g_bar_only_dirty ? 3 : (shell.partial ? 2 : 1)) : 0);
            compositor_paint(full);
            shell.partial = 0;
            wm_frame_scheduled(now_ms);
        }
    }
    compositor_draw_cursor(0);
    return !shell.dirty && !is_busy && !dock_motion_running &&
           (auth_is_active() || dock_hit() == shell.hover);
}
