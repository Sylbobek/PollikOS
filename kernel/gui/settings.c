#include "app_internal.h"
#include "../audio.h"
#include "../pmm.h"
#include "../auth.h"

enum {
    TAB_APPEARANCE = 0,
    TAB_DESKTOP_DOCK = 1,
    TAB_DISPLAY = 2,
    TAB_SOUND = 3,
    TAB_NETWORK = 4,
    TAB_GENERAL = 5,
    TAB_COUNT = 6
};

typedef struct {
    const char *name;
    u32 badge_color;
    char badge_char;
} SettingsTabInfo;

static const SettingsTabInfo TABS[TAB_COUNT] = {
    [TAB_APPEARANCE]   = {"Appearance",     0x2563eb, 'A'},
    [TAB_DESKTOP_DOCK] = {"Desktop & Dock", 0x06b6d4, 'D'},
    [TAB_DISPLAY]      = {"Display",        0xf59e0b, 'S'},
    [TAB_SOUND]        = {"Sound",          0xf43f5e, 'V'},
    [TAB_NETWORK]      = {"Network",        0x10b981, 'N'},
    [TAB_GENERAL]      = {"General",        0x64748b, 'i'},
};

static const u32 ACCENT_COLORS[5] = {
    0x2563eb, /* Royal Blue */
    0x7c3aed, /* Violet */
    0x059669, /* Emerald */
    0xd97706, /* Amber */
    0xe11d48  /* Rose */
};

static const char *ACCENT_NAMES[5] = {
    "Ocean Blue", "Violet Indigo", "Emerald Green", "Amber Orange", "Rose Pink"
};

static int g_settings_tab = TAB_APPEARANCE;
static int g_sound_played = 0;
static const char *g_last_net_status = 0;

/* Layout computation helper */
typedef struct {
    AppRect sidebar;
    AppRect tabs[TAB_COUNT];
    AppRect content;
    int dark;
    u32 bg_card, border_card, text_head, text_body, text_muted;
} SettingsLayout;

static SettingsLayout settings_calc_layout(int width, int height) {
    SettingsLayout l;
    l.dark = ui_is_dark();
    l.bg_card = l.dark ? 0x1c212e : 0xffffff;
    l.border_card = l.dark ? 0x2d3748 : 0xe4dff0;
    l.text_head = l.dark ? 0xf8fafc : 0x221a30;
    l.text_body = l.dark ? 0xe2e8f0 : 0x483d5a;
    l.text_muted = l.dark ? 0x94a3b8 : 0x7c718c;

    int sb_w = 172;
    l.sidebar = (AppRect){0, 34, sb_w, height - 34};
    int tab_y = 80;
    for (int i = 0; i < TAB_COUNT; i++) {
        l.tabs[i] = (AppRect){8, tab_y + i * 36, sb_w - 16, 32};
    }
    l.content = (AppRect){sb_w + 16, 44, width - sb_w - 32, height - 56};
    return l;
}

static void draw_card(AppRect r, u32 bg, u32 border, int radius) {
    (void)border;
    roundrect(r.x, r.y, r.w, r.h, radius, bg);
}

int settings_poll(void) {
    if (g_settings_tab == TAB_NETWORK) {
        if (net_status != g_last_net_status) {
            g_last_net_status = net_status;
            return 1;
        }
    }
    return 0;
}

