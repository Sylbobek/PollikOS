#include "desktop.h"
#include "shell_internal.h"
#include "klog.h"
#include "pmm.h"
#include "input_dispatch.h"

static int desktop_ready;
static int desktop_present(void);

ShellState shell = {
    .width = 1024, .height = 768, .dirty = 1, .scene_dirty = 1,
    .animations_mode = 1, .drag_app = -1, .resizing = -1,
    .last_title_click_id = -1, .context_app = -1, .hover = -1
};
_Static_assert(APP_COUNT == NUM_APPS, "Registry and WM slots must agree");
void request_scene_redraw(void) { shell.dirty = 1; shell.scene_dirty = 1; }
void focus_app(int id) { wm_focus(id); request_scene_redraw(); }
void open_app(int id) {
    if (!gui_app_get(id) || !wm_get_window(id)) return;
    cancel_interaction(-1);
    compositor_cancel_minimize(id);
    wm_open(id);
    gui_app_resized(id, window_width(id), window_height(id));
    gui_app_opened(id);
    compositor_invalidate(id);
    request_scene_redraw();
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
    compositor_minimize(id);
    wm_minimize(id);
    request_scene_redraw();
}
void close_app(int id) {
    Window *w = wm_get_window(id);
    if (!w) return;
    cancel_interaction(id);
    compositor_cancel_minimize(id);
    int was_open = w->open;
    wm_close(id);
    if (was_open) gui_app_closed(id);
    request_scene_redraw();
}
void app_host_open(int id) { open_app(id); }
void app_host_invalidate(int id) { compositor_invalidate(id); request_scene_redraw(); }
int app_host_theme(void) { return shell.theme; }
void app_host_set_theme(int value) { shell.theme = value; }
int app_host_animations(void) { return shell.animations_mode; }
void app_host_set_animations(int enabled) { shell.animations_mode = enabled; shell.dirty = 1; }
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

