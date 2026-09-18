#include "ui.h"

/* =========================================================================
 * 1. THEME PALETTES & METRICS
 * ========================================================================= */

static ThemeColors g_dark_theme = {
    .background         = 0x0e0a19,
    .surface            = 0x191427,
    .surface_secondary  = 0x241d36,
    .surface_elevated   = 0x2e2644,
    .border             = 0x3a3053,
    .border_subtle      = 0x29213d,
    .text               = 0xf3effa,
    .text_secondary     = 0xa096b6,
    .text_muted         = 0x6c6282,
    .accent             = 0x7b38ff,
    .accent_hover       = 0x9255ff,
    .selection          = 0x482f7c,
    .selection_text     = 0xffffff,
    .danger             = 0xef5350,
    .danger_hover       = 0xf76a67,
    .success            = 0x43a047,
    .warning            = 0xfb8c00
};

static ThemeColors g_light_theme = {
    .background         = 0xf5f3f8,
    .surface            = 0xffffff,
    .surface_secondary  = 0xf0ecf5,
    .surface_elevated   = 0xffffff,
    .border             = 0xdad2e4,
    .border_subtle      = 0xe8e2f0,
    .text               = 0x201830,
    .text_secondary     = 0x6a6080,
    .text_muted         = 0x9890a6,
    .accent             = 0x6d28f8,
    .accent_hover       = 0x8344fa,
    .selection          = 0xe5dbfc,
    .selection_text     = 0x351675,
    .danger             = 0xef5350,
    .danger_hover       = 0xf76a67,
    .success            = 0x43a047,
    .warning            = 0xfb8c00
};

static ThemeMetrics g_metrics = {
    .corner_radius        = 8,
    .corner_radius_small  = 4,
    .corner_radius_large  = 12,
    .window_title_height  = 32,
    .menu_item_height     = 26,
    .button_height        = 26,
    .padding_small        = 4,
    .padding_medium       = 8,
    .padding_large        = 16
};

static ThemeMode g_current_mode = THEME_DARK;

void ui_init(void) {
    g_current_mode = THEME_DARK;
}

ThemeColors *ui_theme(void) {
    return g_current_mode == THEME_DARK ? &g_dark_theme : &g_light_theme;
}

ThemeMetrics *ui_metrics(void) {
    return &g_metrics;
}

ThemeMode ui_theme_mode(void) {
    return g_current_mode;
}

void ui_set_theme_mode(ThemeMode mode) {
    g_current_mode = mode;
}

void ui_toggle_theme_mode(void) {
    g_current_mode = (g_current_mode == THEME_DARK) ? THEME_LIGHT : THEME_DARK;
}

int ui_is_dark(void) {
    return g_current_mode == THEME_DARK;
}

/* =========================================================================
 * 2. SYSTEM ICONS (PROCEDURAL VECTOR ENGINE)
 * ========================================================================= */