void settings_render(int width, int height, int active) {
    (void)active;
    SettingsLayout l = settings_calc_layout(width, height);
    int dark = l.dark;
    u32 acc = app_host_accent_color();
    u32 acc_h = app_host_accent_hover();

    /* 1. Left Sidebar background & divider */
    u32 sb_bg = dark ? 0x131620 : 0xf2eef7;
    roundrect(l.sidebar.x, l.sidebar.y, l.sidebar.w, l.sidebar.h, 0, sb_bg);
    rect(l.sidebar.w, 34, 1, height - 34, dark ? 0x262f42 : 0xe0d9eb);

    /* Sidebar User profile pill at top */
    app_label(16, 47, l.sidebar.w - 28, "PollikOS User", l.text_head, 1);
    rect(12, 72, l.sidebar.w - 24, 1, dark ? 0x242d3e : 0xe4ddef);

    /* Sidebar category items */
    for (int i = 0; i < TAB_COUNT; i++) {
        AppRect r = l.tabs[i];
        int sel = (i == g_settings_tab);
        if (sel) {
            roundrect(r.x, r.y, r.w, r.h, 8, acc);
            if (dark) rect(r.x + 8, r.y, r.w - 16, 1, acc_h);
        }
        /* Icon badge */
        roundrect(r.x + 6, r.y + 6, 20, 20, 5, TABS[i].badge_color);
        char bch[2] = {TABS[i].badge_char, 0};
        centered(r.x + 6, r.y + 9, 20, bch, 0xffffff, 1);

        /* Label */
        u32 label_color = sel ? 0xffffff : l.text_body;
        text(r.x + 32, r.y + 9, TABS[i].name, label_color, 1);
    }

    /* 2. Content Header */
    int cx = l.content.x;
    int cw = l.content.w;
    text(cx, 44, TABS[g_settings_tab].name, l.text_head, 2);

    /* 3. Tab-specific Cards */
    if (g_settings_tab == TAB_APPEARANCE) {
        /* Subtitle */
        text(cx, 68, "Customize the appearance of windows, accents and materials.", l.text_muted, 1);

        /* Theme Card */
        AppRect card1 = {cx, 90, cw, 106};
        draw_card(card1, l.bg_card, l.border_card, 12);
        text(cx + 14, 98, "Theme Mode", l.text_head, 1);

        /* Light Preview Tile */
        int t1_x = cx + 24, t_y = 118;
        if (!dark) roundrect(t1_x - 3, t_y - 3, 102, 62, 8, acc);
        roundrect(t1_x, t_y, 96, 56, 6, 0xf8fafc);
        rect(t1_x, t_y, 96, 13, 0xe2e8f0);
        roundrect(t1_x + 4, t_y + 4, 5, 5, 2, 0xef4444);
        roundrect(t1_x + 11, t_y + 4, 5, 5, 2, 0xf59e0b);
        roundrect(t1_x + 18, t_y + 4, 5, 5, 2, 0x10b981);
        rect(t1_x + 6, t_y + 22, 54, 3, 0x94a3b8);
        rect(t1_x + 6, t_y + 30, 42, 2, 0xcbd5e1);
        centered(t1_x, t_y + 40, 96, "Light", 0x334155, 1);

        /* Dark Preview Tile */
        int t2_x = cx + 138;
        if (dark) roundrect(t2_x - 3, t_y - 3, 102, 62, 8, acc);
        roundrect(t2_x, t_y, 96, 56, 6, 0x0f172a);
        rect(t2_x, t_y, 96, 13, 0x1e293b);
        roundrect(t2_x + 4, t_y + 4, 5, 5, 2, 0xef4444);
        roundrect(t2_x + 11, t_y + 4, 5, 5, 2, 0xf59e0b);
        roundrect(t2_x + 18, t_y + 4, 5, 5, 2, 0x10b981);
        rect(t2_x + 6, t_y + 22, 54, 3, 0x475569);
        rect(t2_x + 6, t_y + 30, 42, 2, 0x334155);
        centered(t2_x, t_y + 40, 96, "Dark", 0xe2e8f0, 1);

        /* Accent Color Card */
        AppRect card2 = {cx, 206, cw, 78};
        draw_card(card2, l.bg_card, l.border_card, 12);
        text(cx + 14, 214, "Accent Color", l.text_head, 1);
        text(cx + 14, 230, "Choose your primary tint for buttons, active tabs and highlights:", l.text_muted, 1);

        int cur_accent = app_host_accent();
        for (int a = 0; a < 5; a++) {
            int ax = cx + 18 + a * 38;
            roundrect(ax, 248, 26, 26, 13, ACCENT_COLORS[a]);
            if (a == cur_accent) {
                roundrect(ax + 7, 255, 12, 12, 6, 0xffffff);
            }
        }
        app_label(cx + 220, 253, cw - 230, ACCENT_NAMES[cur_accent], ACCENT_COLORS[cur_accent], 1);

        /* Visual Materials Card */
        AppRect card3 = {cx, 294, cw, 68};
        draw_card(card3, l.bg_card, l.border_card, 12);
        text(cx + 14, 302, "Window Materials & Shadows", l.text_head, 1);
        app_label(cx + 14, 320, cw - 150,
                  "Frosted acrylic material with soft ambient shadow.", l.text_body, 1);
        roundrect(cx + cw - 120, 314, 104, 24, 6, dark ? 0x223046 : 0xe9e2f2);
        centered(cx + cw - 120, 319, 104, "Hardware 32bpp", dark ? 0x93c5fd : 0x5a4878, 1);

        AppRect card4 = {cx, 370, cw, 128};
        draw_card(card4, l.bg_card, l.border_card, 12);
        text(cx + 14, 378, "Wallpaper", l.text_head, 1);
        int wallpaper_count = app_host_wallpaper_count();
        const char *selected_wallpaper = app_host_selected_wallpaper();
        if (!wallpaper_count) text(cx + 16, 398, "No PNG files found; using the built-in gradient.", l.text_muted, 1);
        for (int i = 0; i < wallpaper_count; i++) {
            const char *name = app_host_wallpaper_name(i);
            if (!name) continue;
            int selected = 1, j = 0;
            while (name[j] || selected_wallpaper[j]) {
                if (name[j] != selected_wallpaper[j]) { selected = 0; break; }
                ++j;
            }
            int row_y = 396 + i * 13;
            if (selected) roundrect(cx + 10, row_y - 1, cw - 20, 12, 4, acc);
            text(cx + 18, row_y, name, selected ? 0xffffff : l.text_body, 1);
        }

    } else if (g_settings_tab == TAB_DESKTOP_DOCK) {
        text(cx, 68, "Control dock behavior, magnification and desktop item snapping.", l.text_muted, 1);

        /* Window Animations Card */
        AppRect card1 = {cx, 90, cw, 80};
        draw_card(card1, l.bg_card, l.border_card, 12);
        text(cx + 14, 98, "Window Motion & Minimize Effects", l.text_head, 1);
        text(cx + 14, 114, "Enable buttery smooth 120 FPS window transitions and animations.", l.text_muted, 1);

        int anim_on = app_host_animations();
        /* Smooth button */
        roundrect(cx + 14, 134, 130, 26, 8, anim_on ? acc : (dark ? 0x161a26 : 0xe9e1f3));
        centered(cx + 14, 140, 130, "Smooth (120 FPS)", anim_on ? 0xffffff : l.text_body, 1);
        /* Fast button */
        roundrect(cx + 152, 134, 110, 26, 8, !anim_on ? acc : (dark ? 0x161a26 : 0xe9e1f3));
        centered(cx + 152, 140, 110, "Fast (Instant)", !anim_on ? 0xffffff : l.text_body, 1);

        /* Dock Magnification Card */
        AppRect card2 = {cx, 180, cw, 80};
        draw_card(card2, l.bg_card, l.border_card, 12);
        text(cx + 14, 188, "Dock Magnification & Appearance", l.text_head, 1);
        text(cx + 14, 204, "Icons dynamically scale and bounce on hover and launch.", l.text_muted, 1);

        int zoom = app_host_dock_zoom();
        roundrect(cx + 14, 224, 130, 26, 8, zoom ? acc : (dark ? 0x161a26 : 0xe9e1f3));
        centered(cx + 14, 230, 130, zoom ? "Zoom: Enabled" : "Zoom: Disabled", zoom ? 0xffffff : l.text_body, 1);

        roundrect(cx + 152, 224, 150, 26, 8, dark ? 0x1a2436 : 0xf1ecf8);
        centered(cx + 152, 230, 150, "Position: Bottom Center", l.text_body, 1);

        /* Desktop Grid Snapping Card */
        AppRect card3 = {cx, 270, cw, 78};
        draw_card(card3, l.bg_card, l.border_card, 12);
        text(cx + 14, 278, "Desktop Grid & Layout Persistence", l.text_head, 1);
        text(cx + 14, 296, "Automatic 104x96 px grid snapping prevents overlapping icons.", l.text_body, 1);
        text(cx + 14, 312, "Layouts persist safely across reboots in /home/Desktop/.layout", l.text_muted, 1);

        AppRect card4 = {cx, 358, cw, 58};
        draw_card(card4, l.bg_card, l.border_card, 12);
        text(cx + 14, 366, "Pointer Acceleration", l.text_head, 1);
        int accel = app_host_pointer_acceleration();
        roundrect(cx + 14, 384, 146, 24, 7, accel ? acc : (dark ? 0x161a26 : 0xe9e1f3));
        centered(cx + 14, 390, 146, accel ? "Modest: Enabled" : "Disabled", accel ? 0xffffff : l.text_body, 1);

        AppRect cursor_card = {cx, 426, cw, 64};
        draw_card(cursor_card, l.bg_card, l.border_card, 12);
        text(cx + 14, 434, "Cursor size", l.text_head, 1);
        const int cursor_sizes[4] = {100, 125, 150, 200};
        const char *cursor_labels[4] = {"100%", "125%", "150%", "200%"};
        for (int i = 0; i < 4; ++i) {
            int selected = app_host_cursor_size() == cursor_sizes[i];
            int bx = cx + 14 + i * 72;
            roundrect(bx, 456, 64, 26, 7, selected ? acc : (dark ? 0x161a26 : 0xe9e1f3));
            centered(bx, 462, 64, cursor_labels[i], selected ? 0xffffff : l.text_body, 1);
        }

    } else if (g_settings_tab == TAB_DISPLAY) {
        text(cx, 68, "View display resolution, color profile and graphics pipeline stats.", l.text_muted, 1);

        /* Display Card */
        AppRect card1 = {cx, 90, cw, 96};
        draw_card(card1, l.bg_card, l.border_card, 12);

        /* Monitor Graphic */
        int mx = cx + 18, my = 104;
        roundrect(mx, my, 48, 36, 4, acc);
        rect(mx + 2, my + 2, 44, 30, 0x0f172a);
        rect(mx + 20, my + 36, 8, 8, 0x64748b);
        roundrect(mx + 12, my + 44, 24, 3, 1, 0x94a3b8);

        text(cx + 78, 102, "Built-in Display", l.text_head, 1);
        text(cx + 78, 120, "1024 x 768 @ 60/120 Hz TrueColor", l.text_body, 1);
        text(cx + 78, 136, "32-bit linear framebuffer architecture via VESA/VBE BIOS.", l.text_muted, 1);

        /* Pipeline Card */
        AppRect card2 = {cx, 196, cw, 86};
        draw_card(card2, l.bg_card, l.border_card, 12);
        text(cx + 14, 204, "Rendering & Compositor Architecture", l.text_head, 1);
        text(cx + 14, 222, "Direct memory-mapped buffer with zero-tearing double buffering.", l.text_body, 1);
        text(cx + 14, 238, "Hardware format: 0x00RRGGBB, fast Ring 0 compositor.", l.text_muted, 1);

        int target_fps = app_host_target_fps();
        int is_120 = (target_fps >= 120);
        roundrect(cx + 14, 252, 135, 26, 8, is_120 ? acc : (dark ? 0x161a26 : 0xe9e1f3));
        centered(cx + 14, 258, 135, "120 Hz (ProMotion)", is_120 ? 0xffffff : l.text_body, 1);

        roundrect(cx + 156, 252, 125, 26, 8, !is_120 ? acc : (dark ? 0x161a26 : 0xe9e1f3));
        centered(cx + 156, 258, 125, "60 Hz (Standard)", !is_120 ? 0xffffff : l.text_body, 1);

    } else if (g_settings_tab == TAB_SOUND) {
        text(cx, 68, "Manage audio outputs, notification alert tones and hardware mixer.", l.text_muted, 1);

        /* Speaker Device Card */
        AppRect card1 = {cx, 90, cw, 80};
        draw_card(card1, l.bg_card, l.border_card, 12);
        text(cx + 14, 98, "Output Audio Device", l.text_head, 1);
        if (audio_is_available()) {
            text(cx + 14, 116, "Intel 82801AA AC'97 Controller (Bus Master DMA)", l.text_body, 1);
            text(cx + 14, 132, "48.0 kHz 16-bit PCM TrueSound with hardware mixer.", l.text_muted, 1);
        } else {
            text(cx + 14, 116, "Standard PC Speaker (Intel 8254 PIT Timer 2, Port 0x61)", l.text_body, 1);
            text(cx + 14, 132, "Direct hardware pulse-width modulation (PWM) synthesizer.", l.text_muted, 1);
        }

        /* Volume & Frequency Card */
        AppRect card2 = {cx, 180, cw, 86};
        draw_card(card2, l.bg_card, l.border_card, 12);
        text(cx + 14, 188, "Output Volume Level", l.text_head, 1);
        text(cx + 14, 204, "Select output volume level (hardware mixer attenuation):", l.text_muted, 1);

        u8 vols[4] = {25, 50, 75, 100};
        const char *vnames[4] = {"25%", "50%", "75%", "100%"};
        u8 cur_vol = audio_get_volume();
        for (int v = 0; v < 4; v++) {
            int vx = cx + 14 + v * 85;
            int sel = (cur_vol == vols[v] || (v == 3 && cur_vol >= 90));
            roundrect(vx, 226, 75, 28, 8, sel ? acc : (dark ? 0x161a26 : 0xe9e1f3));
            centered(vx, 233, 75, vnames[v], sel ? 0xffffff : l.text_body, 1);
        }

        /* Sound Test Card */
        AppRect card3 = {cx, 276, cw, 78};
        draw_card(card3, l.bg_card, l.border_card, 12);
        text(cx + 14, 284, "Hardware Audio Diagnostic & System Chimes", l.text_head, 1);

        int muted = app_host_sound_muted();
        roundrect(cx + 14, 306, 95, 30, 8, muted ? 0xef4444 : (dark ? 0x161a26 : 0xe9e1f3));
        centered(cx + 14, 314, 95, muted ? "Muted" : "Active", muted ? 0xffffff : l.text_body, 1);

        roundrect(cx + 115, 306, 120, 30, 8, acc);
        centered(cx + 115, 314, 120, "Play Chime", 0xffffff, 1);

        roundrect(cx + 242, 306, 110, 30, 8, dark ? 0x161a26 : 0xe9e1f3);
        centered(cx + 242, 314, 110, "Trash Sound", l.text_body, 1);

        if (g_sound_played) {
            roundrect(cx + 360, 309, 100, 24, 6, muted ? 0xef4444 : 0x059669);
            centered(cx + 360, 313, 100, muted ? "Muted" : "Played OK!", 0xffffff, 1);
        } else {
            text(cx + 360, 314, "Click to test.", l.text_muted, 1);
        }

    } else if (g_settings_tab == TAB_NETWORK) {
        text(cx, 68, "View network adapter status, IP configuration and run ICMP ping.", l.text_muted, 1);

        /* Connection Card */
        AppRect card1 = {cx, 90, cw, 72};
        draw_card(card1, l.bg_card, l.border_card, 12);
        roundrect(cx + 14, 100, 10, 10, 5, net_ready ? 0x10b981 : 0xef4444);
        text(cx + 32, 98, net_ready ? "Connected" : "Disconnected", l.text_head, 1);
        text(cx + 32, 116, "Realtek RTL8139 Fast Ethernet PCI (100 Mbps Full-Duplex)", l.text_body, 1);
        text(cx + 32, 132, "DHCP Auto-Configuration active. Standard MTU: 1500 bytes.", l.text_muted, 1);

        /* Detailed Network info Card */
        AppRect card2 = {cx, 172, cw, 100};
        draw_card(card2, l.bg_card, l.border_card, 12);
        text(cx + 14, 180, "IP Protocol Details", l.text_head, 1);

        char info[512];
        net_info(info);
        app_text_box((AppRect){cx + 14, 200, cw - 28, 64}, info, l.text_body, 1, 14);

        /* Ping Test Card */
        AppRect card3 = {cx, 282, cw, 72};
        draw_card(card3, l.bg_card, l.border_card, 12);
        text(cx + 14, 290, "Connection Diagnostics", l.text_head, 1);

        roundrect(cx + 14, 310, 150, 30, 8, acc);
        centered(cx + 14, 318, 150, "Run Ping Test", 0xffffff, 1);

        app_label(cx + 176, 318, cw - 190, net_status ? net_status : "Ready", l.text_muted, 1);

    } else if (g_settings_tab == TAB_GENERAL) {
        text(cx, 68, "System specifications, memory allocation and power management.", l.text_muted, 1);

        /* System Info Card */
        AppRect card1 = {cx, 90, cw, 78};
        draw_card(card1, l.bg_card, l.border_card, 12);
        text(cx + 14, 100, OS_LABEL, l.text_head, 1);
        text(cx + 14, 118, "x86-32 Protected Mode Kernel, Direct Compositor & PollikFS v2", l.text_body, 1);
        text(cx + 14, 134, "Native Ring 0 GUI Host, ELF32 Userspace Isolation", l.text_muted, 1);

        /* Memory Usage Card */
        AppRect card2 = {cx, 178, cw, 80};
        draw_card(card2, l.bg_card, l.border_card, 12);
        text(cx + 14, 186, "Physical Memory (PMM)", l.text_head, 1);

        u32 total_bytes = pmm_get_total_memory();
        u32 free_bytes = pmm_get_free_memory();
        u32 used_bytes = (total_bytes > free_bytes) ? (total_bytes - free_bytes) : 0;
        u32 total_mb = total_bytes / (1024 * 1024);
        u32 used_mb = used_bytes / (1024 * 1024);
        if (total_mb == 0) total_mb = 2048;

        /* Progress bar track */
        int bar_w = cw - 28;
        roundrect(cx + 14, 206, bar_w, 14, 7, dark ? 0x242d3e : 0xe4ddf0);
        int fill_w = (int)(((u32)used_mb * (u32)bar_w) / (total_mb ? total_mb : 1));
        if (fill_w < 10) fill_w = 10;
        if (fill_w > bar_w) fill_w = bar_w;
        roundrect(cx + 14, 206, fill_w, 14, 7, acc);

        char mem_str[64] = "Used: ";
        char num[16];
        number(num, used_mb);
        append_str(mem_str, num, sizeof(mem_str));
        append_str(mem_str, " MB  /  Total: ", sizeof(mem_str));
        number(num, total_mb);
        append_str(mem_str, num, sizeof(mem_str));
        append_str(mem_str, " MB RAM", sizeof(mem_str));
        text(cx + 14, 228, mem_str, l.text_muted, 1);

        /* Storage Card */
        AppRect card3 = {cx, 268, cw, 50};
        draw_card(card3, l.bg_card, l.border_card, 12);
        text(cx + 14, 276, "Storage:", l.text_head, 1);
        text(cx + 80, 276, "PollikFS v2 (10 GiB Sparse Disk, /home/Desktop)", l.text_body, 1);
        text(cx + 14, 292, "Mount: / mounted as root (read/write). Block size: 1024 B.", l.text_muted, 1);

        /* Power Options Card */
        AppRect card4 = {cx, 328, cw, 48};
        draw_card(card4, l.bg_card, l.border_card, 12);
        roundrect(cx + 14, 336, 110, 30, 8, dark ? 0x223046 : 0xe9e1f3);
        centered(cx + 14, 343, 110, "Restart...", l.text_head, 1);

        roundrect(cx + 134, 336, 120, 30, 8, dark ? 0x3d1a24 : 0xfbe4e8);
        centered(cx + 134, 343, 120, "Shut Down...", dark ? 0xf87171 : 0xa93226, 1);

        roundrect(cx + 264, 336, 110, 30, 8, acc);
        centered(cx + 264, 343, 110, "Lock", 0xffffff, 1);
    }
}

