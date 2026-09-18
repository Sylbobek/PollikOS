#include "app_internal.h"

typedef struct { AppRect theme[2], animation[2], test; int compact, col; } SettingsLayout;
static SettingsLayout settings_layout(int width, int height) {
    SettingsLayout l;
    l.compact = height < 380;
    l.col = (width - 120) / 2;
    int top = l.compact ? 91 : 124, h = l.compact ? 44 : 74;
    for (int i = 0; i < 2; i++) {
        l.theme[i] = (AppRect){38 + i * (l.col + 26), top, l.col, h};
        l.animation[i] = (AppRect){38 + i * ((l.col - 10) / 2 + 10),
            l.compact ? 165 : 236, (l.col - 10) / 2, 34};
    }
    l.test = (AppRect){64 + l.col, l.compact ? height - 62 : height - 100,
        l.col < 184 ? l.col : 184, 37};
    return l;
}
void settings_render(int width, int height, int active) {
    (void)active;
    SettingsLayout l = settings_layout(width, height);
    int animations = app_host_animations();
    app_label(38, l.compact ? 53 : 73, width - 76, "Make yourself at home.", 0x4a3d60, width < 600 ? 2 : 3);
    for (int i = 0; i < 2; i++) {
        AppRect r = l.theme[i];
        roundrect(r.x, r.y, r.w, r.h, l.compact ? 14 : 23, i ? 0xd7edeb : 0xe9e0f3);
        app_label(r.x + 20, r.y + (r.h - 26) / 2, r.w - 40,
            i ? "Ocean mist" : "Violet light", i ? 0x467c82 : 0x70518c, width < 600 ? 1 : 2);
        r = l.animation[i];
        int selected = i ? !animations : animations;
        roundrect(r.x, r.y, r.w, r.h, 12, selected ? 0x8472cc : 0xe9e0f3);
        centered(r.x, r.y + 9, r.w, width < 600 ? (i ? "Fast" : "Smooth") :
            (i ? "Fast (Off)" : "Smooth (On)"), selected ? 0xffffff : 0x70518c, 1);
    }
    int label_y = l.animation[0].y - 24, nx = l.theme[1].x;
    text(38, label_y, "Animations", 0x81718f, width < 600 ? 1 : 2);
    app_label(nx, label_y, l.col, "Network / Internet", 0x81718f, width < 600 ? 1 : 2);
    char info[512];
    net_info(info);
    app_text_box((AppRect){nx, l.animation[0].y, l.col, l.test.y - l.animation[0].y - 4},
        l.compact ? net_status : info, 0x8a7b99, 1, 15);
    AppRect r = l.test;
    roundrect(r.x, r.y, r.w, r.h, 17, 0xe9e1f1);
    centered(r.x, r.y + 11, r.w, "Test connection", 0x7a5b99, 1);
    app_label(38, l.compact ? height - 22 : height - 60, width - 76,
        "Wi-Fi: no supported radio driver. Ethernet MTU: 1500.", 0x8a7b99, 1);
}
void settings_click(int x, int y) {
    GuiAppSize s = gui_app_size(APP_SETTINGS);
    SettingsLayout l = settings_layout(s.width, s.height);
    if (app_hit(l.test, x, y)) net_ping();
    for (int i = 0; i < 2; i++) {
        if (app_hit(l.theme[i], x, y)) app_host_set_theme(i);
        if (app_hit(l.animation[i], x, y)) {
            app_host_set_animations(!i);
            if (i) app_host_stop_minimize();
        }
    }
}