void ui_draw_icon(IconKind kind, int x, int y, int size, u32 accent, u32 fg) {
    if (kind == ICON_NONE || size <= 0) return;
    int s = size;
    ThemeColors *th = ui_theme();
    (void)th;

    switch (kind) {
        case ICON_FOLDER: {
            int tab_w = s * 5 / 12;
            int tab_h = s / 4;
            int body_y = y + s / 6;
            int body_h = s * 5 / 6;
            int pocket_y = y + s * 3 / 8;
            int pocket_h = s * 5 / 8;
            ui_bridge_roundrect(x, y, tab_w, tab_h, 2, accent);
            ui_bridge_roundrect(x, body_y, s, body_h, 3, accent);
            ui_bridge_roundrect(x, pocket_y, s, pocket_h, 3, ui_bridge_blend(accent, 0xffffff, 45));
            ui_bridge_rect(x + 2, pocket_y, s - 4, 1, ui_bridge_blend(accent, 0xffffff, 110));
            break;
        }
        case ICON_FILE: {
            int px = x + s / 6;
            int pw = s * 2 / 3;
            int fold = s / 4;
            ui_bridge_roundrect(px, y, pw, s, 2, fg);
            ui_bridge_rect(px + pw - fold, y, fold, fold, ui_bridge_blend(fg, 0x000000, 40));
            ui_bridge_rect(px + pw - fold, y + fold, fold, 1, ui_bridge_blend(fg, 0x000000, 90));
            ui_bridge_rect(px + pw - fold, y, 1, fold, ui_bridge_blend(fg, 0x000000, 90));
            break;
        }
        case ICON_TEXT: {
            int px = x + s / 6;
            int pw = s * 2 / 3;
            int fold = s / 4;
            ui_bridge_roundrect(px, y, pw, s, 2, fg);
            ui_bridge_rect(px + pw - fold, y, fold, fold, ui_bridge_blend(fg, 0x000000, 40));
            ui_bridge_rect(px + pw - fold, y + fold, fold, 1, ui_bridge_blend(fg, 0x000000, 90));
            int line_w = pw - s / 4;
            int lx = px + s / 8;
            for (int i = 0; i < 3; i++) {
                int ly = y + s / 3 + i * (s / 5);
                int cur_w = (i == 2) ? line_w * 2 / 3 : line_w;
                ui_bridge_rect(lx, ly, cur_w, 1, accent);
            }
            break;
        }
        case ICON_APP: {
            ui_bridge_roundrect(x, y, s, s, s / 4, accent);
            /* Inner geometric diamond/rhombus glyph */
            int half = s / 2;
            int rad = s / 4;
            if (rad < 2) rad = 2;
            ui_bridge_roundrect(x + half - rad, y + half - rad, rad * 2, rad * 2, 2, 0xffffff);
            ui_bridge_rect(x + half - 1, y + s / 5, 2, s * 3 / 5, accent);
            ui_bridge_rect(x + s / 5, y + half - 1, s * 3 / 5, 2, accent);
            break;
        }
        case ICON_IMAGE: {
            ui_bridge_roundrect(x, y, s, s, 2, fg);
            /* Sun circle */
            int sun_sz = s / 4;
            ui_bridge_roundrect(x + s * 2 / 3, y + s / 5, sun_sz, sun_sz, sun_sz / 2, 0xfb8c00);
            /* Mountain peak 1 */
            ui_bridge_roundrect(x + 2, y + s / 2, s * 2 / 3, s / 2 - 2, 2, accent);
            /* Mountain peak 2 */
            ui_bridge_roundrect(x + s / 3, y + s * 3 / 8, s * 2 / 3 - 2, s * 5 / 8 - 2, 2, ui_bridge_blend(accent, 0xffffff, 60));
            break;
        }
        case ICON_DISK: {
            ui_bridge_roundrect(x, y, s, s, 3, fg);
            int slot_w = s * 3 / 4;
            int slot_h = s / 3;
            ui_bridge_roundrect(x + (s - slot_w) / 2, y + s / 6, slot_w, slot_h, 2, ui_bridge_blend(fg, 0x000000, 60));
            /* Activity LED */
            ui_bridge_rect(x + s * 3 / 4, y + s * 3 / 4, s > 20 ? 3 : 2, s > 20 ? 3 : 2, 0x43a047);
            break;
        }
        case ICON_TRASH: {
            int body_x = x + s / 5;
            int body_w = s * 3 / 5;
            int body_y = y + s / 4;
            int body_h = s * 3 / 4;
            /* Lid */
            ui_bridge_rect(x + 1, y + s / 6, s - 2, 2, fg);
            ui_bridge_rect(x + s * 3 / 8, y + s / 10, s / 4, 2, fg);
            /* Can body */
            ui_bridge_roundrect(body_x, body_y, body_w, body_h, 2, fg);
            /* Vertical ribs/slats */
            int rib_y = body_y + 3;
            int rib_h = body_h - 6;
            ui_bridge_rect(body_x + body_w / 3, rib_y, 1, rib_h, ui_bridge_blend(fg, 0x000000, 70));
            ui_bridge_rect(body_x + body_w * 2 / 3, rib_y, 1, rib_h, ui_bridge_blend(fg, 0x000000, 70));
            break;
        }
        case ICON_SETTINGS: {
            int r = s / 2;
            ui_bridge_roundrect(x, y, s, s, r, fg);
            /* 4 gear teeth */
            ui_bridge_rect(x + s * 3 / 8, y - 1, s / 4, 3, fg);
            ui_bridge_rect(x + s * 3 / 8, y + s - 2, s / 4, 3, fg);
            ui_bridge_rect(x - 1, y + s * 3 / 8, 3, s / 4, fg);
            ui_bridge_rect(x + s - 2, y + s * 3 / 8, 3, s / 4, fg);
            /* Center hole */
            int hole_sz = s * 3 / 8;
            ui_bridge_roundrect(x + (s - hole_sz) / 2, y + (s - hole_sz) / 2, hole_sz, hole_sz, hole_sz / 2, 0x191427);
            break;
        }
        case ICON_TERMINAL: {
            ui_bridge_roundrect(x, y, s, s, 2, 0x14101e);
            ui_bridge_rect(x, y, s, 3, accent);
            /* Prompt > */
            ui_bridge_rect(x + 3, y + s / 2 - 2, 2, 1, 0x43a047);
            ui_bridge_rect(x + 4, y + s / 2 - 1, 2, 1, 0x43a047);
            ui_bridge_rect(x + 5, y + s / 2, 2, 1, 0x43a047);
            ui_bridge_rect(x + 4, y + s / 2 + 1, 2, 1, 0x43a047);
            ui_bridge_rect(x + 3, y + s / 2 + 2, 2, 1, 0x43a047);
            /* Cursor _ */
            ui_bridge_rect(x + 8, y + s / 2 + 2, s / 4, 1, 0xffffff);
            break;
        }
        case ICON_INFO: {
            ui_bridge_roundrect(x, y, s, s, s / 2, 0x2979ff);
            /* letter i */
            ui_bridge_rect(x + s / 2 - 1, y + s / 4, 2, 2, 0xffffff);
            ui_bridge_rect(x + s / 2 - 1, y + s * 7 / 16, 2, s * 3 / 8, 0xffffff);
            break;
        }
        case ICON_WARNING: {
            ui_bridge_roundrect(x, y, s, s, 3, 0xfb8c00);
            /* exclamation mark ! */
            ui_bridge_rect(x + s / 2 - 1, y + s / 5, 2, s / 2, 0xffffff);
            ui_bridge_rect(x + s / 2 - 1, y + s * 4 / 5, 2, 2, 0xffffff);
            break;
        }
        case ICON_DANGER: {
            ui_bridge_roundrect(x, y, s, s, s / 2, 0xef5350);
            /* Cross X */
            int pad = s / 4;
            int len = s - 2 * pad;
            for (int i = 0; i < len; i++) {
                ui_bridge_rect(x + pad + i, y + pad + i, 2, 1, 0xffffff);
                ui_bridge_rect(x + pad + len - 1 - i, y + pad + i, 2, 1, 0xffffff);
            }
            break;
        }
        case ICON_CHECK: {
            ui_bridge_roundrect(x, y, s, s, s / 2, 0x43a047);
            /* Checkmark */
            int start_x = x + s / 4;
            int start_y = y + s / 2;
            for (int i = 0; i < s / 4; i++) {
                ui_bridge_rect(start_x + i, start_y + i, 2, 2, 0xffffff);
            }
            for (int i = 0; i < s / 2; i++) {
                ui_bridge_rect(start_x + s / 4 + i, start_y + s / 4 - i, 2, 2, 0xffffff);
            }
            break;
        }
        case ICON_CHEVRON_RIGHT: {
            int cx = x + s / 3;
            int cy = y + s / 4;
            int ch = s / 2;
            for (int i = 0; i < ch / 2; i++) {
                ui_bridge_rect(cx + i, cy + i, 2, 1, fg);
                ui_bridge_rect(cx + i, cy + ch - i, 2, 1, fg);
            }
            break;
        }
        case ICON_CLOSE: {
            ui_bridge_roundrect(x, y, s, s, s / 2, 0xef5350);
            int pad = s / 4;
            int len = s - 2 * pad;
            for (int i = 0; i < len; i++) {
                ui_bridge_rect(x + pad + i, y + pad + i, 2, 1, 0xffffff);
                ui_bridge_rect(x + pad + len - 1 - i, y + pad + i, 2, 1, 0xffffff);
            }
            break;
        }
        default:
            break;
    }
}

