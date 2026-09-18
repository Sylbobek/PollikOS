#include "app_internal.h"

static int first_row;
static AppRect files_list(int width, int height) {
    int left = width < 600 ? 146 : 196;
    int rows = (height - 162) / 31;
    if (rows < 1) rows = 1;
    if (rows > FS_FILES) rows = FS_FILES;
    if (first_row > FS_FILES - rows) first_row = FS_FILES - rows;
    return (AppRect){left, 108, width - left - 32, rows * 31};
}
void files_scroll(int delta) {
    GuiAppSize s = gui_app_size(APP_FILES);
    AppRect list = files_list(s.width, s.height);
    int max = FS_FILES - list.h / 31;
    if (delta > max - first_row) first_row = max;
    else if (delta < -first_row) first_row = 0;
    else first_row += delta;
    app_host_invalidate(APP_FILES);
}
void files_render(int width, int height, int active) {
    (void)active;
    AppRect list = files_list(width, height);
    int sidebar_w = list.x - 38;
    roundrect(12, 54, sidebar_w, height - 70, 20, 0xf0edf5);
    app_label(27, 69, sidebar_w - 28, "Favourites", 0x978aa3, 1);
    roundrect(23, 96, sidebar_w - 23, 33, 12, 0xe0d6ef);
    text(36, 103, "Home", 0x6d538f, 2);
    app_label(28, 146, sidebar_w - 28, "Documents", 0x7e718d, 1);
    text(28, 180, "System", 0x7e718d, 1);
    text(list.x + 2, 67, "Your files", 0x40354f, 3);
    for (int row = 0; row < list.h / 31; row++) {
        int i = first_row + row, y = list.y + row * 31;
        if (!files[i].name[0]) continue;
        roundrect(list.x, y, list.w, 27, 11, i == notes_selected_file() ? 0xeae2f4 : 0xf3f0f7);
        app_label(list.x + 13, y + 5, list.w - 120, files[i].name, 0x645274, 1);
        char size[12];
        number(size, files[i].length);
        text(width - 134, y + 5, size, 0x9a8ba8, 1);
        text(width - 90, y + 5, "bytes", 0x9a8ba8, 1);
    }
    app_label(list.x + 7, height - 45, list.w - 14, fs_status, 0x8c8099, 1);
    app_label(list.x + 7, height - 24, list.w - 14,
              height < 410 ? "Wheel: scroll / Terminal: new name.txt" : "Create a file in Terminal: new name.txt", 0x8c8099, 1);
}
static int file_at(int x, int y) {
    GuiAppSize s = gui_app_size(APP_FILES);
    AppRect list = files_list(s.width, s.height);
    if (app_hit(list, x, y) && (y - list.y) % 31 < 27) return first_row + (y - list.y) / 31;
    return -1;
}
void files_click(int x, int y) { notes_open(file_at(x, y)); }
int files_select_at(int x, int y) {
    int slot = file_at(x, y);
    if (slot < 0 || slot >= FS_FILES || !files[slot].name[0]) return -1;
    /* Preserve the historical shared Files selection / Notes save slot. */
    notes_select_file(slot);
    return slot;
}
void files_open_selected(void) { notes_open(notes_selected_file()); }
void files_delete_selected(void) {
    int slot = notes_selected_file();
    if (slot >= 0 && slot < FS_FILES && files[slot].name[0]) {
        fs_remove(slot);
        notes_select_file(-1);
    }
}
const char *files_selected_name(void) {
    int slot = notes_selected_file();
    return slot >= 0 && slot < FS_FILES && files[slot].name[0] ? files[slot].name : 0;
}
