#include "app_internal.h"
#include "../vfs.h"

int welcome_is_first_boot(void) {
    vfs_stat_t st;
    if (vfs_stat("/etc/.first_boot_done", &st) == VFS_OK) return 0;
    return 1;
}

void welcome_mark_first_boot_done(void) {
    vfs_mkdir("/etc");
    int fd = vfs_open("/etc/.first_boot_done", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0) {
        vfs_write(fd, "1\n", 2);
        vfs_close(fd);
    }
}

static AppRect welcome_start_btn_rect(int width, int height) {
    int btn_w = 160;
    int btn_h = 38;
    int btn_x = (width - btn_w) / 2;
    int btn_y = height - 54;
    if (btn_y < 160) btn_y = 160;
    return (AppRect){btn_x, btn_y, btn_w, btn_h};
}

void welcome_click(int x, int y) {
    GuiAppSize sz = gui_app_size(APP_WELCOME);
    AppRect btn = welcome_start_btn_rect(sz.width, sz.height);
    if (app_hit(btn, x, y)) {
        welcome_mark_first_boot_done();
        app_host_close(APP_WELCOME);
    }
}

void welcome_render(int width, int height, int active) {
    (void)active;
    int dark = ui_is_dark();
    int compact = height < 410;
    u32 accent = app_host_accent_color();

    u32 title_col = dark ? 0xf1f5f9 : 0x302b40;
    u32 sub_col = dark ? 0x94a3b8 : 0x80758f;

    int extra = (height - 410) / 4;
    if (extra < 0) extra = 0;
    if (extra > 60) extra = 60;
    int top = compact ? 38 : 50 + extra;

    /* Accent app badge above the heading, with a soft drop shadow. */
    int badge = compact ? 32 : 40;
    int bx = (width - badge) / 2;
    roundrect(bx + 2, top + 2, badge, badge, badge / 3, dark ? 0x0b0d14 : 0xddd6ea);
    roundrect(bx, top, badge, badge, badge / 3, accent);
    centered(bx, top + (badge - (compact ? 16 : 20)) / 2, badge, "P",
             0xffffff, compact ? 1 : 2);

    int title_y = top + badge + (compact ? 6 : 10);
    centered(0, title_y, width, "PollikOS", title_col, compact ? 2 : 3);
    int sub_y = title_y + (compact ? 22 : 30);
    centered(0, sub_y, width, "Version 0.0.001", sub_col, 1);

    const char *titles[] = {"Your files", "Your space", "Your connection"};
    const char *details[] = {"Saved. Even after restart.", "A softer, simpler desktop.", "A small step online."};
    int card_w = (width - 80) / 3;
    int card_y = compact ? height - 128 : height - 146;
    int card_h = compact ? 62 : 72;

    u32 card_bg = dark ? 0x161a26 : 0xf0ecf6;
    u32 card_title_col = dark ? 0xf1f5f9 : 0x5e4f79;
    u32 card_detail_col = dark ? 0x94a3b8 : 0x8a7e99;

    if (height >= 340 && card_w > 80) {
        for (int k = 0; k < 3; k++) {
            int x = 32 + k * (card_w + 8);
            rounded(x, card_y, card_w, card_h, UI_RADIUS_LARGE, card_bg, 112);
            centered(x, card_y + 10, card_w, titles[k], card_title_col,
                     sys_text_width(titles[k], 2) <= card_w - 16 ? 2 : 1);
            app_text_box((AppRect){x + 8, card_y + 34, card_w - 16, 28}, details[k],
                         card_detail_col, 1, 14);
        }
    }

    /* Start Button */
    AppRect btn = welcome_start_btn_rect(width, height);
    roundrect(btn.x + 2, btn.y + 3, btn.w, btn.h, UI_RADIUS_MEDIUM, dark ? 0x0b0d14 : 0xc7bed4);
    roundrect(btn.x, btn.y, btn.w, btn.h, UI_RADIUS_MEDIUM, accent);
    centered(btn.x, btn.y + (btn.h - 16) / 2, btn.w, "Start", 0xffffff, 2);
}