/* =========================================================================
 * 3. REUSABLE BUTTON COMPONENT
 * ========================================================================= */

void ui_draw_button(int x, int y, int w, int h, const char *label, IconKind icon, UiButtonState state, int is_default, int is_danger) {
    ThemeColors *th = ui_theme();
    u32 bg_color = th->surface_secondary;
    u32 border_color = th->border;
    u32 text_color = th->text;

    if (is_danger) {
        bg_color = (state == UI_BTN_HOVER) ? th->danger_hover : th->danger;
        border_color = th->danger;
        text_color = 0xffffff;
    } else if (is_default) {
        bg_color = (state == UI_BTN_HOVER) ? th->accent_hover : th->accent;
        border_color = th->accent;
        text_color = 0xffffff;
    } else {
        if (state == UI_BTN_HOVER) {
            bg_color = th->surface_elevated;
            border_color = th->accent;
        } else if (state == UI_BTN_PRESSED) {
            bg_color = th->selection;
            border_color = th->accent;
        } else if (state == UI_BTN_DISABLED) {
            bg_color = th->surface;
            border_color = th->border_subtle;
            text_color = th->text_muted;
        }
    }

    /* Base rounded container */
    ui_bridge_roundrect(x, y, w, h, 6, bg_color);

    /* 1px crisp outline */
    ui_bridge_rect(x + 4, y, w - 8, 1, border_color);
    ui_bridge_rect(x + 4, y + h - 1, w - 8, 1, border_color);
    ui_bridge_rect(x, y + 4, 1, h - 8, border_color);
    ui_bridge_rect(x + w - 1, y + 4, 1, h - 8, border_color);

    /* Content placement */
    int icon_sz = (icon != ICON_NONE) ? 14 : 0;
    int tw = label ? ui_bridge_text_width(label, 1) : 0;
    int total_w = tw + (icon_sz ? icon_sz + 6 : 0);
    int start_x = x + (w - total_w) / 2;
    int text_y = y + (h - 14) / 2;

    if (icon != ICON_NONE) {
        ui_draw_icon(icon, start_x, y + (h - icon_sz) / 2, icon_sz, is_default || is_danger ? 0xffffff : th->accent, text_color);
        start_x += icon_sz + 6;
    }
    if (label && label[0]) {
        ui_bridge_text(start_x, text_y, label, text_color, 1);
    }
}

