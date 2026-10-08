#include "ui.h"
#include "ui_icons.h"
#include "gui/apps.h"

/* =========================================================================
 * 1. THEME PALETTES & METRICS
 * ========================================================================= */

static ThemeColors g_dark_theme = {
    .background         = 0x0a0c14,
    .surface            = 0x121520,
    .surface_secondary  = 0x181c2b,
    .surface_elevated   = 0x20263a,
    .border             = 0x2c344a,
    .border_subtle      = 0x1c2130,
    .text               = 0xf1f5f9,
    .text_secondary     = 0x94a3b8,
    .text_muted         = 0x64748b,
    .accent             = 0x6366f1,
    .accent_hover       = 0x818cf8,
    .selection          = 0x2b3552,
    .selection_text     = 0xffffff,
    .danger             = 0xef4444,
    .danger_hover       = 0xf87171,
    .success            = 0x22c55e,
    .warning            = 0xf59e0b
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

static ThemeMode g_current_mode = THEME_LIGHT;
static int g_current_accent = 0;

typedef struct {
    u32 dark_accent, dark_hover, dark_selection;
    u32 light_accent, light_hover, light_selection;
} AccentPreset;

static const AccentPreset g_accent_presets[5] = {
    {0x2563eb, 0x3b82f6, 0x1d3557, 0x2563eb, 0x3b82f6, 0xdbeafe}, /* Ocean Blue */
    {0x7c3aed, 0x8b5cf6, 0x311b58, 0x7c3aed, 0x8b5cf6, 0xede9fe}, /* Violet Indigo */
    {0x059669, 0x10b981, 0x064e3b, 0x059669, 0x10b981, 0xd1fae5}, /* Emerald Green */
    {0xd97706, 0xf59e0b, 0x78350f, 0xd97706, 0xf59e0b, 0xfef3c7}, /* Amber Orange */
    {0xe11d48, 0xf43f5e, 0x881337, 0xe11d48, 0xf43f5e, 0xffe4e6}  /* Rose Pink */
};

void ui_init(void) {
    g_current_mode = THEME_LIGHT;
    ui_set_accent_index(0);
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

extern void wm_invalidate_all(void);
extern void compositor_invalidate_all_surfaces(void);
extern void compositor_invalidate_dock(void);
extern void request_scene_redraw(void);
extern void request_partial_redraw(int x, int y, int w, int h);
extern void app_host_set_theme(int value);

void ui_set_theme_mode(ThemeMode mode) {
    if (g_current_mode == mode) return;
    g_current_mode = mode;
    app_host_set_theme(mode == THEME_DARK ? 0 : 1);
    compositor_invalidate_all_surfaces();
    wm_invalidate_all();
    compositor_invalidate_dock();
    request_scene_redraw();
}

void ui_toggle_theme_mode(void) {
    ui_set_theme_mode((g_current_mode == THEME_DARK) ? THEME_LIGHT : THEME_DARK);
}

int ui_is_dark(void) {
    return g_current_mode == THEME_DARK;
}

void ui_set_accent_index(int index) {
    if (index < 0 || index >= 5) index = 0;
    g_current_accent = index;
    const AccentPreset *p = &g_accent_presets[index];
    g_dark_theme.accent = p->dark_accent;
    g_dark_theme.accent_hover = p->dark_hover;
    g_dark_theme.selection = p->dark_selection;

    g_light_theme.accent = p->light_accent;
    g_light_theme.accent_hover = p->light_hover;
    g_light_theme.selection = p->light_selection;

    compositor_invalidate_all_surfaces();
    wm_invalidate_all();
    compositor_invalidate_dock();
    request_scene_redraw();
}

int ui_get_accent_index(void) {
    return g_current_accent;
}

u32 ui_accent_color(void) {
    return ui_theme()->accent;
}

u32 ui_accent_hover(void) {
    return ui_theme()->accent_hover;
}

/* =========================================================================
 * 2. SYSTEM ICONS (PROCEDURAL VECTOR ENGINE)
 * ========================================================================= */

void ui_draw_icon(IconKind kind, int x, int y, int size, u32 accent, u32 fg) {
    int asset=-1;
    switch(kind) {
        case ICON_FOLDER:asset=UI_ICON_FOLDER;break;
        case ICON_FILE:case ICON_TEXT:case ICON_IMAGE:asset=UI_ICON_FILE;break;
        case ICON_APP:case ICON_INFO:asset=UI_ICON_WELCOME;break;
        case ICON_TRASH:asset=UI_ICON_TRASH;break;
        case ICON_SETTINGS:asset=UI_ICON_SETTINGS;break;
        case ICON_TERMINAL:asset=UI_ICON_TERMINAL;break;
        default:break;
    }
    if(asset>=0){ui_icon_draw(asset,x,y,size);return;}
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
            int is_dark = ui_is_dark();
            int px = x + s / 6;
            int pw = s * 2 / 3;
            int fold = s / 4;
            u32 paper_bg = is_dark ? 0x242b3b : 0xffffff;
            u32 paper_border = is_dark ? 0x475569 : 0xcbd5e1;
            ui_bridge_roundrect(px, y, pw, s, 2, paper_bg);
            ui_bridge_rect(px, y, pw, 1, paper_border);
            ui_bridge_rect(px, y + s - 1, pw, 1, paper_border);
            ui_bridge_rect(px, y, 1, s, paper_border);
            ui_bridge_rect(px + pw - 1, y, 1, s, paper_border);
            /* Folded corner */
            ui_bridge_rect(px + pw - fold, y, fold, fold, is_dark ? 0x334155 : 0xe2e8f0);
            ui_bridge_rect(px + pw - fold, y + fold, fold, 1, paper_border);
            ui_bridge_rect(px + pw - fold, y, 1, fold, paper_border);
            break;
        }
        case ICON_TEXT: {
            int is_dark = ui_is_dark();
            int px = x + s / 6;
            int pw = s * 2 / 3;
            int fold = s / 4;
            u32 paper_bg = is_dark ? 0x242b3b : 0xffffff;
            u32 paper_border = is_dark ? 0x475569 : 0xcbd5e1;
            ui_bridge_roundrect(px, y, pw, s, 2, paper_bg);
            ui_bridge_rect(px, y, pw, 1, paper_border);
            ui_bridge_rect(px, y + s - 1, pw, 1, paper_border);
            ui_bridge_rect(px, y, 1, s, paper_border);
            ui_bridge_rect(px + pw - 1, y, 1, s, paper_border);
            /* Folded corner */
            ui_bridge_rect(px + pw - fold, y, fold, fold, is_dark ? 0x334155 : 0xe2e8f0);
            ui_bridge_rect(px + pw - fold, y + fold, fold, 1, paper_border);
            ui_bridge_rect(px + pw - fold, y, 1, fold, paper_border);
            /* Text lines */
            int line_w = pw - s / 3;
            int lx = px + 2;
            u32 line_color = accent ? accent : (is_dark ? 0xa78bfa : 0x7c3aed);
            for (int i = 0; i < 3; i++) {
                int ly = y + s / 3 + i * (s / 5);
                int cur_w = (i == 2) ? line_w * 2 / 3 : line_w;
                ui_bridge_rect(lx, ly, cur_w, 1, line_color);
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
            int is_dark = ui_is_dark();
            u32 frame_bg = is_dark ? 0x1e293b : 0xffffff;
            u32 frame_border = is_dark ? 0x475569 : 0xcbd5e1;
            ui_bridge_roundrect(x, y, s, s, 2, frame_bg);
            ui_bridge_rect(x, y, s, 1, frame_border);
            ui_bridge_rect(x, y + s - 1, s, 1, frame_border);
            ui_bridge_rect(x, y, 1, s, frame_border);
            ui_bridge_rect(x + s - 1, y, 1, s, frame_border);
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
            int is_dark = ui_is_dark();
            u32 disk_bg = is_dark ? 0x334155 : 0x475569;
            ui_bridge_roundrect(x, y, s, s, 3, disk_bg);
            int slot_w = s * 3 / 4;
            int slot_h = s / 3;
            ui_bridge_roundrect(x + (s - slot_w) / 2, y + s / 6, slot_w, slot_h, 2, is_dark ? 0x1e293b : 0xffffff);
            /* Activity LED */
            ui_bridge_rect(x + s * 3 / 4, y + s * 3 / 4, s > 20 ? 3 : 2, s > 20 ? 3 : 2, 0x43a047);
            break;
        }
        case ICON_TRASH: {
            int is_dark = ui_is_dark();
            u32 can_col = accent ? accent : (is_dark ? 0xef4444 : 0xd32f2f);
            int body_x = x + s / 5;
            int body_w = s * 3 / 5;
            int body_y = y + s / 4;
            int body_h = s * 3 / 4;
            /* Lid */
            ui_bridge_rect(x + 1, y + s / 6, s - 2, 2, can_col);
            ui_bridge_rect(x + s * 3 / 8, y + s / 10, s / 4, 2, can_col);
            /* Can body */
            ui_bridge_roundrect(body_x, body_y, body_w, body_h, 2, can_col);
            /* Vertical ribs */
            int rib_y = body_y + 3;
            int rib_h = body_h - 6;
            u32 rib_col = is_dark ? 0x1f2937 : 0xffffff;
            ui_bridge_rect(body_x + body_w / 3, rib_y, 1, rib_h, rib_col);
            ui_bridge_rect(body_x + body_w * 2 / 3, rib_y, 1, rib_h, rib_col);
            break;
        }
        case ICON_SETTINGS: {
            int is_dark = ui_is_dark();
            u32 gear_col = accent ? accent : (is_dark ? 0xa78bfa : 0x6366f1);
            u32 hole_col = is_dark ? 0x171d2b : 0xffffff;
            /* Gear central body */
            ui_bridge_roundrect(x + 2, y + 2, s - 4, s - 4, (s - 4) / 2, gear_col);
            /* 4 cardinal teeth */
            ui_bridge_rect(x + s / 2 - 2, y, 4, 3, gear_col);
            ui_bridge_rect(x + s / 2 - 2, y + s - 3, 4, 3, gear_col);
            ui_bridge_rect(x, y + s / 2 - 2, 3, 4, gear_col);
            ui_bridge_rect(x + s - 3, y + s / 2 - 2, 3, 4, gear_col);
            /* 4 diagonal teeth */
            ui_bridge_rect(x + 2, y + 2, 2, 2, gear_col);
            ui_bridge_rect(x + s - 4, y + 2, 2, 2, gear_col);
            ui_bridge_rect(x + 2, y + s - 4, 2, 2, gear_col);
            ui_bridge_rect(x + s - 4, y + s - 4, 2, 2, gear_col);
            /* Center hole with sharp contrast */
            int hole_sz = s / 3 + 1;
            ui_bridge_roundrect(x + (s - hole_sz) / 2, y + (s - hole_sz) / 2, hole_sz, hole_sz, hole_sz / 2, hole_col);
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
        case ICON_THEME: {
            int is_dark = ui_is_dark();
            /* Two-tone Sun & Moon / Dark & Light Mode toggle glyph */
            int cx = x + s / 2;
            int cy = y + s / 2;
            int r = s / 2 - 1;
            u32 ring_col = is_dark ? 0x475569 : 0x94a3b8;
            ui_bridge_roundrect(cx - r, cy - r, r * 2, r * 2, r, ring_col);
            int inner_r = r - 1;
            for (int dy = -inner_r; dy <= inner_r; dy++) {
                for (int dx = -inner_r; dx <= inner_r; dx++) {
                    if (dx * dx + dy * dy <= inner_r * inner_r) {
                        if (dx < 0) {
                            /* Left half: Dark night */
                            ui_bridge_rect(cx + dx, cy + dy, 1, 1, 0x1e293b);
                        } else if (dx > 0) {
                            /* Right half: Daylight amber */
                            ui_bridge_rect(cx + dx, cy + dy, 1, 1, 0xf59e0b);
                        } else {
                            /* Center dividing line */
                            ui_bridge_rect(cx + dx, cy + dy, 1, 1, 0x64748b);
                        }
                    }
                }
            }
            if (s >= 14) {
                ui_bridge_rect(cx - 3, cy - 2, 2, 2, 0xe2e8f0);
                ui_bridge_rect(cx + 2, cy - 2, 2, 2, 0xffffff);
            }
            break;
        }
        case ICON_PIN: {
            int hw = s / 2, hh = s / 3;
            ui_bridge_roundrect(x + (s - hw) / 2, y + 1, hw, hh, 2, accent);
            ui_bridge_rect(x + s / 6, y + 1 + hh, s * 2 / 3, 2, ui_bridge_blend(accent, 0xffffff, 90));
            int bw = s / 3;
            ui_bridge_rect(x + (s - bw) / 2, y + 3 + hh, bw, s / 4, accent);
            int needle_h = s - (3 + hh + s / 4) - 1;
            if (needle_h > 0) ui_bridge_rect(x + s / 2 - 1, y + 3 + hh + s / 4, 2, needle_h, fg);
            break;
        }
        case ICON_RENAME: {
            int pw = s - 4;
            ui_bridge_roundrect(x + 2, y + 2, pw, 3, 1, accent);
            ui_bridge_rect(x + s - 4, y + 2, 2, 2, 0xfb8c00);
            ui_bridge_rect(x + 2, y + s - 4, pw, 2, fg);
            break;
        }
        case ICON_COPY: {
            int is_dark = ui_is_dark();
            int doc_w = s * 7 / 12, doc_h = s * 7 / 10;
            u32 doc_bg1 = is_dark ? 0x1e293b : 0xe2e8f0;
            u32 doc_bg2 = is_dark ? 0x334155 : 0xffffff;
            u32 border = is_dark ? 0x475569 : 0xcbd5e1;
            ui_bridge_roundrect(x + s - doc_w, y, doc_w, doc_h, 2, doc_bg1);
            ui_bridge_rect(x + s - doc_w, y, doc_w, 1, border);
            ui_bridge_roundrect(x, y + s - doc_h, doc_w, doc_h, 2, doc_bg2);
            ui_bridge_rect(x, y + s - doc_h, doc_w, 1, border);
            ui_bridge_rect(x, y + s - 1, doc_w, 1, border);
            ui_bridge_rect(x, y + s - doc_h, 1, doc_h, border);
            ui_bridge_rect(x + doc_w - 1, y + s - doc_h, 1, doc_h, border);
            break;
        }
        case ICON_CUT: {
            int cx = x + s / 2, cy = y + s / 3;
            for (int d = 0; d < s / 3; d++) {
                ui_bridge_rect(cx - d, cy - d, 2, 1, fg);
                ui_bridge_rect(cx + d, cy - d, 2, 1, fg);
            }
            int rsz = s / 3;
            ui_bridge_roundrect(x + 2, y + s - rsz - 1, rsz, rsz, rsz / 2, accent);
            ui_bridge_roundrect(x + s - rsz - 2, y + s - rsz - 1, rsz, rsz, rsz / 2, accent);
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

    if (state == UI_BTN_DISABLED) {
        bg_color = th->surface;
        border_color = th->border_subtle;
        text_color = th->text_muted;
    } else if (is_danger) {
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
        }
    }
    if (state == UI_BTN_PRESSED && (is_default || is_danger))
        bg_color = ui_bridge_blend(bg_color, 0x000000, 38);

    /* Full rounded stroke (ring colour + interior) so corners never fade. */
    ui_bridge_roundrect_stroke(x, y, w, h, UI_RADIUS_SMALL, 1, border_color, bg_color);
    /* A single inset highlight gives raised controls depth without a blur or
     * per-pixel gradient. Pressed/disabled controls deliberately stay flat. */
    if (state != UI_BTN_PRESSED && state != UI_BTN_DISABLED &&
        w > UI_RADIUS_SMALL * 2 + 2 && h > 4)
        ui_bridge_rect(x + UI_RADIUS_SMALL + 1, y + 1,
                       w - UI_RADIUS_SMALL * 2 - 2, 1,
                       ui_bridge_blend(bg_color, 0xffffff, ui_is_dark() ? 30 : 100));

    /* Content placement (nudged down when pressed). */
    int press = (state == UI_BTN_PRESSED) ? 1 : 0;
    int icon_sz = (icon != ICON_NONE) ? 14 : 0;
    int tw = label ? ui_bridge_text_width(label, 1) : 0;
    int total_w = tw + (icon_sz ? icon_sz + 6 : 0);
    int start_x = x + (w - total_w) / 2;
    int text_y = y + (h - 14) / 2 + press;

    if (icon != ICON_NONE) {
        ui_draw_icon(icon, start_x, y + (h - icon_sz) / 2 + press, icon_sz,
                     state == UI_BTN_DISABLED ? th->text_muted :
                     is_default || is_danger ? 0xffffff : th->accent, text_color);
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

static void ui_menu_invalidate(const UiMenu *m) {
    if (!m || m->w <= 0 || m->h <= 0) return;
    request_partial_redraw(m->x - 4, m->y - 3, m->w + 8, m->h + 10);
}

void ui_menu_open(UiMenu *m, int x, int y, const UiMenuItem *items, int count, void (*on_select)(int)) {
    if (!m) return;
    /* A modal dialog owns the screen: never stack a menu on top of it. This
     * is what previously left the desktop stuck after right-clicking with the
     * About card open. */
    if (g_active_dialog.active) return;
    if (m->active) ui_menu_invalidate(m);
    if (count > MAX_MENU_ITEMS) count = MAX_MENU_ITEMS;

    m->item_count = count;
    m->hovered_index = -1;
    m->selected_index = -1;
    m->on_select = on_select;

    int max_tw = 0;
    int max_sw = 0;
    int has_icon = 0;
    int total_h = 12; /* Top and bottom paddings */

    for (int i = 0; i < count; i++) {
        m->items[i] = items[i];
        if (items[i].kind == MENU_ITEM_SEPARATOR) {
            total_h += 9;
            continue;
        }
        total_h += 34;
        if (items[i].icon != ICON_NONE) has_icon = 1;
        int tw = items[i].label ? ui_bridge_text_width(items[i].label, 2) : 0;
        if (tw > max_tw) max_tw = tw;
        int sw = items[i].shortcut ? ui_bridge_text_width(items[i].shortcut, 2) : 0;
        if (sw > max_sw) max_sw = sw;
    }

    int calc_w = max_tw + (has_icon ? 26 : 12) + (max_sw ? max_sw + 24 : 16) + 16;
    if (calc_w < 180) calc_w = 180;
    if (calc_w > 440) calc_w = 440;

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
    ui_menu_invalidate(m);
}

void ui_menu_close(UiMenu *m) {
    if (m && m->active) {
        ui_menu_invalidate(m);
        m->active = 0;
    }
}

void ui_draw_menu(UiMenu *m) {
    if (!m || !m->active) return;
    ThemeColors *th = ui_theme();
    int is_dark = ui_is_dark();

    /* A single restrained shadow keeps the menu crisp and inexpensive to draw. */
    ui_bridge_rounded(m->x - 2, m->y + 2, m->w + 4, m->h + 4, 12, 0x080610, 70);

    /* Themed raised surface, fine outline, and a subtle top-edge highlight. */
    ui_bridge_roundrect(m->x - 1, m->y - 1, m->w + 2, m->h + 2, 12, th->border);
    u32 menu_bg = th->surface_elevated;
    ui_bridge_roundrect(m->x, m->y, m->w, m->h, 11, menu_bg);
    ui_bridge_rect(m->x + 12, m->y + 1, m->w - 24, 1,
                   is_dark ? 0x343b52 : 0xffffff);

    int cur_y = m->y + 6;
    for (int i = 0; i < m->item_count; i++) {
        UiMenuItem *item = &m->items[i];
        if (item->kind == MENU_ITEM_SEPARATOR) {
            ui_bridge_rect(m->x + 12, cur_y + 4, m->w - 24, 1, th->border_subtle);
            cur_y += 9;
            continue;
        }

        int is_sel = (i == m->selected_index || i == m->hovered_index) && item->enabled;
        if (is_sel) {
            ui_bridge_roundrect(m->x + 6, cur_y + 1, m->w - 12, 32, 8, th->selection);
        }

        u32 text_col = item->enabled ? (is_sel ? th->selection_text : th->text) : th->text_muted;

        /* Icon */
        if (item->icon != ICON_NONE) {
            u32 ico_accent = th->accent;
            if (item->icon == ICON_FOLDER) ico_accent = 0x3892e6;
            else if (item->icon == ICON_TEXT) ico_accent = 0x8b5cf6;
            else if (item->icon == ICON_TRASH || item->icon == ICON_CLOSE) ico_accent = 0xef4444;
            else if (item->icon == ICON_THEME) ico_accent = 0xf59e0b;
            else if (item->icon == ICON_SETTINGS) ico_accent = is_dark ? 0xa78bfa : 0x6366f1;
            else if (item->icon == ICON_INFO) ico_accent = 0x0284c7;
            else if (item->icon == ICON_PIN) ico_accent = 0xa855f7;
            else if (item->icon == ICON_RENAME) ico_accent = 0x10b981;
            else if (item->icon == ICON_COPY || item->icon == ICON_CUT) ico_accent = 0x06b6d4;
            if (!item->enabled) ico_accent = th->text_muted;
            u32 icon_tile = is_dark ? ui_bridge_blend(menu_bg, ico_accent, is_sel ? 54 : 32)
                                    : ui_bridge_blend(0xffffff, ico_accent, is_sel ? 48 : 24);
            ui_bridge_roundrect(m->x + 8, cur_y + 6, 22, 22, 7, icon_tile);
            ui_draw_icon(item->icon, m->x + 11, cur_y + 9, 16, ico_accent, text_col);
        }

        /* Label */
        int label_x = m->x + (item->icon != ICON_NONE ? 38 : 14);
        if (item->label) {
            ui_bridge_text(label_x, cur_y + 7, item->label, text_col, 2);
        }

        /* Shortcut */
        if (item->shortcut) {
            int sw = ui_bridge_text_width(item->shortcut, 2);
            ui_bridge_text(m->x + m->w - sw - 14, cur_y + 7, item->shortcut, is_sel ? text_col : th->text_secondary, 2);
        }

        /* Submenu indicator */
        if (item->kind == MENU_ITEM_SUBMENU) {
            ui_draw_icon(ICON_CHEVRON_RIGHT, m->x + m->w - 17, cur_y + 7, 12, th->accent, text_col);
        }

        cur_y += 34;
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
    int old_hovered = m->hovered_index;
    int old_selected = m->selected_index;

    if (mx < m->x || mx >= m->x + m->w || my < m->y || my >= m->y + m->h) {
        m->hovered_index = -1;
        return old_hovered != m->hovered_index;
    }

    int cur_y = m->y + 6;
    for (int i = 0; i < m->item_count; i++) {
        int item_h = (m->items[i].kind == MENU_ITEM_SEPARATOR) ? 9 : 34;
        if (my >= cur_y && my < cur_y + item_h) {
            if (m->items[i].kind != MENU_ITEM_SEPARATOR && m->items[i].enabled) {
                m->hovered_index = i;
                m->selected_index = i;
            } else {
                m->hovered_index = -1;
            }
            return old_hovered != m->hovered_index || old_selected != m->selected_index;
        }
        cur_y += item_h;
    }
    m->hovered_index = -1;
    return old_hovered != m->hovered_index;
}

int ui_menu_on_mouse_down(UiMenu *m, int mx, int my, int button) {
    if (!m || !m->active) return 0;
    (void)button;

    if (mx < m->x || mx >= m->x + m->w || my < m->y || my >= m->y + m->h) {
        ui_menu_close(m);
        return 1; /* Handled: closed menu by clicking outside */
    }

    int cur_y = m->y + 6;
    for (int i = 0; i < m->item_count; i++) {
        int item_h = (m->items[i].kind == MENU_ITEM_SEPARATOR) ? 9 : 34;
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

/* Repaint the whole dialog card after it opens or moves. Without this the
 * About card could stay invisible: the menu close only damaged the menu rect,
 * and the dialog region was never queued. */
static void dialog_request_redraw(void) {
    int x, y, w, h;
    ui_dialog_bounds(&x, &y, &w, &h);
    request_partial_redraw(x - 10, y - 10, w + 20, h + 20);
}

static void str_copy_limit(char *dest, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
}

static int dialog_app=-1;
static void dialog_result(void (*callback)(int),int result){
    int previous=gui_app_context_enter(dialog_app);if(callback)callback(result);gui_app_context_leave(previous);
}
static void dialog_input_result(void (*callback)(const char *),const char *text){
    int previous=gui_app_context_enter(dialog_app);if(callback)callback(text);gui_app_context_leave(previous);
}
void ui_dialog_message(const char *title, const char *msg, IconKind icon, void (*on_close)(int)) {
    dialog_app=gui_app_context_current();
    UiDialog *d = &g_active_dialog;
    ui_menu_close(&g_active_menu);
    d->kind = DIALOG_MESSAGE;
    str_copy_limit(d->title, title ? title : "Message", sizeof(d->title));
    str_copy_limit(d->message, msg ? msg : "", sizeof(d->message));
    str_copy_limit(d->confirm_label, "OK", sizeof(d->confirm_label));
    d->cancel_label[0] = 0;
    d->is_danger = 0;
    d->icon = (icon != ICON_NONE) ? icon : ICON_INFO;
    d->focused_btn = 1;
    d->on_result = on_close;
    d->on_input_result = NULL;
    d->x = -1; d->y = -1; d->dragging = 0;
    d->active = 1;
    dialog_request_redraw();
}

void ui_dialog_confirm(const char *title, const char *msg, const char *confirm_label, const char *cancel_label, int is_danger, IconKind icon, void (*on_result)(int)) {
    dialog_app=gui_app_context_current();
    UiDialog *d = &g_active_dialog;
    ui_menu_close(&g_active_menu);
    d->kind = DIALOG_CONFIRM;
    str_copy_limit(d->title, title ? title : "Confirm", sizeof(d->title));
    str_copy_limit(d->message, msg ? msg : "", sizeof(d->message));
    str_copy_limit(d->confirm_label, confirm_label ? confirm_label : "Confirm", sizeof(d->confirm_label));
    str_copy_limit(d->cancel_label, cancel_label ? cancel_label : "Cancel", sizeof(d->cancel_label));
    d->is_danger = is_danger;
    d->icon = (icon != ICON_NONE) ? icon : (is_danger ? ICON_DANGER : ICON_WARNING);
    d->focused_btn = is_danger ? 0 : 1; /* Danger defaults to Cancel for safety */
    d->on_result = on_result;
    d->on_input_result = NULL;
    d->x = -1; d->y = -1; d->dragging = 0;
    d->active = 1;
    dialog_request_redraw();
}

void ui_dialog_input(const char *title, const char *msg, const char *initial_val, IconKind icon, void (*on_input_result)(const char *)) {
    dialog_app=gui_app_context_current();
    UiDialog *d = &g_active_dialog;
    ui_menu_close(&g_active_menu);
    d->kind = DIALOG_INPUT;
    str_copy_limit(d->title, title ? title : "Input", sizeof(d->title));
    str_copy_limit(d->message, msg ? msg : "", sizeof(d->message));
    str_copy_limit(d->confirm_label, "OK", sizeof(d->confirm_label));
    str_copy_limit(d->cancel_label, "Cancel", sizeof(d->cancel_label));
    d->is_danger = 0;
    d->icon = (icon != ICON_NONE) ? icon : ICON_FILE;
    d->focused_btn = 1;
    d->on_result = NULL;
    d->on_input_result = on_input_result;
    d->input_len = 0;
    if (initial_val) {
        while (initial_val[d->input_len] && d->input_len < (int)sizeof(d->input_text) - 1) {
            d->input_text[d->input_len] = initial_val[d->input_len];
            d->input_len++;
        }
    }
    d->input_text[d->input_len] = 0;
    d->input_cursor = d->input_len;
    d->input_anchor = -1;
    d->x = -1; d->y = -1; d->dragging = 0;
    d->active = 1;
    dialog_request_redraw();
}

void ui_dialog_close(void) {
    if (g_active_dialog.active) dialog_request_redraw();
    g_active_dialog.active = 0;
    g_active_dialog.dragging = 0;
}

/* Resolve the dialog card rectangle, honouring a dragged position and keeping
 * the whole card on screen. Shared by drawing, hit-testing and invalidation. */
static void dialog_rect(const UiDialog *d, int *dx, int *dy, int *dw, int *dh) {
    int sw = ui_bridge_screen_width(), sh = ui_bridge_screen_height();
    int about = !memcmp(d->title,"About PollikOS",sizeof "About PollikOS");
    int w = 380, h = about ? 212 : (d->kind == DIALOG_INPUT) ? 168 : 150;
    int x = d->x >= 0 ? d->x : (sw - w) / 2;
    int y = d->y >= 0 ? d->y : (sh - h) / 2;
    if (x < 4) x = 4;
    if (y < 4) y = 4;
    if (x + w > sw - 4) x = sw - w - 4;
    if (y + h > sh - 4) y = sh - h - 4;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    *dx = x; *dy = y; *dw = w; *dh = h;
}

void ui_dialog_bounds(int *out_x, int *out_y, int *out_w, int *out_h) {
    int dx, dy, dw, dh;
    dialog_rect(&g_active_dialog, &dx, &dy, &dw, &dh);
    if (out_x) *out_x = dx;
    if (out_y) *out_y = dy;
    if (out_w) *out_w = dw;
    if (out_h) *out_h = dh;
}

void ui_draw_dialog(void) {
    UiDialog *d = &g_active_dialog;
    if (!d->active) return;
    ThemeColors *th = ui_theme();

    int dx, dy, dw, dh;
    dialog_rect(d, &dx, &dy, &dw, &dh);

    /* Ambient drop shadow */
    ui_bridge_rounded(dx - 3, dy + 3, dw + 6, dh + 6, 14, 0x080610, 50);
    ui_bridge_rounded(dx - 1, dy + 1, dw + 2, dh + 2, 12, 0x080610, 75);

    /* Elevated dialog card with border */
    ui_bridge_roundrect(dx - 1, dy - 1, dw + 2, dh + 2, 11, th->border);
    ui_bridge_roundrect(dx, dy, dw, dh, 10, th->surface_elevated);

    /* Title bar */
    ui_bridge_rect(dx + 12, dy + 32, dw - 24, 1, th->border_subtle);
    ui_bridge_text(dx + 16, dy + 8, d->title, th->text, 2);

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
    int about = !memcmp(d->title,"About PollikOS",sizeof "About PollikOS");
    int message_scale=about?2:1, line_height=about?26:16;
    const char *p = d->message;
    char line[48];
    int lidx = 0;
    int cur_ty = text_y;
    while (*p && cur_ty < dy + dh - 40) {
        if (*p == '\n') {
            line[lidx] = 0;
            ui_bridge_text(text_x, cur_ty, line, th->text, message_scale);
            cur_ty += line_height;
            lidx = 0;
            p++;
            continue;
        }
        line[lidx++] = *p++;
        if (lidx >= (about?22:40)) {
            line[lidx] = 0;
            ui_bridge_text(text_x, cur_ty, line, th->text, message_scale);
            cur_ty += line_height;
            lidx = 0;
        }
    }
    if (lidx > 0 && cur_ty < dy + dh - 40) {
        line[lidx] = 0;
        ui_bridge_text(text_x, cur_ty, line, th->text, message_scale);
    }

    /* Input text box for DIALOG_INPUT */
    if (d->kind == DIALOG_INPUT) {
        int in_x = dx + 20;
        int in_y = dy + 84;
        int in_w = dw - 40;
        int in_h = 28;
        ui_bridge_roundrect(in_x - 1, in_y - 1, in_w + 2, in_h + 2, 7, th->accent);
        ui_bridge_roundrect(in_x, in_y, in_w, in_h, 6, th->surface);
        if (d->input_anchor >= 0 && d->input_anchor != d->input_cursor) {
            int a = d->input_anchor, b = d->input_cursor;
            if (a > b) { int t = a; a = b; b = t; }
            char before[64], selected[64]; int n = a;
            if (n > d->input_len) n = d->input_len;
            memcpy(before, d->input_text, (u32)n); before[n] = 0;
            n = b - a; if (a + n > d->input_len) n = d->input_len - a;
            memcpy(selected, d->input_text + a, (u32)n); selected[n] = 0;
            ui_bridge_roundrect(in_x + 8 + ui_bridge_text_width(before, 1), in_y + 5,
                                ui_bridge_text_width(selected, 1), 18, 3, th->selection);
        }
        ui_bridge_text(in_x + 8, in_y + 7, d->input_text, th->text, 1);
        char prefix[64];
        int prefix_len = d->input_cursor;
        if (prefix_len < 0) prefix_len = 0;
        if (prefix_len > d->input_len) prefix_len = d->input_len;
        memcpy(prefix, d->input_text, (u32)prefix_len); prefix[prefix_len] = 0;
        int cur_x = in_x + 8 + ui_bridge_text_width(prefix, 1);
        if (cur_x < in_x + in_w - 6) {
            ui_bridge_rect(cur_x, in_y + 5, 2, 18, th->accent);
        }
    }

    /* Action buttons at bottom right */
    int btn_w = 90;
    int btn_h = 26;
    int btn_y = dy + dh - btn_h - 14;
    int confirm_x = dx + dw - btn_w - 16;

    if (d->kind == DIALOG_CONFIRM || d->kind == DIALOG_INPUT) {
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

int ui_dialog_on_key(int key, int shift, int control) {
    UiDialog *d = &g_active_dialog;
    if (!d->active) return 0;

    if (d->kind == DIALOG_INPUT && control && key == 30) {
        d->input_anchor = 0; d->input_cursor = d->input_len; return 1;
    }
    if (d->kind == DIALOG_INPUT && control && (key == 46 || key == 45)) {
        if (d->input_anchor >= 0 && d->input_anchor != d->input_cursor) {
            int a = d->input_anchor, b = d->input_cursor;
            if (a > b) { int t = a; a = b; b = t; }
            app_clipboard_copy(d->input_text + a, b - a);
            if (key == 45) {
                memmove(d->input_text + a, d->input_text + b, (u32)(d->input_len - b + 1));
                d->input_len -= b - a; d->input_cursor = a; d->input_anchor = -1;
            }
        }
        return 1;
    }
    if (d->kind == DIALOG_INPUT && control && key == 47) {
        char pasted[1024]; int n = app_clipboard_paste(pasted, sizeof(pasted));
        if (d->input_anchor >= 0 && d->input_anchor != d->input_cursor) {
            int a = d->input_anchor, b = d->input_cursor;
            if (a > b) { int t = a; a = b; b = t; }
            memmove(d->input_text + a, d->input_text + b, (u32)(d->input_len - b + 1));
            d->input_len -= b - a; d->input_cursor = a; d->input_anchor = -1;
        }
        if (n > (int)sizeof(d->input_text) - 1 - d->input_len)
            n = (int)sizeof(d->input_text) - 1 - d->input_len;
        if (n > 0) {
            memmove(d->input_text + d->input_cursor + n, d->input_text + d->input_cursor,
                    (u32)(d->input_len - d->input_cursor + 1));
            for (int i = 0; i < n; i++) {
                char ch = pasted[i];
                d->input_text[d->input_cursor + i] = ch >= 32 && ch < 127 ? ch : ' ';
            }
            d->input_len += n; d->input_cursor += n;
        }
        return 1;
    }

    /* Backspace (14) */
    if (d->kind == DIALOG_INPUT && key == 14) {
        if (d->input_anchor >= 0 && d->input_anchor != d->input_cursor) {
            int a = d->input_anchor, b = d->input_cursor;
            if (a > b) { int t = a; a = b; b = t; }
            memmove(d->input_text + a, d->input_text + b, (u32)(d->input_len - b + 1));
            d->input_cursor = a; d->input_len -= b - a; d->input_anchor = -1;
        } else if (d->input_cursor > 0) {
            memmove(d->input_text + d->input_cursor - 1, d->input_text + d->input_cursor,
                    (u32)(d->input_len - d->input_cursor + 1));
            d->input_cursor--;
            d->input_len--;
            d->input_anchor = -1;
        }
        return 1;
    }

    if (d->kind == DIALOG_INPUT && (key == 75 || key == 77 || key == 71 || key == 79 || key == 83)) {
        if (key == 75 || key == 77 || key == 71 || key == 79) {
            int next = key == 75 ? d->input_cursor - 1 : key == 77 ? d->input_cursor + 1 : key == 71 ? 0 : d->input_len;
            if (next < 0) next = 0; if (next > d->input_len) next = d->input_len;
            if (shift) { if (d->input_anchor < 0) d->input_anchor = d->input_cursor; }
            else d->input_anchor = -1;
            d->input_cursor = next;
        }
        else if (key == 83) {
            if (d->input_anchor >= 0 && d->input_anchor != d->input_cursor) {
                int a = d->input_anchor, b = d->input_cursor;
                if (a > b) { int t = a; a = b; b = t; }
                memmove(d->input_text + a, d->input_text + b, (u32)(d->input_len - b + 1));
                d->input_len -= b - a; d->input_cursor = a; d->input_anchor = -1;
            } else if (d->input_cursor < d->input_len) {
                memmove(d->input_text + d->input_cursor, d->input_text + d->input_cursor + 1,
                        (u32)(d->input_len - d->input_cursor));
                d->input_len--;
            }
        }
        return 1;
    }

    /* Tab moves focus between dialog buttons. */
    if (key == 15 || ((key == 75 || key == 77) && d->kind != DIALOG_INPUT)) {
        if (d->kind == DIALOG_CONFIRM || d->kind == DIALOG_INPUT) {
            d->focused_btn = !d->focused_btn;
            return 1;
        }
        return 1;
    }

    /* Enter (28) */
    if (key == 28) {
        if (d->kind == DIALOG_INPUT) {
            void (*cb)(const char *) = d->on_input_result;
            char buf[64];
            str_copy_limit(buf, d->input_text, sizeof(buf));
            ui_dialog_close();
            dialog_input_result(cb,buf);
            return 1;
        }
        int result = (d->kind == DIALOG_MESSAGE) ? 1 : (d->focused_btn == 1);
        void (*cb)(int) = d->on_result;
        ui_dialog_close();
        if (cb) cb(result);
        return 1;
    }

    /* Escape (1) */
    if (key == 1) {
        if (d->kind == DIALOG_INPUT) {
            void (*cb)(const char *) = d->on_input_result;
            ui_dialog_close();
            dialog_input_result(cb,NULL);
            return 1;
        }
        void (*cb)(int) = d->on_result;
        ui_dialog_close();
        dialog_result(cb,0);
        return 1;
    }

    /* Character input for DIALOG_INPUT */
    if (d->kind == DIALOG_INPUT && d->input_len < (int)sizeof(d->input_text) - 1) {
        static const char sc_map[60] = {
            [2]='1', [3]='2', [4]='3', [5]='4', [6]='5', [7]='6', [8]='7', [9]='8', [10]='9', [11]='0',
            [12]='-', [13]='=', [16]='q', [17]='w', [18]='e', [19]='r', [20]='t', [21]='y', [22]='u',
            [23]='i', [24]='o', [25]='p', [30]='a', [31]='s', [32]='d', [33]='f', [34]='g', [35]='h',
            [36]='j', [37]='k', [38]='l', [44]='z', [45]='x', [46]='c', [47]='v', [48]='b', [49]='n',
            [50]='m', [51]=',', [52]='.', [53]='/', [57]=' '
        };
        char ch = (key < 60) ? sc_map[key] : 0;
        if (ch && shift && ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch) {
            if (d->input_anchor >= 0 && d->input_anchor != d->input_cursor) {
                int a = d->input_anchor, b = d->input_cursor;
                if (a > b) { int t = a; a = b; b = t; }
                memmove(d->input_text + a, d->input_text + b, (u32)(d->input_len - b + 1));
                d->input_len -= b - a; d->input_cursor = a; d->input_anchor = -1;
            }
            if (d->input_len >= (int)sizeof(d->input_text) - 1) return 1;
            memmove(d->input_text + d->input_cursor + 1, d->input_text + d->input_cursor,
                    (u32)(d->input_len - d->input_cursor + 1));
            d->input_text[d->input_cursor++] = ch;
            d->input_len++;
            d->input_text[d->input_len] = 0;
            return 1;
        }
    }

    return 1; /* Dialog consumes all key events while modal */
}

int ui_dialog_on_mouse_move(int mx, int my) {
    UiDialog *d = &g_active_dialog;
    if (!d->active) return 0;
    int dx, dy, dw, dh;
    dialog_rect(d, &dx, &dy, &dw, &dh);

    /* Title-bar drag: move the card and report that its pixels changed. */
    if (d->dragging) {
        int sw = ui_bridge_screen_width(), sh = ui_bridge_screen_height();
        int nx = mx - d->drag_off_x;
        int ny = my - d->drag_off_y;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx + dw > sw) nx = sw - dw;
        if (ny + dh > sh) ny = sh - dh;
        if (nx == dx && ny == dy) return 0;
        d->x = nx; d->y = ny;
        return 1;
    }

    int btn_w = 90;
    int btn_h = 26;
    int btn_y = dy + dh - btn_h - 14;
    int confirm_x = dx + dw - btn_w - 16;
    int cancel_x = confirm_x - btn_w - 10;

    if (d->kind == DIALOG_CONFIRM || d->kind == DIALOG_INPUT) {
        if (ui_button_hit(cancel_x, btn_y, btn_w, btn_h, mx, my)) {
            if (d->focused_btn != 0) { d->focused_btn = 0; return 1; }
            return 0;
        }
        if (ui_button_hit(confirm_x, btn_y, btn_w, btn_h, mx, my)) {
            if (d->focused_btn != 1) { d->focused_btn = 1; return 1; }
            return 0;
        }
    }
    return 0;
}

int ui_dialog_on_mouse_down(int mx, int my, int button) {
    UiDialog *d = &g_active_dialog;
    if (!d->active) return 0;
    (void)button;

    int dx, dy, dw, dh;
    dialog_rect(d, &dx, &dy, &dw, &dh);

    /* Top right close button */
    if (mx >= dx + dw - 28 && mx < dx + dw - 8 && my >= dy + 6 && my < dy + 26) {
        if (d->kind == DIALOG_INPUT) {
            void (*cb)(const char *) = d->on_input_result;
            ui_dialog_close();
            dialog_input_result(cb,NULL);
            return 1;
        }
        void (*cb)(int) = d->on_result;
        ui_dialog_close();
        dialog_result(cb,0);
        return 1;
    }

    int btn_w = 90;
    int btn_h = 26;
    int btn_y = dy + dh - btn_h - 14;
    int confirm_x = dx + dw - btn_w - 16;

    if (d->kind == DIALOG_CONFIRM || d->kind == DIALOG_INPUT) {
        int cancel_x = confirm_x - btn_w - 10;
        if (ui_button_hit(cancel_x, btn_y, btn_w, btn_h, mx, my)) {
            if (d->kind == DIALOG_INPUT) {
                void (*cb)(const char *) = d->on_input_result;
                ui_dialog_close();
                dialog_input_result(cb,NULL);
                return 1;
            }
            void (*cb)(int) = d->on_result;
            ui_dialog_close();
            dialog_result(cb,0);
            return 1;
        }
        if (ui_button_hit(confirm_x, btn_y, btn_w, btn_h, mx, my)) {
            if (d->kind == DIALOG_INPUT) {
                void (*cb)(const char *) = d->on_input_result;
                char buf[64];
                str_copy_limit(buf, d->input_text, sizeof(buf));
                ui_dialog_close();
                dialog_input_result(cb,buf);
                return 1;
            }
            void (*cb)(int) = d->on_result;
            ui_dialog_close();
            dialog_result(cb,1);
            return 1;
        }
    } else {
        if (ui_button_hit(confirm_x, btn_y, btn_w, btn_h, mx, my)) {
            void (*cb)(int) = d->on_result;
            ui_dialog_close();
            dialog_result(cb,1);
            return 1;
        }
    }

    /* Title-bar drag: the top 32px strip, anywhere except the close button. */
    if (my >= dy && my < dy + 32 && mx >= dx && mx < dx + dw) {
        d->dragging = 1;
        d->drag_off_x = mx - dx;
        d->drag_off_y = my - dy;
        return 1;
    }

    return 1; /* Dialog captures clicks within modal area */
}

void ui_dialog_on_mouse_up(int mx, int my) {
    (void)mx; (void)my;
    g_active_dialog.dragging = 0;
}

/* =========================================================================
 * 6. NOTIFICATION TOAST SYSTEM
 * ========================================================================= */

UiNotification g_notifications[MAX_NOTIFICATIONS];

extern u32 wm_time_ms(void);

void ui_notify(const char *title, const char *message, IconKind icon) {
    (void)title;
    (void)message;
    (void)icon;
}

void ui_draw_notifications(u32 now_ms) {
    (void)now_ms;
    /* Top-right notification popups disabled per user request */
}
