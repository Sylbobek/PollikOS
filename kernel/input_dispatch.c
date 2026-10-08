#include "input_dispatch.h"
#include "shell_internal.h"
#include "ui.h"
#include "hw.h"
#include "desktop_items.h"
#include "ui_animation.h"
#include "trash.h"
#include "auth.h"
#include "control_center.h"

/* Private PS/2 decoder and pointer state. Symbol names remain compatible with
 * the existing QMP tests without exporting mutable pointers to other modules. */
static int mx = 760, my = 500;
static int mouse_packet_size = 3;
static int alt_held;
static int window_only;
static int s_ctrl_held = 0;
static int app_pointer_capture = -1;
static int pointer_acceleration = 1;
int input_ctrl_held(void) { return s_ctrl_held; }
void input_set_pointer_acceleration(int enabled) { pointer_acceleration = enabled != 0; }
int input_get_pointer_acceleration(void) { return pointer_acceleration; }
int input_pointer_x(void) { return mx; }
int input_pointer_y(void) { return my; }
void cancel_interaction(int id) {
    if(id<0||id==APP_FILES)desktop_items_drag_cancel();
    if (id < 0 || shell.drag_app == id) {
        shell.drag = 0;
        shell.drag_app = -1;
        shell.drag_moved = 0;
        g_dragged_window = -1;
        shell.snap_preview = SNAP_NONE;
    }
    if (id < 0 || shell.resizing == id) {
        shell.resizing = -1;
        shell.resize_edges = RESIZE_NONE;
        g_resized_window = -1;
        g_resize_edges = RESIZE_NONE;
    }
    shell.last_title_click_id = -1;
}
static SnapTarget drag_snap_target(int id) {
    SnapTarget target = wm_check_snap(mx, my);
    Rect r;
    wm_get_snap_bounds(target, &r);
    Window *w = wm_get_window(id);
    if (!w || r.w < w->min_w || r.h < w->min_h || r.w > w->max_w || r.h > w->max_h) return SNAP_NONE;
    return target;
}
int input_cursor_kind(void) {
    if(control_center_active())return CURSOR_POINTER;
    if (shell.drag && shell.drag_moved) return CURSOR_MOVE;
    if (shell.resizing >= 0) {
        if ((shell.resize_edges & (RESIZE_LEFT | RESIZE_TOP)) == (RESIZE_LEFT | RESIZE_TOP) ||
            (shell.resize_edges & (RESIZE_RIGHT | RESIZE_BOTTOM)) == (RESIZE_RIGHT | RESIZE_BOTTOM)) return CURSOR_RESIZE_NWSE;
        if ((shell.resize_edges & (RESIZE_RIGHT | RESIZE_TOP)) == (RESIZE_RIGHT | RESIZE_TOP) ||
            (shell.resize_edges & (RESIZE_LEFT | RESIZE_BOTTOM)) == (RESIZE_LEFT | RESIZE_BOTTOM)) return CURSOR_RESIZE_NESW;
        if (shell.resize_edges & (RESIZE_LEFT | RESIZE_RIGHT)) return CURSOR_RESIZE_H;
        if (shell.resize_edges & (RESIZE_TOP | RESIZE_BOTTOM)) return CURSOR_RESIZE_V;
    }
    if (g_active_menu.active && mx >= g_active_menu.x && mx < g_active_menu.x + g_active_menu.w &&
        my >= g_active_menu.y && my < g_active_menu.y + g_active_menu.h) return CURSOR_POINTER;
    if (g_active_dialog.active) return CURSOR_POINTER;
    if (dock_hit() >= 0) return CURSOR_POINTER;
    int top = active_app();
    if (top >= 0) {
        HitTestResult hit = wm_hit_test(top, mx, my);
        if (hit == HIT_CLOSE || hit == HIT_MINIMIZE || hit == HIT_MAXIMIZE) return CURSOR_POINTER;
        if (hit == HIT_RESIZE_LEFT || hit == HIT_RESIZE_RIGHT) return CURSOR_RESIZE_H;
        if (hit == HIT_RESIZE_TOP || hit == HIT_RESIZE_BOTTOM) return CURSOR_RESIZE_V;
        if (hit == HIT_RESIZE_TOP_LEFT || hit == HIT_RESIZE_BOTTOM_RIGHT) return CURSOR_RESIZE_NWSE;
        if (hit == HIT_RESIZE_TOP_RIGHT || hit == HIT_RESIZE_BOTTOM_LEFT) return CURSOR_RESIZE_NESW;
        return gui_app_cursor(top, mx - windows[top].x, my - windows[top].y);
    }
    if (desktop_items_hit_test(mx, my) >= 0) return CURSOR_POINTER;
    return CURSOR_DEFAULT;
}
static void wait_write(void) {
    for (int t = 0; t < 100000; t++) if (!(inb(0x64) & 2)) break;
}
void app_host_power(int reboot) {
    wait_write();
    if (reboot) power_reboot();
    else power_shutdown();
}
static int mouse_read(void) {
    for (int t = 0; t < 100000; t++) {
        u8 status = inb(0x64);
        if (status & 1) {
            u8 data = inb(0x60);
            if (status & 32) return data;
        }
    }
    return -1;
}
static int mouse_cmd(u8 c) {
    for (int retry = 0; retry < 3; retry++) {
        wait_write(); outb(0x64, 0xd4);
        wait_write(); outb(0x60, c);
        int reply = mouse_read();
        if (reply == 0xfa) return 1;
        if (reply != 0xfe) return 0;
    }
    return 0;
}
void input_dispatch_init(void) {
    wait_write(); outb(0x64, 0xa8);
    wait_write(); outb(0x64, 0x20);
    int config = -1;
    for (int t = 0; t < 100000; t++) if (inb(0x64) & 1) { config = inb(0x60); break; }
    if (config < 0) { serial("INPUT controller timeout\n"); return; }
    wait_write(); outb(0x64, 0x60);
    /* Polling needs no IRQ enables, but both PS/2 clocks must be on. The
     * controller config bit 4 disables the keyboard clock; clear it along
     * with the auxiliary-disable bit before initializing the mouse. */
    wait_write(); outb(0x60, (config | 0x40) & ~0x33);
    if (!mouse_cmd(0xf6)) { serial("INPUT mouse ACK failed\n"); return; }
    int ok = mouse_cmd(0xf3) && mouse_cmd(200) && mouse_cmd(0xf3) && mouse_cmd(100) && mouse_cmd(0xf3) && mouse_cmd(80);
    if (ok && mouse_cmd(0xf2)) {
        int id = mouse_read();
        if (id == 3) mouse_packet_size = 4;
    }
    mouse_cmd(0xf3); mouse_cmd(200);
    if (mouse_cmd(0xf4)) serial(mouse_packet_size == 4 ? "INPUT PS/2 wheel ready; ACK/retry enabled\n" : "INPUT standard PS/2 ready\n");
}
static void click(void) {
    if(control_center_active()) {
        if(!window_only)control_center_click(mx,my);
        return;
    }
    /* Do not execute modal callbacks or launch clients on the loader's stack. */
    if (window_only && (g_active_dialog.active || g_active_menu.active)) return;
    if (g_active_dialog.active && ui_dialog_on_mouse_down(mx, my, 0)) {
        int dx, dy, dw, dh;
        ui_dialog_bounds(&dx, &dy, &dw, &dh);
        request_partial_redraw(dx - 8, dy - 8, dw + 16, dh + 16);
        return;
    }
    if (my < 32) {
        if (!window_only) desktop_bar_handle_click(mx, my);
        return;
    }
    if (g_active_menu.active && ui_menu_on_mouse_down(&g_active_menu, mx, my, 0)) return;

    /* 1. Dock (always in foreground) */
    int target = dock_hit();
    if (target >= 0) {
        if (window_only) return;
        shell.last_title_click_id = -1;
        dock_activate_app(target);
        serial("APP opened\n");
        return;
    }

    /* 2. Window controls & client area */
    for (int z = NUM_APPS - 1; z >= 0; z--) {
        int id = z_order[z];
        if (!windows[id].open || windows[id].minimized) continue;
        int wx = windows[id].x, wy = windows[id].y;
        int ww = window_width(id), wh = window_height(id);
        HitTestResult hit = wm_hit_test(id, mx, my);
        if (hit == HIT_NONE) continue;
        /* Clicking into an application releases any desktop icon selection;
         * otherwise the highlight stayed lit behind the focused window. */
        if (desktop_items_selected_count() > 0) desktop_items_clear_selection();
        if(active_app()!=id)focus_app(id);
        if (hit != HIT_TITLEBAR) shell.last_title_click_id = -1;
        if (hit >= HIT_RESIZE_LEFT && hit <= HIT_RESIZE_BOTTOM_RIGHT) {
            shell.resizing = id;
            g_resized_window = id;
            shell.resize_edges = wm_get_resize_edges(id, mx, my);
            g_resize_edges = shell.resize_edges;
            shell.resize_start_x = wx; shell.resize_start_y = wy;
            shell.resize_start_w = ww; shell.resize_start_h = wh;
            shell.resize_start_mx = mx; shell.resize_start_my = my;
            shell.drag_old_x = wx; shell.drag_old_y = wy;
            shell.drag_old_w = ww; shell.drag_old_h = wh;
            return;
        }
        if (hit == HIT_CLOSE) { close_app(id); return; }
        if (hit == HIT_MINIMIZE) { minimize_app(id); return; }
        if (hit == HIT_MAXIMIZE) { toggle_maximize(id); return; }
        if (hit == HIT_TITLEBAR) {
            u32 now_ms = wm_time_ms();
            int click_dx = mx - shell.last_title_click_x, click_dy = my - shell.last_title_click_y;
            if (shell.last_title_click_id == id && now_ms - shell.last_title_click_ms <= TITLE_DOUBLECLICK_MS &&
                click_dx >= -TITLE_DOUBLECLICK_DISTANCE && click_dx <= TITLE_DOUBLECLICK_DISTANCE &&
                click_dy >= -TITLE_DOUBLECLICK_DISTANCE && click_dy <= TITLE_DOUBLECLICK_DISTANCE) {
                toggle_maximize(id); return;
            }
            shell.last_title_click_id = id;
            shell.last_title_click_ms = now_ms;
            shell.last_title_click_x = mx; shell.last_title_click_y = my;
            shell.drag = 1; /* Pending until the pointer crosses the movement threshold. */
            shell.drag_app = id; shell.drag_moved = 0;
            shell.drag_start_mx = mx; shell.drag_start_my = my;
            shell.snap_preview = SNAP_NONE;
            shell.dx = mx - wx; shell.dy = my - wy;
            shell.drag_old_x = wx; shell.drag_old_y = wy;
            shell.drag_old_w = ww; shell.drag_old_h = wh;
            return;
        }
        if (hit == HIT_CLIENT) {
            if (window_only) return;
            if(id!=APP_CALCULATOR)compositor_invalidate(id);
            int local_x = mx - wx, local_y = my - wy;
            gui_app_click(id, local_x, local_y);
            if(id==APP_FILES&&active_app()==APP_FILES){
                char path[128],name[64];int is_dir;
                if(files_drag_path(local_x,local_y,path,name,&is_dir))desktop_items_drag_file(path,name,is_dir,mx,my);
            }
            app_pointer_capture = gui_app_drag(id, local_x, local_y, 1) >= 0 ? id : -1;
            return;
        }
    }

    /* 3. Top bar */
    if (my < 30) {
        shell.last_title_click_id = -1;
        wm_unfocus();
        desktop_items_clear_selection();
        request_scene_redraw();
        return;
    }

    /* 4. Desktop items & desktop background */
    shell.last_title_click_id = -1;
    wm_unfocus();
    int item_idx = desktop_items_hit_test(mx, my);
    if (item_idx >= 0) {
        if (s_ctrl_held) {
            desktop_items_toggle_select(item_idx);
        } else {
            if (!desktop_items_is_selected(item_idx)) {
                desktop_items_select_single(item_idx);
            }
            static int last_desktop_click_id = -1;
            static u32 last_desktop_click_ms = 0;
            u32 now_ms = wm_time_ms();
            if (last_desktop_click_id == item_idx && (now_ms - last_desktop_click_ms <= 400)) {
                desktop_items_open(item_idx);
                last_desktop_click_id = -1;
            } else {
                last_desktop_click_id = item_idx;
                last_desktop_click_ms = now_ms;
                desktop_items_drag_start(item_idx, mx, my);
            }
        }
    } else {
        if (!s_ctrl_held) {
            desktop_items_clear_selection();
        }
        if (my >= 30 && my < shell.height - 114) {
            desktop_items_marquee_start(mx, my);
        }
    }
    request_scene_redraw();
}
static void key(u8 code) {
    static int shift, control;
    if (code == 29) { s_ctrl_held = 1; control = 1; return; }
    if (code == 157) { s_ctrl_held = 0; control = 0; return; }
    if (code == 42 || code == 54) { shift = 1; return; }
    if (code == 170 || code == 182) { shift = 0; return; }
    if (auth_is_active()) {
        desktop_items_drag_cancel();
        if (!(code & 128)) auth_key_ex(code, shift, control);
        request_scene_redraw();
        return;
    }
    if(code==1&&desktop_items_is_dragging()){desktop_items_drag_cancel();return;}
    if (window_only) {
        if (code == 56) alt_held = 1;
        if(alt_held && code==62){
            /* Host window close is safe here; the browser defers DOM teardown
             * until its loader unwinds. Client editing remains suppressed. */
            shell.alttab_open=0;close_app(active_app());request_scene_redraw();
        }
        if (code == 184) {
            alt_held = 0;
            if (shell.alttab_open) { shell.alttab_open = 0; request_scene_redraw(); }
        }
        return; /* Discard, do not replay commands against a different focus. */
    }
    if(!window_only&&control_center_key_ex(code,shift,control))return;
    if (g_active_dialog.active && ui_dialog_on_key(code, shift, control)) { shell.dirty = 1; return; }
    if (g_active_menu.active && ui_menu_on_key(&g_active_menu, code, shift)) {
        if (g_active_menu.active)
            request_partial_redraw(g_active_menu.x - 4, g_active_menu.y - 3,
                                   g_active_menu.w + 8, g_active_menu.h + 10);
        return;
    }
    if (code == 56) { alt_held = 1; return; }
    if (code == 184) {
        alt_held = 0;
        if (shell.alttab_open) {
            shell.alttab_open = 0;
            Window *selected = wm_get_window(shell.alttab_selected);
            if (selected && selected->open) open_app(shell.alttab_selected);
            request_scene_redraw();
        }
        return;
    }
    if (alt_held && code == 15) {
        int open_apps[NUM_APPS], count = 0;
        for (int z = NUM_APPS - 1; z >= 0; z--) {
            int id = z_order[z];
            if (windows[id].open) open_apps[count++] = id;
        }
        if (count > 0) {
            int from = shell.alttab_open ? shell.alttab_selected : active_app(), cur_idx = -1;
            for (int i = 0; i < count; i++) if (open_apps[i] == from) { cur_idx = i; break; }
            if (cur_idx < 0) shell.alttab_selected = shift ? open_apps[count - 1] : open_apps[0];
            else shell.alttab_selected = open_apps[(cur_idx + (shift ? count - 1 : 1)) % count];
            cancel_interaction(-1); ui_menu_close(&g_active_menu);
            shell.alttab_open = 1;
            request_scene_redraw();
        }
        return;
    }
    if (code & 128) return;
#ifndef POLLIK_INSTALL_MEDIA
    if (code == 88) { /* F12 toggles the compositor timing overlay. */
        wm_perf_overlay_toggle();
        request_partial_redraw(8, 35, 660, 58);
        return;
    }
#endif
    if (shell.alttab_open && code == 1) { shell.alttab_open = 0; request_scene_redraw(); return; }
    if (alt_held && code == 62) {
        shell.alttab_open = 0;
        close_app(active_app()); request_scene_redraw(); return;
    }
    if (shell.alttab_open) return;
    shell.dirty = 1;
    int top = active_app();
    /* Calculator owns precise readout damage; pre-invalidating the whole
     * client discards that region and repaints all button faces on every key. */
    if (top >= 0 && top != APP_CALCULATOR) compositor_invalidate(top);
    if (code == 60) { /* F2: rename selected item */
        if (desktop_items_get_selected() >= 0) {
            desktop_prompt_rename_selected();
            return;
        }
    }
    if (code == 83) { /* Del: move selected item to trash */
        if (desktop_items_selected_count() > 0) {
            desktop_prompt_delete_selected();
            return;
        }
    }
    if (code == 28) { /* Enter: open selected item if no window has focus */
        int sel = desktop_items_get_selected();
        if (sel >= 0 && top < 0) {
            desktop_items_open(sel);
            return;
        }
    }
    if (code >= 59 && code < 59 + APP_COUNT) { open_app(code - 59); serial("APP keyboard open\n"); return; }
    if (code == 87) { if (top >= 0) toggle_maximize(top); return; }
    if (code == 1) {
        if (top == APP_POLLIKMARK) gui_app_key(top, code, shift, control);
        else if (top >= 0) minimize_app(top);
        return;
    }
    gui_app_key(top, code, shift, control);
}
static void pointer_packet(const u8 *packet) {
    static int held, right_held;
    if (packet[0] & 0xc0) return;
    int pdx = (int)packet[1] - ((packet[0] & 16) ? 256 : 0);
    int pdy = -((int)packet[2] - ((packet[0] & 32) ? 256 : 0));
    if (pointer_acceleration) {
        int ax = pdx < 0 ? -pdx : pdx, ay = pdy < 0 ? -pdy : pdy;
        int speed = ax > ay ? ax : ay;
        if (speed >= 6) { pdx += pdx / 5; pdy += pdy / 5; }
    }
    mx += pdx; my += pdy;
    if (mx < 0) mx = 0;
    if (mx > shell.width - 1) mx = shell.width - 1;
    if (my < 0) my = 0;
    if (my > shell.height - 1) my = shell.height - 1;
    if (auth_is_active()) {
        if (auth_pointer(mx, my, packet[0] & 1)) request_scene_redraw();
        /* Keep button edges current across logout and the next sign-in. */
        held=(packet[0]&1)!=0;right_held=(packet[0]&2)!=0;
        compositor_draw_cursor(0);
        return;
    }
    if(control_center_active()) {
        int down=packet[0]&1;
        if(down&&!held&&!window_only)control_center_click(mx,my);
        if(!window_only)control_center_pointer(mx,my,down);
        held=down;right_held=(packet[0]&2)!=0;compositor_draw_cursor(0);return;
    }
    /* A modal dialog repaints only its own card: focused-button changes and
     * title-bar drags damage the union of the old and new rectangle, never the
     * whole scene, so the pointer stays smooth on every PS/2 packet. */
    if (g_active_dialog.active) {
        int ox, oy, ow, oh, was_focused = g_active_dialog.focused_btn;
        ui_dialog_bounds(&ox, &oy, &ow, &oh);
        int moved = ui_dialog_on_mouse_move(mx, my);
        if (moved || g_active_dialog.focused_btn != was_focused) {
            int nx, ny, nw, nh;
            ui_dialog_bounds(&nx, &ny, &nw, &nh);
            int ux1 = ox < nx ? ox : nx, uy1 = oy < ny ? oy : ny;
            int ux2 = ox + ow > nx + nw ? ox + ow : nx + nw;
            int uy2 = oy + oh > ny + nh ? oy + oh : ny + nh;
            request_partial_redraw(ux1 - 8, uy1 - 8, (ux2 - ux1) + 16, (uy2 - uy1) + 16);
        }
    }
    if (g_active_menu.active && ui_menu_on_mouse_move(&g_active_menu, mx, my))
        request_partial_redraw(g_active_menu.x - 4, g_active_menu.y - 3,
                               g_active_menu.w + 8, g_active_menu.h + 10);
    /* While a modal dialog is open nothing behind it may react: no bar hover,
     * no desktop icon drag/marquee, no wheel scroll and no context menu. This
     * is what previously let a second right-click stack a menu on the About
     * card and leave the desktop stuck. */
    if (!g_active_dialog.active) {
        if (desktop_bar_handle_pointer(mx, my)) shell.dirty = 1;
        if (desktop_items_is_dragging()) {
            desktop_items_drag_move(mx, my);
            shell.dirty = 1;
        }
        if (desktop_items_marquee_is_active()) {
            desktop_items_marquee_update(mx, my, s_ctrl_held);
            shell.dirty = 1;
        }
    }
    wm_perf_record_input(pdx != 0 || pdy != 0);
    if (!window_only && !g_active_dialog.active && mouse_packet_size == 4 && packet[3])
        gui_app_scroll(active_app(), (signed char)packet[3]);
    int down = packet[0] & 1, right = packet[0] & 2;
    if (!window_only && !g_active_dialog.active && right && !right_held && my >= 32) {
        int interaction_changed_scene = shell.drag || shell.resizing >= 0 || shell.snap_preview != SNAP_NONE;
        cancel_interaction(-1);
        if (interaction_changed_scene) request_scene_redraw();
        shell.context_app = -1;
        int dh = dock_hit();
        if (dh >= 0) {
            open_dock_context_menu(mx, my, dh);
        } else {
            int hit_file = -1;
            for (int z = NUM_APPS - 1; z >= 0; z--) {
                int id = z_order[z], wx = windows[id].x, wy = windows[id].y;
                if (!windows[id].open || windows[id].minimized) continue;
                if (mx >= wx && mx < wx + window_width(id) && my >= wy && my < wy + window_height(id)) {
                    shell.context_app = id;
                    focus_app(id); compositor_invalidate(id);
                    if (id == APP_FILES) hit_file = files_select_at(mx - wx, my - wy);
                    break;
                }
            }
            if (shell.context_app >= 0) {
                open_context_menu(mx, my, shell.context_app, hit_file);
            } else {
                int item_idx = desktop_items_hit_test(mx, my);
                if (item_idx >= 0) {
                    if (!desktop_items_is_selected(item_idx)) {
                        desktop_items_select_single(item_idx);
                    }
                    open_context_menu(mx, my, CONTEXT_DESKTOP_ITEM, item_idx);
                } else {
                    desktop_items_clear_selection();
                    open_context_menu(mx, my, -1, -1);
                }
            }
        }
    }
    right_held = right;
    if (down && !held) click();
    if (app_pointer_capture >= 0 && (down || held) && (pdx || pdy)) {
        int id = app_pointer_capture;
        if (gui_app_drag(id, mx - windows[id].x, my - windows[id].y, 1) > 0) {
            compositor_invalidate_animated(id);
            shell.dirty = 1;
            shell.partial = 1;
        }
    }
    /* Include the final release packet's movement before committing. */
    if ((down || held) && shell.resizing >= 0) {
        int id = shell.resizing;
        Window *w = &windows[id];
        Rect before = {w->x, w->y, w->width, w->height};
        Rect start = {shell.resize_start_x, shell.resize_start_y, shell.resize_start_w, shell.resize_start_h};
        wm_resize_window(id, start, shell.resize_edges, mx - shell.resize_start_mx, my - shell.resize_start_my);
        if (w->x != before.x || w->y != before.y || w->width != before.w || w->height != before.h) {
            compositor_invalidate(id); gui_app_resized(id, w->width, w->height); shell.dirty = 1;
        }
    }
    if ((down || held) && shell.drag && shell.drag_app >= 0) {
        int id = shell.drag_app;
        Window *w = &windows[id];
        int moved_x = mx - shell.drag_start_mx, moved_y = my - shell.drag_start_my;
        if (!shell.drag_moved && (moved_x <= -DRAG_THRESHOLD || moved_x >= DRAG_THRESHOLD ||
                                 moved_y <= -DRAG_THRESHOLD || moved_y >= DRAG_THRESHOLD)) {
            shell.drag_moved = 1; g_dragged_window = id; shell.last_title_click_id = -1;
            if (w->state == WINDOW_STATE_MAXIMIZED || w->state == WINDOW_STATE_SNAPPED) {
                wm_begin_drag_restore(id, shell.drag_start_mx, shell.drag_start_my);
                shell.dx = shell.drag_start_mx - w->x; shell.dy = shell.drag_start_my - w->y;
                compositor_invalidate(id); gui_app_resized(id, w->width, w->height); request_scene_redraw();
            }
        }
        if (shell.drag_moved && w->state == WINDOW_STATE_NORMAL) {
            WorkArea wa;
            wm_get_work_area(&wa);
            /* Hard guards: never let a dragged window cover the top bar or the
             * Dock shelf. The work area is the base bound, then the shadow's
             * +7 top / +14 bottom extent is kept clear of both. */
            int top_limit = wa.y;
            int bottom_limit = wa.y + wa.h;
            if (top_limit < 39) top_limit = 39;          /* bar 0..32 + shadow */
            int dock_top = shell.height - 145;
            if (bottom_limit > dock_top) bottom_limit = dock_top;
            int new_x = mx - shell.dx, new_y = my - shell.dy;
            int max_x = wa.x + wa.w - w->width, max_y = bottom_limit - w->height - 14;
            if (max_x < wa.x) max_x = wa.x;
            if (max_y < top_limit) max_y = top_limit;
            if (new_x < wa.x) new_x = wa.x;
            if (new_x > max_x) new_x = max_x;
            if (new_y < top_limit) new_y = top_limit;
            if (new_y > max_y) new_y = max_y;
            /* Never let the titlebar slip under the top bar even after a
             * maximize-restore drag that started on the old geometry. */
            if (new_y < 36) new_y = 36;
            if (w->x != new_x || w->y != new_y) {
                wm_invalidate_window(id); w->x = new_x; w->y = new_y; wm_invalidate_window(id);
                shell.dirty = 1; /* Translation reuses the client cache. */
            }
            SnapTarget prev_snap = shell.snap_preview;
            shell.snap_preview = drag_snap_target(id);
            if (shell.snap_preview != prev_snap) {
                /* Only the old and new preview rectangles changed. Damaging
                 * just those keeps a drag smooth when the preview appears or
                 * disappears, instead of a full-scene repaint. */
                int ux1 = 0x7fffffff, uy1 = 0x7fffffff;
                int ux2 = -0x7fffffff, uy2 = -0x7fffffff;
                Rect r;
                if (prev_snap != SNAP_NONE) {
                    wm_get_snap_bounds(prev_snap, &r);
                    if (r.w > 0 && r.h > 0) {
                        ux1 = r.x; uy1 = r.y; ux2 = r.x + r.w; uy2 = r.y + r.h;
                    }
                }
                if (shell.snap_preview != SNAP_NONE) {
                    wm_get_snap_bounds(shell.snap_preview, &r);
                    if (r.w > 0 && r.h > 0) {
                        if (r.x < ux1) ux1 = r.x;
                        if (r.y < uy1) uy1 = r.y;
                        if (r.x + r.w > ux2) ux2 = r.x + r.w;
                        if (r.y + r.h > uy2) uy2 = r.y + r.h;
                    }
                }
                if (ux2 > ux1 && uy2 > uy1)
                    request_partial_redraw(ux1 - 16, uy1 - 16,
                                           (ux2 - ux1) + 32, (uy2 - uy1) + 32);
            }
        }
    }
    if (!down) {
        if (g_active_dialog.active) ui_dialog_on_mouse_up(mx, my);
        if (app_pointer_capture >= 0) {
            int id = app_pointer_capture;
            gui_app_drag(id, mx - windows[id].x, my - windows[id].y, 0);
            app_pointer_capture = -1;
        }
        if (desktop_items_is_dragging()) {
            desktop_items_drag_end(mx, my);
            shell.dirty = 1;
        }
        if (desktop_items_marquee_is_active()) {
            desktop_items_marquee_end();
            shell.dirty = 1;
        }
        if (shell.resizing >= 0) {
            cancel_interaction(shell.resizing); request_scene_redraw(); serial("WINDOW resized\n");
        }
        if (shell.drag) {
            if (shell.drag_app >= 0 && shell.drag_moved) {
                SnapTarget snap = drag_snap_target(shell.drag_app);
                if (snap != SNAP_NONE) {
                    int id = shell.drag_app;
                    wm_snap(id, snap); compositor_invalidate(id);
                    gui_app_resized(id, window_width(id), window_height(id));
                }
            }
            /* Preserve the first stationary click for double-click detection. */
            shell.drag = 0; shell.drag_app = -1; shell.drag_moved = 0;
            g_dragged_window = -1; shell.snap_preview = SNAP_NONE;
            request_scene_redraw();
        }
    }
    /* A modal dialog owns the pointer; never track titlebar hover behind it.
     * Hover transitions repaint only the affected window frames instead of
     * forcing a full-scene pass on every PS/2 packet. */
    if (!shell.drag && shell.resizing < 0 && !g_active_dialog.active) {
        int top = active_app(), hbtn = 0;
        if (top >= 0) {
            HitTestResult ht = wm_hit_test(top, mx, my);
            if (ht == HIT_CLOSE) hbtn = 1;
            else if (ht == HIT_MINIMIZE) hbtn = 2;
            else if (ht == HIT_MAXIMIZE) hbtn = 3;
        }
        if (hbtn != shell.hovered_btn || top != g_hovered_window) {
            if (g_hovered_window >= 0 && g_hovered_window < NUM_APPS)
                compositor_invalidate_animated(g_hovered_window);
            shell.hovered_btn = hbtn; g_hovered_window = top;
            if (top >= 0) compositor_invalidate_animated(top);
            shell.dirty = 1;
            shell.partial = 1;
        }
        static int prev_dock_vis = 1;
        int cur_dock_vis = dock_is_visible();
        if (cur_dock_vis != prev_dock_vis) {
            prev_dock_vis = cur_dock_vis;
            request_scene_redraw();
        }
    }
    held = down;
    compositor_draw_cursor(0);
}
void input_dispatch_poll(void) {
    static u8 packet[4];
    static int index;
    for (int count = 0; count < 256; count++) {
        u8 s = inb(0x64);
        if (!(s & 1)) break;
        u8 c = inb(0x60);
        if (s & 32) {
            if (index == 0 && !(c & 8)) continue;
            packet[index++] = c;
            if (index == mouse_packet_size) { index = 0; pointer_packet(packet); }
        } else key(c);
    }
}
void input_dispatch_poll_window_only(void) {
    if (window_only) return;
    window_only = 1;
    input_dispatch_poll();
    window_only = 0;
}