int ui_button_hit(int x, int y, int w, int h, int px, int py) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

/* =========================================================================
 * 4. REUSABLE MENU & CONTEXT MENU SYSTEM
 * ========================================================================= */

UiMenu g_active_menu;

void ui_menu_open(UiMenu *m, int x, int y, const UiMenuItem *items, int count, void (*on_select)(int)) {
    if (!m) return;
    if (count > MAX_MENU_ITEMS) count = MAX_MENU_ITEMS;

    m->item_count = count;
    m->hovered_index = -1;
    m->selected_index = -1;
    m->on_select = on_select;

    int max_tw = 0;
    int max_sw = 0;
    int has_icon = 0;
    int total_h = 8; /* Top and bottom paddings */

    for (int i = 0; i < count; i++) {
        m->items[i] = items[i];
        if (items[i].kind == MENU_ITEM_SEPARATOR) {
            total_h += 8;
            continue;
        }
        total_h += 24;
        if (items[i].icon != ICON_NONE) has_icon = 1;
        int tw = items[i].label ? ui_bridge_text_width(items[i].label, 1) : 0;
        if (tw > max_tw) max_tw = tw;
        int sw = items[i].shortcut ? ui_bridge_text_width(items[i].shortcut, 1) : 0;
        if (sw > max_sw) max_sw = sw;
    }

    int calc_w = max_tw + (has_icon ? 26 : 12) + (max_sw ? max_sw + 24 : 16) + 16;
    if (calc_w < 180) calc_w = 180;
    if (calc_w > 320) calc_w = 320;

    int screen_w = ui_bridge_screen_width();
    int screen_h = ui_bridge_screen_height();

    /* Auto-clamp within screen boundaries */
    if (x + calc_w > screen_w - 6) x = screen_w - calc_w - 6;
    if (y + total_h > screen_h - 6) y = screen_h - total_h - 6;
    if (x < 6) x = 6;
    if (y < 6) y = 6;

    m->x = x;
    m->y = y;
    m->w = calc_w;
    m->h = total_h;
    m->active = 1;
}