static void on_delete_dialog_result(int confirmed) {
    shell.dirty = 1;
    if (confirmed) {
        if (shell.context_app == APP_FILES) files_delete_selected();
        ui_notify("Trash", "Item moved to Trash", ICON_TRASH);
    }
}
static void on_menu_action(int action_id) {
    shell.dirty = 1;
    switch (action_id) {
        case ACTION_ABOUT_POLLIKOS:
            ui_dialog_message("About PollikOS", "PollikOS Desktop Edition\nx86 Native Compositor & Shell\nA desktop OS with Pollik UX design.", ICON_APP, NULL);
            break;
        case ACTION_DISPLAY_SETTINGS:
            ui_toggle_theme_mode();
            ui_notify("Appearance", ui_is_dark() ? "Dark Theme enabled" : "Light Theme enabled", ICON_SETTINGS);
            break;
        case ACTION_DESKTOP_SETTINGS: ui_notify("Desktop", "Desktop settings: default user", ICON_SETTINGS); break;
        case ACTION_NEW_FOLDER: ui_notify("Files", "Created new folder", ICON_FOLDER); break;
        case ACTION_OPEN:
            if (shell.context_app == APP_FILES && notes_selected_file() >= 0) files_open_selected();
            else if (shell.context_app >= 0) open_app(shell.context_app);
            break;
        case ACTION_OPEN_WITH: ui_notify("Open With", "Applications: Notes, Browser", ICON_APP); break;
        case ACTION_RENAME: ui_notify("Files", "Rename file requested", ICON_FILE); break;
        case ACTION_COPY: ui_notify("Clipboard", "Copied to clipboard", ICON_FILE); break;
        case ACTION_CUT: ui_notify("Clipboard", "Cut to clipboard", ICON_FILE); break;
        case ACTION_DELETE:
            ui_dialog_confirm("Move to Trash?", "Are you sure you want to move\nthe selected item to Trash?",
                "Move to Trash", "Cancel", 1, ICON_TRASH, on_delete_dialog_result);
            break;
        case ACTION_PROPERTIES:
            ui_dialog_message("Properties", (shell.context_app == APP_FILES && files_selected_name()) ? files_selected_name() : "PollikOS Desktop Object", ICON_FILE, NULL);
            break;
        case ACTION_MINIMIZE: if (shell.context_app >= 0) minimize_app(shell.context_app); break;
        case ACTION_MAXIMIZE: if (shell.context_app >= 0) toggle_maximize(shell.context_app); break;
        case ACTION_CLOSE_WINDOW: if (shell.context_app >= 0) close_app(shell.context_app); break;
        default: break;
    }
}
void open_context_menu(int screen_x, int screen_y, int app_id, int file_slot) {
    UiMenuItem items[12];
    int count = 0;
    if (app_id == APP_FILES && file_slot >= 0 && files_selected_name()) {
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN, "Open", "Enter", ICON_FILE, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_OPEN_WITH, "Open With...", NULL, ICON_APP, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_RENAME, "Rename", "F2", ICON_FILE, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_COPY, "Copy", "Ctrl+C", ICON_FILE, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_CUT, "Cut", "Ctrl+X", ICON_FILE, 1};
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
        items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_DISPLAY_SETTINGS, "Toggle Dark/Light Mode", NULL, ICON_SETTINGS, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_DESKTOP_SETTINGS, "Desktop Settings", NULL, ICON_SETTINGS, 1};
        items[count++] = (UiMenuItem){MENU_ITEM_SEPARATOR, 0, NULL, NULL, ICON_NONE, 0};
        items[count++] = (UiMenuItem){MENU_ITEM_NORMAL, ACTION_ABOUT_POLLIKOS, "About PollikOS...", NULL, ICON_APP, 1};
    }
    ui_menu_open(&g_active_menu, screen_x, screen_y, items, count, on_menu_action);
    shell.dirty = 1;
    serial("MENU opened\n");
}
int dock_hit(void) {
    int mx = input_pointer_x(), my = input_pointer_y();
    if (my < shell.height - 114 || my >= shell.height - 10) return -1;
    for (int i = 0; i < APP_COUNT; i++) {
        int cx = shell.width / 2 - (APP_COUNT - 1) * 34 + i * 68;
        if (mx >= cx - 32 && mx < cx + 32) return i;
    }
    return -1;
}
static void app_icon(int id, int x, int y, int size) {
    const AppIcon *icon = &gui_app_get(id)->icon;
    sprite(x, y, size, size, icon->indices, icon->alpha, icon->palette, icon->width, icon->height);
}
void dock_draw_pill(void) {
    int width = APP_COUNT * 68 + 48;
    int x = (shell.width - width) / 2, y = shell.height - 96;
    rounded(x, y, width, 84, 29, 0xffffff, 205);
    rounded(x + 1, y + 1, width - 2, 82, 28, 0xece8f8, 140);
    rect(x + 1, y + 29, 1, 25, 0xf6f2ff);
    rect(x + width - 2, y + 29, 1, 25, 0xf6f2ff);
}
void dock_draw_content(void) {
    int y = shell.height - 96, top = active_app();
    for (int k = 0; k < APP_COUNT; k++) {
        int level = shell.hover_level[k], size = 56 + level / 16;
        int cx = shell.width / 2 - (APP_COUNT - 1) * 34 + k * 68, iy = y + 12 - level / 32;
        app_icon(k, cx - size / 2, iy, size);
        if (windows[k].open && !compositor_minimizing(k))
            roundrect(cx - 3, y + 74, 6, 4, 2, k == top ? 0x786889 : 0xb0a5c0);
    }
    if (shell.hover >= 0 && shell.hover_level[shell.hover] > 160) {
        const char *name = gui_app_get(shell.hover)->name;
        int cx = shell.width / 2 - (APP_COUNT - 1) * 34 + shell.hover * 68, w = text_width(name, 1) + 24;
        roundrect(cx - w / 2, y - 37, w, 26, 11, 0xf7f4fd);
        centered(cx - w / 2, y - 33, w, name, 0x63536f, 1);
    }
}
void desktop_draw_bar(void) {
    int width = shell.width, top = active_app();
    text(20, 7, top >= 0 ? gui_app_get(top)->name : "Desktop", 0x302d3c, 1);
    int logo_w = 18 + 7 + text_width(OS_NAME, 1), logo_x = (width - logo_w) / 2;
    roundrect(logo_x, 7, 18, 18, 5, 0x8472cc);
    centered(logo_x, 8, 18, "P", 0xffffff, 1);
    text(logo_x + 25, 7, OS_NAME, 0x302d3c, 1);
    roundrect(width - 305, 7, 72, 18, 5, 0xebedf5);
    centered(width - 305, 8, 72, "120 FPS", 0x5b4778, 1);
    roundrect(width - 223, 12, 7, 7, 3, net_ready ? 0x509981 : 0x9493a1);
    text(width - 206, 7, net_ready ? "Connected" : "Offline", 0x514965, 1);
    text(width - 107, 7, OS_VERSION, 0x514965, 1);
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
            rounded(ix - 4, iy - 4, card_w + 8, 48 + 8, 10, 0x8472cc, 180);
            rect(ix - 3, iy + 48, card_w + 6, 2, 0xffffff);
        }
        app_icon(id, ix + (card_w - 44) / 2, iy + 2, 44);
        centered(ix, iy + 56, card_w, gui_app_get(id)->name, is_sel ? 0xffffff : 0xc8c0d8, 1);
    }
}
void desktop_paint_wallpaper(u32 *wallpaper) {
    int width = shell.width, height = shell.height, theme = shell.theme;
    klog_dec(KLOG_CAT_GUI, "Wallpaper bytes: ", (u32)width * (u32)height * 4u);
    klog_dec(KLOG_CAT_GUI, "Free pages before wallpaper paint: ", pmm_get_free_pages_count());
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
        int t = y * 256 / height;
        u32 c = blend(theme ? 0x8fd8d3 : 0xb8b1ef, theme ? 0x1d657e : 0x49438e, t);
        int edge = height / 5 + (x - width / 2) * (x - width / 2) / 1900 - x / 5;
        if (y > edge) {
            u32 band = blend(theme ? 0x60bdbb : 0x9a88d7, theme ? 0x184e72 : 0x423c89, t);
            c = blend(c, band, y - edge < 3 ? (y - edge) * 85 : 256);
        }
        edge = height / 2 + (x - width) * (x - width) / 2600 - x / 4;
        if (y > edge) {
            u32 band = blend(theme ? 0xb2e2d1 : 0xf3cbdc, theme ? 0x317d91 : 0x9e88c8, t);
            c = blend(c, band, y - edge < 3 ? (y - edge) * 85 : 256);
        }
        edge = height * 4 / 5 + x / 7;
        if (y > edge) c = blend(c, theme ? 0x226980 : 0x514984, y - edge < 3 ? (y - edge) * 85 : 256);
        wallpaper[y * width + x] = c;
    }
    for (int y = 0; y < 32 && y < height; y++) for (int x = 0; x < width; x++) {
        u32 *p = &wallpaper[y * width + x];
        *p = blend(*p, 0xf7f7ff, 208);
    }
}
void desktop_init(void) {
    extern int framebuffer_width(void), framebuffer_height(void);
    shell.width = framebuffer_width(); shell.height = framebuffer_height();
    compositor_init();
}
void desktop_start(void) {
    graphics_init(shell.width, shell.height);
    ui_init();
    wm_init(shell.width, shell.height);
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
    u32 changed_apps = gui_apps_poll();
    for (int id = 0; id < APP_COUNT; id++) {
        Window *w = wm_get_window(id);
        if ((changed_apps & (1u << id)) && w->open && w->visible && !w->minimized)
            app_host_invalidate(id);
    }
    if (network_changed) shell.dirty = 1;
    input_dispatch_poll();
    return desktop_present();
}
/* Shared frame scheduling only; never call desktop_poll from a wait callback. */
static int desktop_present(void) {
    static int was_animating;
    u32 now_ms = wm_time_ms();
    int animating = compositor_animate(now_ms);
    if (animating || was_animating) request_scene_redraw();
    was_animating = animating;
    if (wm_has_active_animations()) shell.dirty = 1;
    wm_update_animations(now_ms);
    int is_busy = shell.drag || shell.resizing >= 0 || animating || wm_has_active_animations();
    int frame_due = wm_frame_due(now_ms);
    if (frame_due && (is_busy || shell.dirty || dock_hit() != shell.hover)) {
        int changed = 0;
        shell.hover = dock_hit();
        for (int k = 0; k < NUM_APPS; k++) {
            int goal = 0;
            if (shell.hover >= 0) {
                if (k == shell.hover) goal = 256;
                else if (k == shell.hover - 1 || k == shell.hover + 1) goal = 96;
            }
            int old = shell.hover_level[k];
            if (old != goal) {
                int diff = goal - old;
                int step = diff >= 0 ? (diff * 3 + 7) / 8 : (diff * 3 - 7) / 8;
                if (!step) step = diff > 0 ? 1 : -1;
                if (!shell.animations_mode || (diff > -16 && diff < 16)) step = diff;
                shell.hover_level[k] = old + step;
                changed = 1;
            }
        }
        if (shell.dirty || changed) {
            compositor_paint(is_busy ? 2 : shell.dirty ? 1 : 0);
            wm_frame_scheduled(now_ms);
        }
    }
    compositor_draw_cursor(0);
    return !shell.dirty && !is_busy && dock_hit() == shell.hover;
}
