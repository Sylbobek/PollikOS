#include "app_internal.h"

static char note[1024] = "Welcome to " OS_LABEL ".\n\nYour files now have a home.\nPress CTRL+S to "
                         "save this note to disk.";
static int note_len, selected_file, note_changed, note_scroll;
static char note_name[24] = "welcome.txt";

void notes_init(void) {
    if (files[0].name[0]) {
        copy(note_name, files[0].name);
        copy(note, files[0].data);
    }
    note_len = len(note);
    if (fs_ready && !files[0].name[0])
        fs_save(0, note_name, note, note_len);
}
int notes_save(void) {
    int saved = fs_save(selected_file, note_name, note, note_len);
    if (saved) note_changed = 0;
    app_host_invalidate(3);
    return saved;
}
void notes_open(int i) {
    if (i < 0 || i >= FS_FILES || !files[i].name[0]) return;
    if (note_changed && !notes_save()) return;
    selected_file = i;
    copy(note_name, files[i].name);
    copy(note, files[i].data);
    note_len = files[i].length;
    note_changed = 0;
    note_scroll = 0;
    app_host_open(3);
    app_host_invalidate(3);
}
int notes_changed(void) { return note_changed; }
const char *notes_text(void) { return note; }
int notes_selected_file(void) { return selected_file; }
void notes_select_file(int slot) { selected_file = slot; }

static GuiAppSize note_size = {680, 410};
static int note_rows(void) {
    int rows = (note_size.height - 150) / 20;
    return rows > 0 ? rows : 1;
}
static AppRect note_save_rect(void) {
    return (AppRect){note_size.width - 121, note_size.height - 45, 96, 31};
}
/* One walk defines wrapping for painting, cursor following and scroll limits. */
static int note_walk(int paint) {
    int row = 0, x = 0, wrap = note_size.width - 70;
    if (wrap < 1) wrap = 1;
    for (int i = 0; i < note_len; i++) {
        u8 ch = note[i];
        if (ch == '\n') { row++; x = 0; continue; }
        if (ch < 32 || ch >= 127) continue;
        int advance = sys_get_glyph_advance(ch, 2);
        if (x + advance > wrap) { row++; x = 0; }
        int screenrow = row - note_scroll;
        if (paint && screenrow >= 0 && screenrow < note_rows())
            letter(25 + x, 89 + screenrow * 20, ch, 0x50445e, 2);
        x += advance;
    }
    if (paint && row >= note_scroll && row < note_scroll + note_rows())
        rect(26 + x, 91 + (row - note_scroll) * 20, 1, 15, 0x9470bd);
    return row;
}
static int note_max_scroll(void) {
    int max = note_walk(0) + 1 - note_rows();
    return max > 0 ? max : 0;
}
static void note_follow(void) {
    int row = note_walk(0);
    if (row < note_scroll) note_scroll = row;
    if (row >= note_scroll + note_rows()) note_scroll = row - note_rows() + 1;
}
void notes_resized(int width, int height) {
    /* Preserve reading position where possible; typing still follows the end. */
    note_size = (GuiAppSize){width, height};
    int max = note_max_scroll();
    if (note_scroll > max) note_scroll = max;
}
int notes_cursor(int x, int y) {
    return app_hit((AppRect){24, 81, note_size.width - 57, note_rows() * 20 + 10}, x, y);
}
void notes_render(int width, int height, int active) {
    (void)active;
    if (width != note_size.width || height != note_size.height) notes_resized(width, height);
    text(25, 60, note_name, 0x9a85a9, 1);
    note_walk(1);
    AppRect save = note_save_rect();
    app_label(25, height - 30, save.x - 40,
              note_changed ? "Unsaved  /  Ctrl+S save  /  PgUp PgDn scroll" : fs_status, 0x9a8b9e, 1);
    roundrect(save.x, save.y, save.w, save.h, 14, 0xe5dcf1);
    centered(save.x, save.y + 7, save.w, "Save", 0x7a5b99, 1);
}
void notes_click(int x, int y) {
    if (app_hit(note_save_rect(), x, y)) notes_save();
}
void notes_scroll(int delta) {
    int max = note_max_scroll();
    if (delta > max - note_scroll) note_scroll = max;
    else if (delta < -note_scroll) note_scroll = 0;
    else note_scroll += delta;
    app_host_invalidate(3);
}
void notes_key(u8 code, char ch, int control) {
    if (control && code == 31) { notes_save(); return; }
    if (code == 73 || code == 81) { notes_scroll(code == 73 ? -10 : 10); return; }
    if (app_edit_key(note, &note_len, 1023, code, ch, control)) {
        note_changed = 1;
        note_follow();
    }
}