void ui_menu_close(UiMenu *m) {
    if (m) m->active = 0;
}

void ui_draw_menu(UiMenu *m) {
    if (!m || !m->active) return;
    ThemeColors *th = ui_theme();

    /* Subtle shadow / perimeter border */
    ui_bridge_roundrect(m->x - 1, m->y - 1, m->w + 2, m->h + 2, 9, th->border);
    /* Elevated surface */
    ui_bridge_roundrect(m->x, m->y, m->w, m->h, 8, th->surface_elevated);

    int cur_y = m->y + 4;
    for (int i = 0; i < m->item_count; i++) {
        UiMenuItem *item = &m->items[i];
        if (item->kind == MENU_ITEM_SEPARATOR) {
            ui_bridge_rect(m->x + 8, cur_y + 3, m->w - 16, 1, th->border_subtle);
            cur_y += 8;
            continue;
        }

        int is_sel = (i == m->selected_index || i == m->hovered_index) && item->enabled;
        if (is_sel) {
            ui_bridge_roundrect(m->x + 4, cur_y, m->w - 8, 22, 4, th->selection);
        }

        u32 text_col = item->enabled ? (is_sel ? th->selection_text : th->text) : th->text_muted;

        /* Icon */
        if (item->icon != ICON_NONE) {
            ui_draw_icon(item->icon, m->x + 8, cur_y + 3, 16, is_sel ? th->accent_hover : th->accent, text_col);
        }

        /* Label */
        int label_x = m->x + (item->icon != ICON_NONE ? 28 : 12);
        if (item->label) {
            ui_bridge_text(label_x, cur_y + 4, item->label, text_col, 1);
        }

        /* Shortcut */
        if (item->shortcut) {
            int sw = ui_bridge_text_width(item->shortcut, 1);
            ui_bridge_text(m->x + m->w - sw - 12, cur_y + 4, item->shortcut, is_sel ? th->selection_text : th->text_secondary, 1);
        }

        /* Submenu indicator */
        if (item->kind == MENU_ITEM_SUBMENU) {
            ui_draw_icon(ICON_CHEVRON_RIGHT, m->x + m->w - 16, cur_y + 4, 12, th->accent, text_col);
        }

        cur_y += 24;
    }
}

int ui_menu_on_key(UiMenu *m, int key, int shift) {
    (void)shift;
    if (!m || !m->active) return 0;

    /* Up Arrow (key 72) */
    if (key == 72) {
        int start = m->selected_index >= 0 ? m->selected_index : m->item_count;
        for (int step = 1; step <= m->item_count; step++) {
            int idx = (start - step + m->item_count) % m->item_count;
            if (m->items[idx].kind != MENU_ITEM_SEPARATOR && m->items[idx].enabled) {
                m->selected_index = idx;
                m->hovered_index = idx;
                return 1;
            }
        }
        return 1;
    }

    /* Down Arrow (key 80) */
    if (key == 80) {
        int start = m->selected_index;
        for (int step = 1; step <= m->item_count; step++) {
            int idx = (start + step) % m->item_count;
            if (m->items[idx].kind != MENU_ITEM_SEPARATOR && m->items[idx].enabled) {
                m->selected_index = idx;
                m->hovered_index = idx;
                return 1;
            }
        }
        return 1;
    }

    /* Enter (key 28) */
    if (key == 28) {
        if (m->selected_index >= 0 && m->selected_index < m->item_count) {
            UiMenuItem *item = &m->items[m->selected_index];
            if (item->enabled && item->kind != MENU_ITEM_SEPARATOR) {
                int action_id = item->id;
                void (*cb)(int) = m->on_select;
                ui_menu_close(m);
                if (cb) cb(action_id);
                return 1;
            }
        }
        return 1;
    }

    /* Escape (key 1) */
    if (key == 1) {
        ui_menu_close(m);
        return 1;
    }

    return 0;
}

