#include "app_internal.h"
#include "../vfs.h"
#include "../ui.h"
#include "../account.h"

static char note[1024] = "Welcome to " OS_LABEL ".\n\nYour files now have a home.\nPress CTRL+S to "
                         "save this note to disk.";
static int note_len, selected_file, note_changed, note_scroll;
static int note_cursor_index, note_selection_anchor = -1, note_dragging;
static int note_caret_row, note_caret_x;
static char note_name[64] = "welcome.txt";
static char notes_vfs_path[128] = "";

void notes_session_clear(void) {
    account_wipe(note,sizeof(note)); account_wipe(notes_vfs_path,sizeof(notes_vfs_path));
    account_wipe(note_name,sizeof(note_name)); copy(note_name,"untitled.txt");
    note_len=selected_file=note_changed=note_scroll=note_cursor_index=note_dragging=0;
    note_selection_anchor=-1; note_caret_row=note_caret_x=0;
}

void notes_init(void) {
    if (files[0].name[0]) {
        copy(note_name, files[0].name);
        copy(note, files[0].data);
    }
    note_len = len(note);
    note_cursor_index = note_len;
    note_selection_anchor = -1;
    if (fs_ready && !files[0].name[0])
        fs_save(0, note_name, note, note_len);
}
int notes_save(void) {
    int saved = 0;
    if (notes_vfs_path[0]) {
        int fd = vfs_open(notes_vfs_path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd >= 0) {
            int written = vfs_write(fd, note, (u32)note_len);
            int closed = vfs_close(fd);
            saved = written == note_len && closed == 0;
        }
    } else {
        saved = fs_save(selected_file, note_name, note, note_len);
    }
    if (saved) note_changed = 0;
    app_host_invalidate(APP_NOTES);
    return saved;
}
static int notes_save_as_path(const char *path) {
    if (!path || path[0] != '/') return 0;
    int n = len(path);
    if (n < 2 || n >= (int)sizeof(notes_vfs_path)) return 0;
    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return 0;
    int written = vfs_write(fd, note, (u32)note_len);
    int closed = vfs_close(fd);
    if (written != note_len || closed != 0) return 0;
    copy(notes_vfs_path, path);
    const char *base = path;
    for (int i = 0; path[i]; i++) if (path[i] == '/') base = path + i + 1;
    int i = 0;
    while (base[i] && i < (int)sizeof(note_name) - 1) { note_name[i] = base[i]; i++; }
    note_name[i] = 0;
    selected_file = -1;
    note_changed = 0;
    app_host_invalidate(APP_NOTES);
    return 1;
}
static void notes_save_as_result(const char *path) {
    if (!path) return;
    if (!notes_save_as_path(path))
        ui_dialog_message("Save failed", "Choose an absolute path on an existing folder.", ICON_WARNING, 0);
}
static void notes_show_save_as(void) {
    ui_dialog_input("Save note as", "Enter a path, for example /home/notes.txt:", "", ICON_FILE,
                    notes_save_as_result);
}
void notes_open(int i) {
    if (i < 0 || i >= FS_FILES || !files[i].name[0]) return;
    if (note_changed && !notes_save()) return;
    notes_vfs_path[0] = 0;
    selected_file = i;
    copy(note_name, files[i].name);
    copy(note, files[i].data);
    note_len = files[i].length;
    note_cursor_index = note_len;
    note_selection_anchor = -1;
    note_changed = 0;
    note_scroll = 0;
    app_host_open(APP_NOTES);
    app_host_invalidate(APP_NOTES);
}
int notes_open_path(const char *path) {
    if (!path || !path[0]) return 0;
    if (note_changed && !notes_save()) return 0;

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return 0;

    char loaded[sizeof(note)];
    memset(loaded, 0, sizeof(loaded));
    int bytes = vfs_read(fd, loaded, sizeof(loaded) - 1);
    int closed = vfs_close(fd);
    if (bytes < 0 || closed < 0) return 0;
    loaded[bytes] = 0;
    memcpy(note, loaded, (u32)bytes + 1);
    note_len = bytes;
    note_cursor_index = note_len;
    note_selection_anchor = -1;

    u32 p = 0;
    while (path[p] && p < 127) { notes_vfs_path[p] = path[p]; p++; }
    notes_vfs_path[p] = 0;

    const char *last_slash = path;
    for (int k = 0; path[k]; k++) {
        if (path[k] == '/') last_slash = &path[k + 1];
    }
    u32 n = 0;
    while (last_slash[n] && n < sizeof(note_name) - 1) {
        note_name[n] = last_slash[n];
        n++;
    }
    note_name[n] = 0;

    selected_file = -1;
    note_changed = 0;
    note_scroll = 0;
    app_host_open(APP_NOTES);
    app_host_invalidate(APP_NOTES);
    return 1;
}
int notes_changed(void) { return note_changed; }
const char *notes_text(void) { return note; }
int notes_selected_file(void) { return selected_file; }
void notes_select_file(int slot) { selected_file = slot; }

