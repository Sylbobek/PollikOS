#include "app_internal.h"

void welcome_render(int width, int height, int active) {
    (void)active;
    int compact = height < 410, logo = compact ? 48 : 70;
    int top = compact ? 46 : 66 + (height - 410 > 0 ? (height - 410) / 4 : 0);
    roundrect((width - logo) / 2, top, logo, logo, compact ? 16 : 23, 0x8472cc);
    centered(0, top + 9, width, "P", 0xffffff, compact ? 3 : 4);
    centered(0, top + logo + (compact ? 8 : 19), width, OS_NAME, 0x302b40, compact ? 3 : 4);
    centered(0, top + logo + (compact ? 40 : 71), width, OS_VERSION, 0x80758f, 2);
    const char *titles[] = {"Your files", "Your space", "Your connection"};
    const char *details[] = {"Saved. Even after restart.", "A softer, simpler desktop.", "A small step online."};
    int card_w = (width - 80) / 3, card_y = compact ? height - 110 : height - 142;
    int card_h = compact ? 72 : 76;
    for (int k = 0; k < 3; k++) {
        int x = 32 + k * (card_w + 8);
        roundrect(x, card_y, card_w, card_h, 18, 0xf0ecf6);
        centered(x, card_y + 12, card_w, titles[k], 0x5e4f79,
                 sys_text_width(titles[k], 2) <= card_w - 16 ? 2 : 1);
        app_text_box((AppRect){x + 8, card_y + 40, card_w - 16, 30}, details[k], 0x8a7e99, 1, 15);
    }
    centered(0, height - 35, width, width < 600 ? "Dock / F1-F6 apps / Esc desktop" :
             "Choose an app in the dock  /  F1-F6 apps  /  Esc desktop", 0x93869f, 1);
}