int ui_menu_on_mouse_move(UiMenu *m, int mx, int my) {
    if (!m || !m->active) return 0;

    if (mx < m->x || mx >= m->x + m->w || my < m->y || my >= m->y + m->h) {
        m->hovered_index = -1;
        return 0;
    }

    int cur_y = m->y + 4;
    for (int i = 0; i < m->item_count; i++) {
        int item_h = (m->items[i].kind == MENU_ITEM_SEPARATOR) ? 8 : 24;
        if (my >= cur_y && my < cur_y + item_h) {
            if (m->items[i].kind != MENU_ITEM_SEPARATOR && m->items[i].enabled) {
                m->hovered_index = i;
                m->selected_index = i;
            } else {
                m->hovered_index = -1;
            }
            return 1;
        }
        cur_y += item_h;
    }
    m->hovered_index = -1;
    return 1;
}

int ui_menu_on_mouse_down(UiMenu *m, int mx, int my, int button) {
    if (!m || !m->active) return 0;
    (void)button;

    if (mx < m->x || mx >= m->x + m->w || my < m->y || my >= m->y + m->h) {
        ui_menu_close(m);
        return 1; /* Handled: closed menu by clicking outside */
    }

    int cur_y = m->y + 4;
    for (int i = 0; i < m->item_count; i++) {
        int item_h = (m->items[i].kind == MENU_ITEM_SEPARATOR) ? 8 : 24;
        if (my >= cur_y && my < cur_y + item_h) {
            if (m->items[i].kind != MENU_ITEM_SEPARATOR && m->items[i].enabled) {
                int action_id = m->items[i].id;
                void (*cb)(int) = m->on_select;
                ui_menu_close(m);
                if (cb) cb(action_id);
                return 1;
            }
            return 1;
        }
        cur_y += item_h;
    }

    return 1;
}

/* =========================================================================
 * 5. REUSABLE DIALOG SYSTEM
 * ========================================================================= */

UiDialog g_active_dialog;

static void str_copy_limit(char *dest, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
}

void ui_dialog_message(const char *title, const char *msg, IconKind icon, void (*on_close)(int)) {
    UiDialog *d = &g_active_dialog;
    d->kind = DIALOG_MESSAGE;
    str_copy_limit(d->title, title ? title : "Message", sizeof(d->title));
    str_copy_limit(d->message, msg ? msg : "", sizeof(d->message));
    str_copy_limit(d->confirm_label, "OK", sizeof(d->confirm_label));
    d->cancel_label[0] = 0;
    d->is_danger = 0;
    d->icon = (icon != ICON_NONE) ? icon : ICON_INFO;
    d->focused_btn = 1;
    d->on_result = on_close;
    d->active = 1;
}

void ui_dialog_confirm(const char *title, const char *msg, const char *confirm_label, const char *cancel_label, int is_danger, IconKind icon, void (*on_result)(int)) {
    UiDialog *d = &g_active_dialog;
    d->kind = DIALOG_CONFIRM;
    str_copy_limit(d->title, title ? title : "Confirm", sizeof(d->title));
    str_copy_limit(d->message, msg ? msg : "", sizeof(d->message));
    str_copy_limit(d->confirm_label, confirm_label ? confirm_label : "Confirm", sizeof(d->confirm_label));
    str_copy_limit(d->cancel_label, cancel_label ? cancel_label : "Cancel", sizeof(d->cancel_label));
    d->is_danger = is_danger;
    d->icon = (icon != ICON_NONE) ? icon : (is_danger ? ICON_DANGER : ICON_WARNING);
    d->focused_btn = is_danger ? 0 : 1; /* Danger defaults to Cancel for safety */
    d->on_result = on_result;
    d->active = 1;
}

void ui_dialog_close(void) {
    g_active_dialog.active = 0;
}