static GuiAppSize note_size = {680, 410};
static int note_rows(void) {
    int rows = (note_size.height - 152) / 20;
    return rows > 0 ? rows : 1;
}
static int note_text_width(const char *text) {
    int width = 0;
    while (*text) width += sys_get_glyph_advance((u8)*text++, 1);
    return width;
}
static void note_document_stats(char *out, int capacity) {
    int lines = 1, words = 0, in_word = 0;
    for (int i = 0; i < note_len; i++) {
        u8 ch = (u8)note[i];
        if (ch == '\n') lines++;
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') in_word = 0;
        else if (!in_word) { words++; in_word = 1; }
    }
    char value[12];
    out[0] = 0;
    append_str(out, "Lines ", capacity); number(value, (u32)lines); append_str(out, value, capacity);
    append_str(out, "  Words ", capacity); number(value, (u32)words); append_str(out, value, capacity);
    append_str(out, "  Chars ", capacity); number(value, (u32)note_len); append_str(out, value, capacity);
}
static AppRect note_save_rect(void) {
    return (AppRect){note_size.width - 108, 47, 84, 27};
}
static AppRect note_save_as_rect(void) {
    return (AppRect){note_size.width - 202, 47, 84, 27};
}
/* One walk defines wrapping for painting, cursor following and scroll limits. */
static int note_walk(int paint) {
    int row = 0, x = 0, line = 1, line_start = 1, wrap = note_size.width - 102;
    if (wrap < 1) wrap = 1;
    int select_lo = note_selection_anchor, select_hi = note_cursor_index;
    if (select_lo > select_hi) { int t = select_lo; select_lo = select_hi; select_hi = t; }
    note_caret_row = note_caret_x = 0;
    for (int i = 0; i <= note_len; i++) {
        if (i == note_len) {
            note_caret_row = row; note_caret_x = x;
            break;
        }
        u8 ch = note[i];
        if (ch == '\n') {
            if (i == note_cursor_index) { note_caret_row = row; note_caret_x = x; }
            if (paint && note_selection_anchor >= 0 && i >= select_lo && i < select_hi) {
                int screenrow = row - note_scroll;
                if (screenrow >= 0 && screenrow < note_rows())
                    rect(64 + x, 91 + screenrow * 20, 5, 17,
                         ui_is_dark() ? 0x334766 : 0xd7e6fa);
            }
            row++; line++; x = 0; line_start = 1;
            int screenrow = row - note_scroll;
            if (paint && screenrow >= 0 && screenrow < note_rows()) {
                char line_num[12]; number(line_num, (u32)line);
                app_label(22, 95 + screenrow * 20, 28, line_num,
                          ui_is_dark() ? 0x71809a : 0x9aa3b5, 1);
                line_start = 0;
            } else {
                line_start = 1;
            }
            continue;
        }
        if (ch < 32 || ch >= 127) continue;
        int advance = sys_get_glyph_advance(ch, 2);
        if (x + advance > wrap) { row++; x = 0; line_start = 0; }
        if (i == note_cursor_index) { note_caret_row = row; note_caret_x = x; }
        int screenrow = row - note_scroll;
        if (paint && screenrow >= 0 && screenrow < note_rows()) {
            u32 text_col = ui_is_dark() ? 0xf1f5f9 : 0x50445e;
            if (note_selection_anchor >= 0 && i >= select_lo && i < select_hi)
                roundrect(64 + x, 90 + screenrow * 20, advance, 18, 2,
                          ui_is_dark() ? 0x334766 : 0xd7e6fa);
            if (line_start) {
                char line_num[12]; number(line_num, (u32)line);
                app_label(22, 95 + screenrow * 20, 28, line_num,
                          ui_is_dark() ? 0x71809a : 0x9aa3b5, 1);
                line_start = 0;
            }
            letter(64 + x, 91 + screenrow * 20, ch, text_col, 2);
        }
        x += advance;
    }
    if (paint && note_caret_row >= note_scroll && note_caret_row < note_scroll + note_rows()) {
        u32 cursor_col = ui_is_dark() ? 0x60a5fa : 0x9470bd;
        rect(64 + note_caret_x, 93 + (note_caret_row - note_scroll) * 20, 2, 15, cursor_col);
    }
    return row;
}
static int note_max_scroll(void) {
    int max = note_walk(0) + 1 - note_rows();
    return max > 0 ? max : 0;
}
static void note_follow(void) {
    note_walk(0);
    int row = note_caret_row;
    if (row < note_scroll) note_scroll = row;
    if (row >= note_scroll + note_rows()) note_scroll = row - note_rows() + 1;
}
static int note_index_at(int px, int py) {
    int target_row = note_scroll + (py - 91) / 20;
    if (py < 91) target_row = note_scroll;
    if (target_row < 0) target_row = 0;
    int target_x = px - 64;
    if (target_x < 0) target_x = 0;
    int row = 0, x = 0, wrap = note_size.width - 102;
    if (wrap < 1) wrap = 1;
    for (int i = 0; i < note_len; i++) {
        u8 ch = note[i];
        if (ch == '\n') {
            if (row == target_row) return i;
            row++; x = 0;
            continue;
        }
        if (ch < 32 || ch >= 127) continue;
        int advance = sys_get_glyph_advance(ch, 2);
        if (x + advance > wrap) { row++; x = 0; }
        if (row > target_row) return i;
        if (row == target_row && target_x < x + advance / 2) return i;
        x += advance;
    }
    return note_len;
}
static int note_has_selection(void) {
    return note_selection_anchor >= 0 && note_selection_anchor != note_cursor_index;
}
static void note_delete_selection(void) {
    if (!note_has_selection()) { note_selection_anchor = -1; return; }
    int a = note_selection_anchor, b = note_cursor_index;
    if (a > b) { int t = a; a = b; b = t; }
    memmove(note + a, note + b, (u32)(note_len - b));
    note_len -= b - a;
    note[note_len] = 0;
    note_cursor_index = a;
    note_selection_anchor = -1;
}
static int note_insert(const char *text, int count) {
    if (!text || count <= 0) return 0;
    note_delete_selection();
    if (count > 1023 - note_len) count = 1023 - note_len;
    if (count <= 0) return 0;
    memmove(note + note_cursor_index + count, note + note_cursor_index,
            (u32)(note_len - note_cursor_index));
    memcpy(note + note_cursor_index, text, (u32)count);
    note_len += count;
    note_cursor_index += count;
    note[note_len] = 0;
    note_changed = 1;
    return 1;
}
static void note_move_to(int next, int extend) {
    if (next < 0) next = 0;
    if (next > note_len) next = note_len;
    if (extend) {
        if (note_selection_anchor < 0) note_selection_anchor = note_cursor_index;
    } else note_selection_anchor = -1;
    note_cursor_index = next;
}
void notes_resized(int width, int height) {
    /* Preserve reading position where possible; typing still follows the end. */
    note_size = (GuiAppSize){width, height};
    int max = note_max_scroll();
    if (note_scroll > max) note_scroll = max;
}
int notes_cursor(int x, int y) {
    return app_hit(note_save_rect(), x, y) || app_hit(note_save_as_rect(), x, y) ||
           app_hit((AppRect){14, 82, note_size.width - 28, note_rows() * 20 + 20}, x, y);
}
void notes_render(int width, int height, int active) {
    (void)active;
    if (width != note_size.width || height != note_size.height) notes_resized(width, height);
    int dark = ui_is_dark();
    u32 panel = dark ? 0x171d2b : 0xf3f0f8;
    u32 border = dark ? 0x2b3549 : 0xd9d3e2;
    u32 paper = dark ? 0x101521 : 0xfffeff;
    roundrect(14, 43, width - 28, 36, 9, panel);
    rect(14, 78, width - 28, 1, border);
    roundrect(23, 54, 14, 14, 4, dark ? 0x33415a : 0xe2d9ef);
    rect(27, 58, 6, 1, dark ? 0x93a4bf : 0x9684ad);
    rect(27, 61, 6, 1, dark ? 0x93a4bf : 0x9684ad);
    rect(27, 64, 4, 1, dark ? 0x93a4bf : 0x9684ad);
    app_label(45, 55, note_save_as_rect().x - 54, note_name,
              dark ? 0xe2e8f0 : 0x50445e, 1);
    if (note_changed) roundrect(45, 69, 5, 5, 2, dark ? 0xfbbf24 : 0xb7791f);

    int editor_h = height - 136;
    if (editor_h < 40) editor_h = 40;
    roundrect(14, 84, width - 28, editor_h, 10, paper);
    rect(14, 84, width - 28, 1, border);
    rect(14, 84 + editor_h - 1, width - 28, 1, border);
    rect(14, 84, 1, editor_h, border);
    rect(width - 15, 84, 1, editor_h, border);
    rect(54, 91, 1, editor_h - 14, border);
    note_walk(1);
    AppRect save = note_save_rect();
    AppRect save_as = note_save_as_rect();
    roundrect(save_as.x, save_as.y, save_as.w, save_as.h, 8, dark ? 0x242c3c : 0xe9e4f0);
    centered(save_as.x, save_as.y + 6, save_as.w, "Save As", dark ? 0xcbd5e1 : 0x6e568a, 1);
    roundrect(save.x, save.y, save.w, save.h, 8, dark ? 0x33415a : 0xe5dcf1);
    centered(save.x, save.y + 6, save.w, "Save", dark ? 0xf1f5f9 : 0x6e568a, 1);
    roundrect(14, height - 42, width - 28, 25, 8, panel);
    char stats[64];
    note_document_stats(stats, sizeof(stats));
    int stats_width = note_text_width(stats);
    int stats_x = width - 24 - stats_width;
    int left_width = stats_x - 36;
    if (left_width < 0) left_width = 0;
    if (note_changed) roundrect(24, height - 34, 5, 5, 2, dark ? 0xfbbf24 : 0xb7791f);
    const char *state = note_changed ? "Unsaved changes" : (fs_status[0] ? fs_status : "Saved");
    app_label(36, height - 35, left_width, state,
              dark ? 0x94a3b8 : 0x82778f, 1);
    if (stats_x >= 36) app_label(stats_x, height - 35, stats_width, stats,
                                  dark ? 0x94a3b8 : 0x82778f, 1);
}
void notes_click(int x, int y) {
    if (app_hit(note_save_rect(), x, y)) { notes_save(); return; }
    if (app_hit(note_save_as_rect(), x, y)) { notes_show_save_as(); return; }
    if (app_hit((AppRect){14, 82, note_size.width - 28, note_rows() * 20 + 20}, x, y)) {
        note_cursor_index = note_index_at(x, y);
        note_selection_anchor = -1;
        note_dragging = 1;
        note_follow();
    }
}
int notes_drag(int x, int y, int active) {
    if (!active) { note_dragging = 0; return 0; }
    if (!note_dragging) return -1;
    int next = note_index_at(x, y);
    if (next == note_cursor_index) return 0;
    if (note_selection_anchor < 0) note_selection_anchor = note_cursor_index;
    note_cursor_index = next;
    note_follow();
    return 1;
}
void notes_scroll(int delta) {
    int max = note_max_scroll();
    if (delta > max - note_scroll) note_scroll = max;
    else if (delta < -note_scroll) note_scroll = 0;
    else note_scroll += delta;
    app_host_invalidate(3);
}
void notes_key_ex(u8 code, char ch, int shift, int control) {
    if (control && code == 31) {
        if (shift) notes_show_save_as(); else notes_save();
        return;
    }
    if (control && code == 30) {
        note_selection_anchor = 0;
        note_cursor_index = note_len;
        note_follow();
        app_host_invalidate(APP_NOTES);
        return;
    }
    if (control && (code == 46 || code == 45)) {
        if (note_has_selection()) {
            int a = note_selection_anchor, b = note_cursor_index;
            if (a > b) { int t = a; a = b; b = t; }
            app_clipboard_copy(note + a, b - a);
            if (code == 45) { note_delete_selection(); note_changed = 1; note_follow(); }
            app_host_invalidate(APP_NOTES);
        }
        return;
    }
    if (control && code == 47) {
        char pasted[1024];
        int count = app_clipboard_paste(pasted, sizeof(pasted));
        if (note_insert(pasted, count)) { note_follow(); app_host_invalidate(APP_NOTES); }
        return;
    }
    if (code == 73 || code == 81) { notes_scroll(code == 73 ? -10 : 10); return; }
    if (code == 75 || code == 77) {
        note_move_to(note_cursor_index + (code == 75 ? -1 : 1), shift);
        note_follow(); app_host_invalidate(APP_NOTES); return;
    }
    if (code == 71 || code == 79) {
        int start = note_cursor_index, end = note_cursor_index;
        while (start > 0 && note[start - 1] != '\n') start--;
        while (end < note_len && note[end] != '\n') end++;
        note_move_to(code == 71 ? start : end, shift);
        note_follow(); app_host_invalidate(APP_NOTES); return;
    }
    if (code == 72 || code == 80) {
        note_walk(0);
        int row = note_caret_row + (code == 72 ? -1 : 1);
        if (row < 0) row = 0;
        int next = note_index_at(64 + note_caret_x, 91 + (row - note_scroll) * 20 + 2);
        note_move_to(next, shift);
        note_follow(); app_host_invalidate(APP_NOTES); return;
    }
    if (code == 14 || code == 83) {
        int changed = 0;
        if (note_has_selection()) { note_delete_selection(); changed = 1; }
        else if (code == 14 && note_cursor_index > 0) {
            memmove(note + note_cursor_index - 1, note + note_cursor_index,
                    (u32)(note_len - note_cursor_index));
            note_cursor_index--; note_len--; note[note_len] = 0; changed = 1;
        } else if (code == 83 && note_cursor_index < note_len) {
            memmove(note + note_cursor_index, note + note_cursor_index + 1,
                    (u32)(note_len - note_cursor_index - 1));
            note_len--; note[note_len] = 0; changed = 1;
        }
        note_selection_anchor = -1;
        if (changed) note_changed = 1;
        note_follow(); app_host_invalidate(APP_NOTES); return;
    }
    if (code == 28) {
        if (note_insert("\n", 1)) { note_follow(); app_host_invalidate(APP_NOTES); }
        return;
    }
    if (ch && !control) {
        if (note_insert(&ch, 1)) { note_follow(); app_host_invalidate(APP_NOTES); }
        return;
    }
}
void notes_key(u8 code, char ch, int control) {
    /* Compatibility entry point retained for older in-kernel callers that
     * used append-only editing. The GUI registry uses notes_key_ex(). */
    if (ch && !control) { note_cursor_index = note_len; note_selection_anchor = -1; }
    notes_key_ex(code, ch, 0, control);
}