void settings_click(int x, int y) {
    GuiAppSize s = gui_app_size(APP_SETTINGS);
    SettingsLayout l = settings_calc_layout(s.width, s.height);

    /* 1. Sidebar clicks */
    for (int i = 0; i < TAB_COUNT; i++) {
        if (app_hit(l.tabs[i], x, y)) {
            g_settings_tab = i;
            app_host_invalidate(APP_SETTINGS);
            return;
        }
    }

    int cx = l.content.x;
    int cw = l.content.w;

    /* 2. Tab content clicks */
    if (g_settings_tab == TAB_APPEARANCE) {
        /* Light tile: x: cx + 24, y: 118, w: 96, h: 56 */
        if (x >= cx + 24 && x <= cx + 120 && y >= 118 && y <= 174) {
            app_host_set_theme(1);
            app_host_invalidate(APP_SETTINGS);
        }
        /* Dark tile: x: cx + 138, y: 118, w: 96, h: 56 */
        else if (x >= cx + 138 && x <= cx + 234 && y >= 118 && y <= 174) {
            app_host_set_theme(0);
            app_host_invalidate(APP_SETTINGS);
        }
        /* Accent colors: 5 circles at y: 248..274 */
        for (int a = 0; a < 5; a++) {
            int ax = cx + 18 + a * 38;
            if (x >= ax && x <= ax + 26 && y >= 248 && y <= 274) {
                app_host_set_accent(a);
                app_host_invalidate(APP_SETTINGS);
                break;
            }
        }
        int wallpaper_count = app_host_wallpaper_count();
        for (int i = 0; i < wallpaper_count; i++) {
            int row_y = 396 + i * 13;
            if (x >= cx + 10 && x < cx + cw - 10 && y >= row_y - 1 && y < row_y + 11) {
                app_host_set_wallpaper(i);
                app_host_invalidate(APP_SETTINGS);
                break;
            }
        }
    } else if (g_settings_tab == TAB_DESKTOP_DOCK) {
        /* Smooth animations: cx + 14, 134, 130, 26 */
        if (x >= cx + 14 && x <= cx + 144 && y >= 134 && y <= 160) {
            app_host_set_animations(1);
            app_host_invalidate(APP_SETTINGS);
        }
        /* Fast animations: cx + 152, 134, 110, 26 */
        else if (x >= cx + 152 && x <= cx + 262 && y >= 134 && y <= 160) {
            app_host_set_animations(0);
            app_host_stop_minimize();
            app_host_invalidate(APP_SETTINGS);
        }
        /* Dock zoom: cx + 14, 224, 130, 26 */
        else if (x >= cx + 14 && x <= cx + 144 && y >= 224 && y <= 250) {
            app_host_set_dock_zoom(!app_host_dock_zoom());
            app_host_invalidate(APP_SETTINGS);
        }
        if (x >= cx + 14 && x <= cx + 160 && y >= 384 && y <= 408) {
            app_host_set_pointer_acceleration(!app_host_pointer_acceleration());
            app_host_invalidate(APP_SETTINGS);
        }
        if (y >= 456 && y < 482) {
            const int cursor_sizes[4] = {100, 125, 150, 200};
            for (int i = 0; i < 4; ++i) {
                int bx = cx + 14 + i * 72;
                if (x >= bx && x < bx + 64) {
                    app_host_set_cursor_size(cursor_sizes[i]);
                    app_host_invalidate(APP_SETTINGS);
                }
            }
        }
    } else if (g_settings_tab == TAB_DISPLAY) {
        /* 120 Hz: cx + 14, 252, 135, 26 */
        if (x >= cx + 14 && x <= cx + 149 && y >= 252 && y <= 278) {
            app_host_set_target_fps(120);
            app_host_invalidate(APP_SETTINGS);
        }
        /* 60 Hz: cx + 156, 252, 125, 26 */
        else if (x >= cx + 156 && x <= cx + 281 && y >= 252 && y <= 278) {
            app_host_set_target_fps(60);
            app_host_invalidate(APP_SETTINGS);
        }
    } else if (g_settings_tab == TAB_SOUND) {
        /* Volume buttons: 4 buttons at cx + 14 + v * 85, 226, 75, 28 */
        u8 vols[4] = {25, 50, 75, 100};
        for (int v = 0; v < 4; v++) {
            int vx = cx + 14 + v * 85;
            if (x >= vx && x <= vx + 75 && y >= 226 && y <= 254) {
                audio_set_volume(vols[v]);
                audio_play_sound(SOUND_CLICK);
                app_host_invalidate(APP_SETTINGS);
                break;
            }
        }
        /* Sound Mute Toggle: cx + 14, 306, 95, 30 */
        if (x >= cx + 14 && x <= cx + 109 && y >= 306 && y <= 336) {
            app_host_set_sound_muted(!app_host_sound_muted());
            if (app_host_sound_muted()) audio_set_volume(0);
            else audio_set_volume(85);
            app_host_invalidate(APP_SETTINGS);
        }
        /* Play Chime button: cx + 115, 306, 120, 30 */
        else if (x >= cx + 115 && x <= cx + 235 && y >= 306 && y <= 336) {
            audio_play_sound(SOUND_STARTUP);
            g_sound_played = 1;
            app_host_invalidate(APP_SETTINGS);
        }
        /* Play Trash Sound button: cx + 242, 306, 110, 30 */
        else if (x >= cx + 242 && x <= cx + 352 && y >= 306 && y <= 336) {
            audio_play_sound(SOUND_TRASH);
            g_sound_played = 1;
            app_host_invalidate(APP_SETTINGS);
        }
    } else if (g_settings_tab == TAB_NETWORK) {
        /* Run Ping Test: cx + 14, 310, 150, 30 */
        if (x >= cx + 14 && x <= cx + 164 && y >= 310 && y <= 340) {
            net_ping();
            app_host_invalidate(APP_SETTINGS);
        }
    } else if (g_settings_tab == TAB_GENERAL) {
        /* Restart: cx + 14, 336, 110, 30 */
        if (x >= cx + 14 && x <= cx + 124 && y >= 336 && y <= 366) {
            app_host_power(1);
        }
        /* Shut Down: cx + 134, 336, 120, 30 */
        else if (x >= cx + 134 && x <= cx + 254 && y >= 336 && y <= 366) {
            app_host_power(0);
        }
        else if (x >= cx + 264 && x <= cx + 374 && y >= 336 && y <= 366) {
            auth_lock();
            app_host_invalidate(APP_SETTINGS);
        }
    }
}