void ui_draw_dialog(void) {
    UiDialog *d = &g_active_dialog;
    if (!d->active) return;
    ThemeColors *th = ui_theme();

    int screen_w = ui_bridge_screen_width();
    int screen_h = ui_bridge_screen_height();

    int dw = 380;
    int dh = 150;
    int dx = (screen_w - dw) / 2;
    int dy = (screen_h - dh) / 2;

    /* Elevated dialog card with border */
    ui_bridge_roundrect(dx - 1, dy - 1, dw + 2, dh + 2, 11, th->border);
    ui_bridge_roundrect(dx, dy, dw, dh, 10, th->surface_elevated);

    /* Title bar */
    ui_bridge_rect(dx + 12, dy + 32, dw - 24, 1, th->border_subtle);
    ui_bridge_text(dx + 16, dy + 10, d->title, th->text, 1);

    /* Close button [X] at top right */
    ui_bridge_roundrect(dx + dw - 26, dy + 8, 16, 16, 8, th->surface_secondary);
    ui_bridge_rect(dx + dw - 20, dy + 15, 5, 2, th->text_muted);

    /* Icon + Message */
    int icon_size = 28;
    int icon_x = dx + 20;
    int icon_y = dy + 48;
    ui_draw_icon(d->icon, icon_x, icon_y, icon_size, th->accent, th->text);

    int text_x = icon_x + icon_size + 14;
    int text_y = dy + 48;
    /* Draw message text with clean line wrapping */
    const char *p = d->message;
    char line[48];
    int lidx = 0;
    int cur_ty = text_y;
    while (*p && cur_ty < dy + dh - 40) {
        if (*p == '\n') {
            line[lidx] = 0;
            ui_bridge_text(text_x, cur_ty, line, th->text, 1);
            cur_ty += 16;
            lidx = 0;
            p++;
            continue;
        }
        line[lidx++] = *p++;
        if (lidx >= 40) {
            line[lidx] = 0;
            ui_bridge_text(text_x, cur_ty, line, th->text, 1);
            cur_ty += 16;
            lidx = 0;
        }
    }
    if (lidx > 0 && cur_ty < dy + dh - 40) {
        line[lidx] = 0;
        ui_bridge_text(text_x, cur_ty, line, th->text, 1);
    }

    /* Action buttons at bottom right */
    int btn_w = 90;
    int btn_h = 26;
    int btn_y = dy + dh - btn_h - 14;
    int confirm_x = dx + dw - btn_w - 16;

    if (d->kind == DIALOG_CONFIRM) {
        int cancel_x = confirm_x - btn_w - 10;
        ui_draw_button(cancel_x, btn_y, btn_w, btn_h, d->cancel_label, ICON_NONE,
                       d->focused_btn == 0 ? UI_BTN_HOVER : UI_BTN_NORMAL, 0, 0);
        ui_draw_button(confirm_x, btn_y, btn_w, btn_h, d->confirm_label, ICON_NONE,
                       d->focused_btn == 1 ? UI_BTN_HOVER : UI_BTN_NORMAL, !d->is_danger, d->is_danger);
    } else {
        ui_draw_button(confirm_x, btn_y, btn_w, btn_h, d->confirm_label, ICON_NONE,
                       UI_BTN_NORMAL, 1, 0);
    }
}

int ui_dialog_on_key(int key) {
    UiDialog *d = &g_active_dialog;
    if (!d->active) return 0;

    /* Tab (15) or Left/Right (75/77) to toggle focused button */
    if (key == 15 || key == 75 || key == 77) {
        if (d->kind == DIALOG_CONFIRM) {
            d->focused_btn = !d->focused_btn;
            return 1;
        }
        return 1;
    }

    /* Enter (28) */
    if (key == 28) {
        int result = (d->kind == DIALOG_MESSAGE) ? 1 : (d->focused_btn == 1);
        void (*cb)(int) = d->on_result;
        ui_dialog_close();
        if (cb) cb(result);
        return 1;
    }

    /* Escape (1) */
    if (key == 1) {
        void (*cb)(int) = d->on_result;
        ui_dialog_close();
        if (cb) cb(0);
        return 1;
    }

    return 1; /* Dialog consumes all key events while modal */
}

int ui_dialog_on_mouse_move(int mx, int my) {
    UiDialog *d = &g_active_dialog;
    if (!d->active) return 0;

    int screen_w = ui_bridge_screen_width();
    int screen_h = ui_bridge_screen_height();
    int dw = 380;
    int dh = 150;
    int dx = (screen_w - dw) / 2;
    int dy = (screen_h - dh) / 2;

    int btn_w = 90;
    int btn_h = 26;
    int btn_y = dy + dh - btn_h - 14;
    int confirm_x = dx + dw - btn_w - 16;
    int cancel_x = confirm_x - btn_w - 10;

    if (d->kind == DIALOG_CONFIRM) {
        if (ui_button_hit(cancel_x, btn_y, btn_w, btn_h, mx, my)) {
            d->focused_btn = 0;
            return 1;
        }
        if (ui_button_hit(confirm_x, btn_y, btn_w, btn_h, mx, my)) {
            d->focused_btn = 1;
            return 1;
        }
    }
    return (mx >= dx && mx < dx + dw && my >= dy && my < dy + dh);
}

int ui_dialog_on_mouse_down(int mx, int my, int button) {
    UiDialog *d = &g_active_dialog;
    if (!d->active) return 0;
    (void)button;

    int screen_w = ui_bridge_screen_width();
    int screen_h = ui_bridge_screen_height();
    int dw = 380;
    int dh = 150;
    int dx = (screen_w - dw) / 2;
    int dy = (screen_h - dh) / 2;

    /* Top right close button */
    if (mx >= dx + dw - 28 && mx < dx + dw - 8 && my >= dy + 6 && my < dy + 26) {
        void (*cb)(int) = d->on_result;
        ui_dialog_close();
        if (cb) cb(0);
        return 1;
    }

    int btn_w = 90;
    int btn_h = 26;
    int btn_y = dy + dh - btn_h - 14;
    int confirm_x = dx + dw - btn_w - 16;

    if (d->kind == DIALOG_CONFIRM) {
        int cancel_x = confirm_x - btn_w - 10;
        if (ui_button_hit(cancel_x, btn_y, btn_w, btn_h, mx, my)) {
            void (*cb)(int) = d->on_result;
            ui_dialog_close();
            if (cb) cb(0);
            return 1;
        }
        if (ui_button_hit(confirm_x, btn_y, btn_w, btn_h, mx, my)) {
            void (*cb)(int) = d->on_result;
            ui_dialog_close();
            if (cb) cb(1);
            return 1;
        }
    } else {
        if (ui_button_hit(confirm_x, btn_y, btn_w, btn_h, mx, my)) {
            void (*cb)(int) = d->on_result;
            ui_dialog_close();
            if (cb) cb(1);
            return 1;
        }
    }

    return 1; /* Dialog captures clicks within modal area */
}

/* =========================================================================
 * 6. NOTIFICATION TOAST SYSTEM
 * ========================================================================= */

UiNotification g_notifications[MAX_NOTIFICATIONS];

extern u32 wm_time_ms(void);

void ui_notify(const char *title, const char *message, IconKind icon) {
    int slot = -1;
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        if (!g_notifications[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) slot = 0; /* Recycle oldest */

    UiNotification *n = &g_notifications[slot];
    str_copy_limit(n->title, title ? title : "Notification", sizeof(n->title));
    str_copy_limit(n->message, message ? message : "", sizeof(n->message));
    n->icon = (icon != ICON_NONE) ? icon : ICON_INFO;
    n->start_ms = wm_time_ms();
    n->duration_ms = 3500;
    n->active = 1;
}

void ui_draw_notifications(u32 now_ms) {
    ThemeColors *th = ui_theme();
    int screen_w = ui_bridge_screen_width();
    int nw = 280;
    int nh = 58;

    int drawn_count = 0;
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        UiNotification *n = &g_notifications[i];
        if (!n->active) continue;

        if (now_ms - n->start_ms > n->duration_ms) {
            n->active = 0;
            continue;
        }

        int nx = screen_w - nw - 16;
        int ny = 38 + drawn_count * (nh + 10);

        /* Notification glass container */
        ui_bridge_roundrect(nx - 1, ny - 1, nw + 2, nh + 2, 9, th->border);
        ui_bridge_roundrect(nx, ny, nw, nh, 8, th->surface_elevated);

        /* Icon */
        ui_draw_icon(n->icon, nx + 12, ny + (nh - 24) / 2, 24, th->accent, th->text);

        /* Title */
        ui_bridge_text(nx + 44, ny + 10, n->title, th->text, 1);

        /* Message */
        ui_bridge_text(nx + 44, ny + 30, n->message, th->text_secondary, 1);

        drawn_count++;
    }
}
