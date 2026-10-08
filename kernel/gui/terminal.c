#include "../../common/calc.h"
#include "app_internal.h"
#include "../hw.h"
#include "../audio.h"
#include "../net/http.h"
#include "../pmm.h"
#include "../vfs.h"
#include "../hal.h"
#include "../auth.h"
#include "../account.h"
#include "../process.h"
#include "../pollikfs.h"
#include "../net/net_manager.h"

enum { TERM_HISTORY = 64, TERM_SCROLLBACK = 32768, TERM_COMMAND_LENGTH = 160 };
#define TERM_TEXT_COLOR 0xd9dce9u
#define TERM_INPUT_TEXT_COLOR 0xf4f3fau
#define TERM_ACCENT_COLOR 0xc8b6efu
#define TERM_HINT_COLOR 0x85899bu
#define TERM_SELECTION_COLOR 0x514267u
#define TERM_SELECTION_TEXT 0xffffffu
#define TERM_SCROLL_TRACK 0x343643u
#define TERM_SCROLL_THUMB 0x777184u
#define TERM_LINE_HEIGHT 20
static char command[TERM_COMMAND_LENGTH], output[TERM_SCROLLBACK];
static char transcript[TERM_SCROLLBACK] =
    "PollikOS Terminal v0.0.001\n"
    "Type 'help' for available commands\n";
static char render_buffer[TERM_SCROLLBACK + VFS_MAX_PATH + TERM_COMMAND_LENGTH + 16];
static char history[TERM_HISTORY][TERM_COMMAND_LENGTH];
static char cwd[VFS_MAX_PATH] = "/home/Desktop";
static int cmdlen, cmdcursor, history_count, history_pos, scroll_lines;
static int selection_anchor = -1, selection_focus = -1, selection_drag;
static int terminal_scroll_by(int delta);

void terminal_session_clear(void) {
    account_wipe(command,sizeof(command)); account_wipe(output,sizeof(output));
    account_wipe(transcript,sizeof(transcript)); account_wipe(render_buffer,sizeof(render_buffer));
    account_wipe(history,sizeof(history));
    copy(cwd,"/home/Desktop"); cmdlen=cmdcursor=history_count=history_pos=scroll_lines=0;
    selection_anchor=selection_focus=-1; selection_drag=0;
}

static void terminal_clear_selection(void) {
    selection_anchor = selection_focus = -1;
    selection_drag = 0;
}

static int terminal_build_buffer(int *prompt_at, int *command_at) {
    char prompt[VFS_MAX_PATH + 16];
    copy(prompt, "pollik:");
    append_str(prompt, cwd, sizeof(prompt));
    append_str(prompt, "$ ", sizeof(prompt));
    copy(render_buffer, transcript);
    int used = len(transcript);
    if (prompt_at) *prompt_at = used;
    for (int i = 0; prompt[i] && used < (int)sizeof(render_buffer) - 1; i++)
        render_buffer[used++] = prompt[i];
    if (command_at) *command_at = used;
    for (int i = 0; i < cmdlen && used < (int)sizeof(render_buffer) - 1; i++)
        render_buffer[used++] = command[i];
    render_buffer[used] = 0;
    return used;
}

static void append_transcript(const char *s) {
    int used = len(transcript), add = len(s);
    if (add >= TERM_SCROLLBACK) { s += add - TERM_SCROLLBACK + 1; add = len(s); used = 0; }
    if (used + add >= TERM_SCROLLBACK) {
        int drop = used + add - TERM_SCROLLBACK + 1;
        while (drop < used && transcript[drop] != '\n') drop++;
        if (drop < used) drop++;
        memmove(transcript, transcript + drop, (unsigned)(used - drop + 1));
        used -= drop;
    }
    while (*s && used < TERM_SCROLLBACK - 1) transcript[used++] = *s++;
    transcript[used] = 0;
}
static inline int parse_ansi_color(const char *buf, int max_len, int *skip_len, u32 *out_color) {
    if (max_len < 3 || buf[0] != '\033' || buf[1] != '[') return 0;
    int k = 2;
    int code = 0;
    int is_bold = 0;
    u32 color = TERM_TEXT_COLOR;
    int has_code = 0;

    while (k < max_len && buf[k] && buf[k] != 'm') {
        if (buf[k] >= '0' && buf[k] <= '9') {
            code = code * 10 + (buf[k] - '0');
            has_code = 1;
        } else if (buf[k] == ';') {
            if (code == 1) is_bold = 1;
            else if (code == 0) color = TERM_TEXT_COLOR;
            code = 0;
        }
        k++;
    }
    if (k < max_len && buf[k] == 'm') {
        k++;
        if (has_code) {
            if (code == 0) color = TERM_TEXT_COLOR;
            else if (code == 1) is_bold = 1;
            else if (code == 30) color = 0x475569;
            else if (code == 31) color = is_bold ? 0xf87171 : 0xef4444;
            else if (code == 32) color = is_bold ? 0x4ade80 : 0x22c55e;
            else if (code == 33) color = is_bold ? 0xfde047 : 0xf59e0b;
            else if (code == 34) color = is_bold ? 0x60a5fa : 0x3b82f6;
            else if (code == 35) color = is_bold ? 0xc084fc : 0xa855f7;
            else if (code == 36) color = is_bold ? 0x22d3ee : 0x06b6d4;
            else if (code == 37) color = is_bold ? 0xffffff : 0xe2e8f0;
            else if (code == 90) color = 0x94a3b8;
            else if (code == 91) color = 0xf87171;
            else if (code == 92) color = 0x4ade80;
            else if (code == 93) color = 0xfde047;
            else if (code == 94) color = 0x60a5fa;
            else if (code == 95) color = 0xc084fc;
            else if (code == 96) color = 0x22d3ee;
            else if (code == 97) color = 0xffffff;
        }
        *skip_len = k;
        *out_color = color;
        return 1;
    }
    return 0;
}

static int terminal_char_width(char ch) {
    u8 c = (u8)ch;
    return c >= 32 && c < 127 ? sys_get_glyph_advance(c, 1) : 0;
}
static int visual_lines(const char *s, int width) {
    int lines = 1, x = 0;
    while (*s) {
        if (*s == '\033' && *(s + 1) == '[') {
            s += 2;
            while (*s && *s != 'm') s++;
            if (*s == 'm') s++;
            continue;
        }
        char ch = *s++;
        if (ch == '\n') { lines++; x = 0; continue; }
        int advance = terminal_char_width(ch);
        if (advance && x && x + advance > width) { lines++; x = 0; }
        x += advance;
    }
    return lines;
}
static void terminal_layout(int width, int height, int *left, int *right, int *top,
                            int *bottom, int *visible, int *first, int *total) {
    *left = 18; *right = width - 24; *top = 48; *bottom = height - 36;
    if (*right - *left < 1) *right = *left + 1;
    if (*bottom - *top < TERM_LINE_HEIGHT) *bottom = *top + TERM_LINE_HEIGHT;
    *visible = (*bottom - *top) / TERM_LINE_HEIGHT;
    if (*visible < 1) *visible = 1;
    *total = visual_lines(render_buffer, *right - *left);
    int max_scroll = *total - *visible;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_lines > max_scroll) scroll_lines = max_scroll;
    if (scroll_lines < 0) scroll_lines = 0;
    *first = max_scroll - scroll_lines;
}

static void draw_console(int width, int height) {
    int prompt_at, command_at, used = terminal_build_buffer(&prompt_at, &command_at);
    int caret_at = command_at + cmdcursor;
    int left, right, top, bottom, visible, first, total;
    terminal_layout(width, height, &left, &right, &top, &bottom, &visible, &first, &total);
    int select_from = selection_anchor, select_to = selection_focus;
    if (select_from > select_to) { int tmp = select_from; select_from = select_to; select_to = tmp; }
    int has_selection = select_from >= 0 && select_to > select_from;

    int line = 0, x = left, y = top - first * TERM_LINE_HEIGHT, caret_x = -1, caret_y = -1;
    u32 current_fg = TERM_TEXT_COLOR;
    for (int i = 0; i <= used; i++) {
        if (render_buffer[i] == '\033' && render_buffer[i + 1] == '[') {
            int skip = 0;
            u32 c = current_fg;
            if (parse_ansi_color(&render_buffer[i], used - i, &skip, &c)) {
                current_fg = c;
                i += skip - 1;
                continue;
            }
        }
        char ch = render_buffer[i];
        if (!ch) {
            if (i == caret_at) { caret_x = x; caret_y = y; }
            break;
        }
        if (ch == '\n') {
            if (has_selection && i >= select_from && i < select_to && line >= first && line < first + visible)
                rect(x, y + 1, 5, 15, TERM_SELECTION_COLOR);
            line++; x = left; y += TERM_LINE_HEIGHT; continue;
        }
        int advance = terminal_char_width(ch);
        if (advance && x > left && x + advance > right) {
            line++; x = left; y += TERM_LINE_HEIGHT;
        }
        if (i == caret_at) { caret_x = x; caret_y = y; }
        if (line >= first && line < first + visible && advance) {
            u32 color = current_fg;
            if (i >= prompt_at && i < command_at) {
                int rel = i - prompt_at;
                if (rel < 7) color = 0x4ade80; /* green for pollik: */
                else if (rel < (command_at - prompt_at) - 2) color = 0x60a5fa; /* blue for path */
                else color = 0xf59e0b; /* amber for $ */
            } else if (i >= command_at) {
                color = TERM_INPUT_TEXT_COLOR;
            }
            if (has_selection && i >= select_from && i < select_to) {
                rect(x, y + 1, advance, 15, TERM_SELECTION_COLOR);
                color = TERM_SELECTION_TEXT;
            }
            letter(x, y, (u8)ch, color, 1);
        }
        x += advance;
    }
    if (caret_at == used && x >= right) { caret_x = left; caret_y = y + TERM_LINE_HEIGHT; }
    if (scroll_lines == 0 && caret_x >= left && caret_x < right &&
        caret_y >= top && caret_y + 16 <= bottom)
        rect(caret_x, caret_y, 2, 16, TERM_ACCENT_COLOR);

    if (total > visible) {
        int track_h = visible * TERM_LINE_HEIGHT;
        int thumb_h = track_h * visible / total;
        if (thumb_h < 12) thumb_h = 12;
        int max_scroll = total - visible;
        int thumb_y = top + (track_h - thumb_h) * first / max_scroll;
        rect(width - 11, top, 2, track_h, TERM_SCROLL_TRACK);
        rect(width - 12, thumb_y, 4, thumb_h, TERM_SCROLL_THUMB);
    }
    if (height > 80) {
        rect(16, height - 29, width - 32, 1, TERM_SCROLL_TRACK);
        app_label(18, height - 23, width - 36,
                  "Ctrl+C kopiuj   Ctrl+V wklej   PgUp/PgDn przewijanie",
                  TERM_HINT_COLOR, 1);
    }
}
void terminal_render(int width, int height, int active) {
    (void)active;
    rect(0, 34, width, height - 34, 0x000000);
    draw_console(width, height);
}

static int terminal_index_at(int width, int height, int mouse_x, int mouse_y) {
    int used = terminal_build_buffer(0, 0);
    int left, right, top, bottom, visible, first, total;
    terminal_layout(width, height, &left, &right, &top, &bottom, &visible, &first, &total);
    int target_line = first;
    if (mouse_y >= bottom) target_line += visible - 1;
    else if (mouse_y > top) target_line += (mouse_y - top) / TERM_LINE_HEIGHT;
    if (target_line >= total) return used;
    int target_x = mouse_x - left;
    if (target_x < 0) target_x = 0;
    int line = 0, x = 0;
    for (int i = 0; i < used; i++) {
        if (render_buffer[i] == '\033' && render_buffer[i + 1] == '[') {
            int skip = 2;
            while (i + skip < used && render_buffer[i + skip] != 'm') skip++;
            if (i + skip < used && render_buffer[i + skip] == 'm') {
                i += skip;
                continue;
            }
        }
        char ch = render_buffer[i];
        if (ch == '\n') {
            if (line == target_line) return i;
            line++; x = 0; continue;
        }
        int advance = terminal_char_width(ch);
        if (advance && x && x + advance > right - left) { line++; x = 0; }
        if (line == target_line) {
            if (target_x < x + advance / 2) return i;
            if (target_x < x + advance) return i + 1;
        }
        x += advance;
    }
    return used;
}

void terminal_click(int x, int y) {
    int width = gui_app_size(APP_TERMINAL).width;
    int height = gui_app_size(APP_TERMINAL).height;
    if (y < 48 || y >= height - 36 || x < 18 || x >= width - 24) {
        terminal_clear_selection();
        return;
    }
    selection_anchor = selection_focus = terminal_index_at(width, height, x, y);
    selection_drag = 1;
}

int terminal_drag(int x, int y, int active) {
    if (!active) { selection_drag = 0; return 0; }
    if (!selection_drag) return -1;
    int width = gui_app_size(APP_TERMINAL).width;
    int height = gui_app_size(APP_TERMINAL).height;
    int old_scroll = scroll_lines, old_focus = selection_focus;
    if (y < 48) terminal_scroll_by(-3);
    else if (y >= height - 36) terminal_scroll_by(3);
    selection_focus = terminal_index_at(width, height, x, y);
    return old_scroll != scroll_lines || old_focus != selection_focus;
}

static void path_resolve(const char *src, char *dst) {
    char joined[VFS_MAX_PATH]; int n = 0;
    if (!src || !*src) src = cwd;
    if (*src != '/') {
        while (cwd[n] && n < VFS_MAX_PATH-2) { joined[n] = cwd[n]; n++; }
        if (n > 1) joined[n++] = '/';
    }
    while (*src && n < VFS_MAX_PATH-1) joined[n++] = *src++;
    joined[n] = 0; n = 0; dst[n++] = '/';
    for (int i = 0; joined[i];) {
        while (joined[i] == '/') i++;
        int begin = i; while (joined[i] && joined[i] != '/') i++;
        int size = i - begin;
        if (!size || (size == 1 && joined[begin] == '.')) continue;
        if (size == 2 && joined[begin] == '.' && joined[begin+1] == '.') {
            if (n > 1) { n--; while (n > 1 && dst[n-1] != '/') n--; }
            continue;
        }
        if (n > 1 && n < VFS_MAX_PATH-1) dst[n++] = '/';
        for (int j = 0; j < size && n < VFS_MAX_PATH-1; j++) dst[n++] = joined[begin+j];
    }
    dst[n] = 0;
}
static void history_add(const char *s) {
    if (!*s) return;
    if (history_count && eq(history[history_count-1], s)) { history_pos = history_count; return; }
    if (history_count == TERM_HISTORY) {
        for (int i=1;i<TERM_HISTORY;i++) copy(history[i-1], history[i]);
        history_count--;
    }
    copy(history[history_count++], s); history_pos = history_count;
}
static void history_show(int pos) {
    history_pos = pos;
    if (pos == history_count) command[0] = 0;
    else copy(command, history[pos]);
    cmdlen = cmdcursor = len(command);
}
static int split_pair(char *args, char **first, char **second) {
    while (*args == ' ') args++;
    if (!*args) return 0;
    *first = args;
    while (*args && *args != ' ') args++;
    if (!*args) return 0;
    *args++ = 0;
    while (*args == ' ') args++;
    if (!*args) return 0;
    *second = args;
    while (*args && *args != ' ') args++;
    if (*args) {
        *args++ = 0;
        while (*args == ' ') args++;
        if (*args) return 0;
    }
    return 1;
}
static void terminal_tcc_help(char *out) {
    copy(out, "TinyCC 0.9.27 for PollikOS x86_64\n"
              "GUI i386 terminal cannot execute the ELF64 compiler. In the x86_64 console:\n"
              "  tcc hello.c -o hello       compile and link\n"
              "  tcc -c hello.c -o hello.o  compile an object\n"
              "  tcc -E hello.c             preprocess\n"
              "  tcc a.c b.c -o app         build multiple files\n"
              "  ./hello                    run the program\n"
              "PollikOS TinyCC supports integer C; floating point and -m32/-m64 cross mode are unsupported.");
}
static int gui_builtin_exists(const char *name) {
    static const char *commands[] = {
        "help", "about", "version", "ver", "gfx", "mem", "free", "pwd", "cd",
        "ls", "dir", "cat", "type", "stat", "mkdir", "touch", "rm", "del", "remove",
        "rmdir", "mv", "rename", "cp", "echo", "which", "history", "clear", "cls",
        "new", "open", "save", "ps", "tasks", "pause", "resume", "kill", "spawn",
        "faulttest", "net", "ping", "publicip", "time", "date", "uptime", "lspci",
        "beep", "sound", "audio", "shutdown", "reboot", "lock", "logout", "passwd", "theme", "anim", "animations", "perf", "tcc", "cc", "calc", "calculator"
    };
    for (u32 i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
        if (eq(name, commands[i])) return 1;
    return 0;
}
static int valid_name(const char *s) {
    int n = len(s);
    if (n < 1 || n > 23) return 0;
    for (int i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '.' ||
              s[i] == '_' || s[i] == '-')) return 0;
    return 1;
}
static void execute(void) {
    char entered[TERM_COMMAND_LENGTH];
    output[0] = 0;
    copy(entered, command);
    history_add(entered);
    char *arg = command;
    while (*arg && *arg != ' ') arg++;
    if (*arg) *arg++ = 0;
    while (*arg == ' ') arg++;
    if (eq(command, "dir")) copy(command, "ls");
    else if (eq(command, "type")) copy(command, "cat");
    else if (eq(command, "del") || eq(command, "remove")) copy(command, "rm");
    else if (eq(command, "rename")) copy(command, "mv");
    else if (eq(command, "cls")) copy(command, "clear");
    else if (eq(command, "ver")) copy(command, "version");
    if(eq(command,"lock")) { auth_lock(); copy(output,"Session locked."); }
    else if(eq(command,"logout")) { auth_logout(); copy(output,"Session ended."); }
    else if(eq(command,"passwd")) { auth_change_password(); copy(output,"Enter the current password on the login screen."); }
    else if (eq(command, "help")) {
        if (eq(arg, "tcc") || eq(arg, "cc")) terminal_tcc_help(output);
        else copy(output,
            "FILES:    pwd cd ls/dir cat/type mkdir touch rm/del rmdir mv/rename cp stat\n"
            "\033[1;35mSYSTEM:\033[0m   pollikfetch about version gfx mem/free ps/tasks pause resume\n"
            "          kill spawn faulttest uptime time date lspci beep sound reboot shutdown\n"
            "\033[1;32mNETWORK:\033[0m  net ping publicip\n"
            "\033[1;33mNOTES:\033[0m    new open save\n"
            "\033[1;36mSHELL:\033[0m    help history [-c] clear/cls which tcc theme anim perf\n"
            "          calc EXPR | calculator\n"
            "\033[90m------------------------------------------------------------------------\033[0m\n"
            "\033[1;37mKeys:\033[0m     Up/Down: history | Left/Right: edit | Shift+arrows: select/scroll\n"
            "          Ctrl+A: select all | Ctrl+C: copy | Ctrl+V: paste | Ctrl+L: clear\n"
            "          PgUp/PgDn scroll | Ctrl+Home/End top/bottom | mouse drag selects text");
    } else if (eq(command, "calc")) {
        char result[96];
        int ok=calc_evaluate(arg,result,sizeof result);
        copy(output,ok?result:"Error: ");
        if(!ok) append_str(output,result,sizeof output);
        serial("[CALC] "); serial(output); serial("\n");
    } else if (eq(command, "calculator")) {
        app_host_open(APP_CALCULATOR); copy(output,"Calculator opened.");
    } else if (eq(command, "about"))
        copy(output,
             OS_LABEL "\nOwn kernel, PollikFS, RTL8139 + ARP/ICMP.\nTwo ring3 workers "
                      "with private 64 KiB\nsegments, timer preemption and syscalls.\nDesktop apps "
                      "still run in the kernel.");
    else if (eq(command, "version")) {
        copy(output, OS_LABEL "\nGraphical desktop: i386\nNative TinyCC shell: x86_64 console");
    }
    else if (eq(command, "gfx")) framebuffer_info(output);
    else if (eq(command, "save"))
        copy(output, notes_save() ? "Saved to PollikFS on data disk." : "Save failed. Check data disk.");
    else if (eq(command, "new")) {
        char path[VFS_MAX_PATH];
        vfs_stat_t st;
        path_resolve(arg, path);
        if (!valid_name(arg)) copy(output, "Use a filename of 1-23 letters/digits/._-");
        else if (vfs_stat(path, &st) == 0) copy(output, "File already exists.");
        else if (notes_changed() && !notes_save()) copy(output, "Current note could not be saved.");
        else {
            int fd = vfs_open(path, O_WRONLY | O_CREAT | O_EXCL);
            int created = fd >= 0;
            if (fd >= 0 && vfs_close(fd) < 0) {
                vfs_unlink(path);
                created = 0;
            }
            if (created && notes_open_path(path)) copy(output, "Created file on disk.");
            else {
                if (created) vfs_unlink(path);
                copy(output, "Could not create file.");
            }
        }
    } else if (eq(command, "open")) {
        char path[VFS_MAX_PATH];
        vfs_stat_t st;
        path_resolve(arg, path);
        if (*arg && vfs_stat(path, &st) == 0 && st.type == VFS_FILE) {
            copy(output, notes_open_path(path) ? "Opened document." : "Could not open document.");
        } else {
            int i = fs_find(arg);
            if (i < 0) copy(output, "File not found.");
            else { notes_open(i); copy(output, "Opened document."); }
        }
    } else if (eq(command, "rm")) {
        char path[VFS_MAX_PATH]; path_resolve(arg,path);
        copy(output, *arg && vfs_unlink(path)==0 ? "File removed." : "rm: failed");
    } else if (eq(command, "mv")) {
        char *src_arg, *dst_arg, src[VFS_MAX_PATH], dst[VFS_MAX_PATH];
        if (!split_pair(arg, &src_arg, &dst_arg)) copy(output, "Usage: mv source destination (destination must not exist)");
        else {
            path_resolve(src_arg, src); path_resolve(dst_arg, dst);
            copy(output, vfs_rename(src, dst) == 0 ? "Moved." : "mv: failed (check paths; destination is never overwritten)");
        }
    } else if (eq(command, "cp")) {
        char *src_arg, *dst_arg, src[VFS_MAX_PATH], dst[VFS_MAX_PATH], buffer[512];
        if (!split_pair(arg, &src_arg, &dst_arg)) copy(output, "Usage: cp source destination (destination must not exist)");
        else {
            path_resolve(src_arg, src); path_resolve(dst_arg, dst);
            vfs_stat_t st;
            int in = vfs_stat(src, &st) == 0 && st.type == VFS_FILE ? vfs_open(src, O_RDONLY) : -1;
            int out = in >= 0 && !eq(src, dst) ? vfs_open(dst, O_WRONLY | O_CREAT | O_EXCL) : -1;
            int ok = in >= 0 && out >= 0;
            while (ok) {
                int got = vfs_read(in, buffer, sizeof(buffer));
                if (got < 0) { ok = 0; break; }
                if (!got) break;
                if (vfs_write(out, buffer, (u32)got) != got) { ok = 0; break; }
            }
            if (in >= 0) vfs_close(in);
            if (out >= 0) vfs_close(out);
            if (!ok && out >= 0) vfs_unlink(dst);
            copy(output, ok ? "Copied." : "cp: failed (source must be a file; destination must be new)");
        }
    } else if (eq(command, "pwd")) copy(output, cwd);
    else if (eq(command, "cd")) {
        char path[VFS_MAX_PATH]; vfs_stat_t st;
        path_resolve(arg, path);
        if (vfs_stat(path, &st) < 0) copy(output, "cd: path not found");
        else if (st.type != VFS_DIR) copy(output, "cd: not a directory");
        else { copy(cwd, path); output[0] = 0; }
    } else if (eq(command, "ls")) {
        char path[VFS_MAX_PATH]; path_resolve(arg, path);
        int fd = vfs_open(path, O_RDONLY), pos = 0;
        output[0] = 0;
        if (fd < 0) copy(output, "ls: cannot open path");
        else {
            vfs_dirent_t item;
            while (vfs_readdir(fd, &item) > 0 && pos < (int)sizeof(output)-3) {
                int j=0; while (item.name[j] && pos < (int)sizeof(output)-3) output[pos++]=item.name[j++];
                if (item.type == VFS_DIR) output[pos++]='/';
                output[pos++]='\n'; output[pos]=0;
            }
            vfs_close(fd);
        }
    } else if (eq(command, "stat")) {
        char path[VFS_MAX_PATH], n[12]; vfs_stat_t st;
        path_resolve(arg, path);
        if (!*arg || vfs_stat(path, &st) < 0) copy(output, "stat: path not found");
        else {
            copy(output, st.type == VFS_DIR ? "Type: directory\nSize: " : "Type: file\nSize: ");
            number(n, st.size); append_str(output, n, sizeof(output));
            append_str(output, " bytes\nInode: ", sizeof(output));
            number(n, st.inode); append_str(output, n, sizeof(output));
        }
    } else if (eq(command, "echo")) {
        copy(output, arg);
    } else if (eq(command, "which")) {
        if (eq(arg, "tcc") || eq(arg, "cc"))
            copy(output, "tcc: /bin/tcc (native x86_64 console only; use tcc --help)");
        else if (gui_builtin_exists(arg)) { copy(output, arg); append_str(output, ": built-in command", sizeof(output)); }
        else copy(output, "No such GUI command. External ELF64 programs run in the x86_64 console.");
    } else if (eq(command, "ps")) process_list(output);
    else if (eq(command, "pause") || eq(command, "resume") || eq(command, "kill") || eq(command, "spawn")) {
        int action = eq(command, "pause") ? 0 : eq(command, "resume") ? 1 : eq(command, "kill") ? 2 : 3;
        if (len(arg) != 1 || !process_action(*arg - '0', action)) copy(output, "Use PID 1 or 2.");
        else process_list(output);
    } else if (eq(command, "faulttest")) {
        process_fault_test();
        copy(output, "Worker 1 will attempt a privileged instruction.\nOnly that process should "
                     "stop. Inspect PS.");
    } else if (eq(command, "net")) net_info(output);
    else if (eq(command, "ping")) {
        net_ping();
        copy(output, "Testing gateway 10.0.2.2...\nRun NET for the result, or open Settings.");
    } else if (eq(command, "publicip")) {
        char ip[48];
        if (http_get_public_ip(ip, sizeof(ip))) {
            copy(output, "Public IP: ");
            int at = len(output), i = 0;
            while (ip[i] && at < 239) output[at++] = ip[i++];
            output[at] = 0;
        } else copy(output, "Could not fetch public IP over verified HTTPS.");
    } else if (eq(command, "cat")) {
        char path[VFS_MAX_PATH]; path_resolve(arg, path);
        int fd = vfs_open(path, O_RDONLY);
        if (fd < 0) copy(output, "cat: file not found");
        else { int got=vfs_read(fd, output, sizeof(output)-1); vfs_close(fd); if (got<0) copy(output,"cat: read failed"); else output[got]=0; }
    } else if (eq(command, "mkdir")) {
        char path[VFS_MAX_PATH]; path_resolve(arg,path);
        copy(output, *arg && vfs_mkdir(path)==0 ? "Directory created." : "mkdir: failed");
    } else if (eq(command, "touch")) {
        char path[VFS_MAX_PATH]; path_resolve(arg,path);
        int fd = *arg ? vfs_open(path,O_WRONLY|O_CREAT) : -1;
        if (fd>=0) vfs_close(fd);
        copy(output, fd>=0 ? "File ready." : "touch: failed");
    } else if (eq(command, "rmdir")) {
        char path[VFS_MAX_PATH]; path_resolve(arg,path);
        copy(output, *arg && vfs_rmdir(path)==0 ? "Directory removed." : "rmdir: failed");
    } else if (eq(command, "history")) {
        output[0]=0;
        if (eq(arg, "-c")) {
            history_count = history_pos = 0;
            copy(output, "Command history cleared.");
        } else for (int i=0;i<history_count;i++) {
                char n[12]; number(n, (u32)(i + 1));
                append_str(output, n, sizeof(output)); append_str(output, "  ", sizeof(output));
                append_str(output, history[i], sizeof(output)); append_str(output, "\n", sizeof(output));
            }
    } else if (eq(command, "theme")) {
        app_host_set_theme(!app_host_theme());
        copy(output, "Wallpaper changed.");
    } else if (eq(command, "anim") || eq(command, "animations")) {
        if (eq(arg, "off") || eq(arg, "0")) {
            app_host_set_animations(0);
            app_host_stop_minimize();
            copy(output, "Animations disabled (Fast mode).");
        } else if (eq(arg, "on") || eq(arg, "1")) {
            app_host_set_animations(1);
            copy(output, "Animations enabled (Smooth mode).");
        } else {
            app_host_set_animations(!app_host_animations());
            copy(output, app_host_animations() ? "Animations enabled (Smooth mode)." : "Animations disabled (Fast mode).");
        }
        app_host_invalidate(-1);
    } else if (eq(command, "perf")) app_host_perf_summary(output, (int)sizeof(output));
    else if (eq(command, "clear")) { transcript[0] = 0; output[0] = 0; }
    else if (eq(command, "tasks") || eq(command, "ps")) process_list(output);
    else if (eq(command, "uptime")) {
        char n[12];
        u32 sec = (ticks * 10) / 119;
        u32 mins = sec / 60;
        sec %= 60;
        copy(output, "Uptime: ");
        number(n, mins);
        append_str(output, n, 240);
        append_str(output, "m ", 240);
        number(n, sec);
        append_str(output, n, 240);
        append_str(output, "s (ticks: ", 240);
        number(n, ticks);
        append_str(output, n, 240);
        append_str(output, ")", 240);
    } else if (eq(command, "free") || eq(command, "mem")) {
        char n[12];
        u32 free_pages = pmm_get_free_pages_count();
        u32 total_pages = pmm_get_total_pages_count();
        u32 used_pages = total_pages > free_pages ? (total_pages - free_pages) : 0;
        copy(output, "Memory Summary:\n Total: ");
        number(n, (total_pages * 4));
        append_str(output, n, 240);
        append_str(output, " KiB\n Used:  ", 240);
        number(n, (used_pages * 4));
        append_str(output, n, 240);
        append_str(output, " KiB\n Free:  ", 240);
        number(n, (free_pages * 4));
        append_str(output, n, 240);
        append_str(output, " KiB (", 240);
        number(n, free_pages);
        append_str(output, n, 240);
        append_str(output, " pages)", 240);
    } else if (eq(command, "lspci")) {
        PciDevice devs[MAX_PCI_DEVICES];
        int num_devs = pci_scan_bus(devs, MAX_PCI_DEVICES);
        copy(output, "PCI Devices Detected:\n");
        for (int d = 0; d < num_devs && d < 6; d++) {
            char nb[12];
            number(nb, devs[d].dev);
            append_str(output, "00:", 240);
            append_str(output, nb, 240);
            append_str(output, " ", 240);
            append_str(output, pci_class_name(devs[d].class_code, devs[d].subclass), 240);
            append_str(output, "\n", 240);
        }
    } else if (eq(command, "beep")) {
        speaker_beep(880, 100);
        copy(output, "PC Speaker beeped at 880 Hz.");
    } else if (eq(command, "sound") || eq(command, "audio")) {
        if (!*arg || eq(arg, "status")) {
            if (audio_is_available()) {
                copy(output, "Audio: AC'97 hardware detected (Intel ICH)\nStatus: Active (DMA 48 kHz PCM)\nVolume: ");
                char vb[12];
                number(vb, audio_get_volume());
                append_str(output, vb, 240);
                append_str(output, "%\nUse: sound [test|startup|alert|click|trash]", 240);
            } else {
                copy(output, "Audio: AC'97 not found (PC Speaker active)\nStatus: Ready\nUse: sound [test|startup|alert|click|trash]");
            }
        } else if (eq(arg, "startup") || eq(arg, "play startup")) {
            audio_play_sound(SOUND_STARTUP);
            copy(output, "Playing startup chord...");
        } else if (eq(arg, "alert") || eq(arg, "play alert")) {
            audio_play_sound(SOUND_ALERT);
            copy(output, "Playing alert chime...");
        } else if (eq(arg, "click") || eq(arg, "play click")) {
            audio_play_sound(SOUND_CLICK);
            copy(output, "Playing click sound...");
        } else if (eq(arg, "trash") || eq(arg, "play trash")) {
            audio_play_sound(SOUND_TRASH);
            copy(output, "Playing trash swoop...");
        } else if (eq(arg, "test")) {
            audio_play_tone(440, 100);
            copy(output, "Playing 440 Hz test tone...");
        } else if (arg[0] == '/' || (arg[0] == 'p' && arg[1] == 'l' && arg[2] == 'a' && arg[3] == 'y' && arg[4] == ' ')) {
            const char *file_path = arg;
            if (arg[0] == 'p') file_path = arg + 5;
            while (*file_path == ' ') file_path++;
            if (audio_play_wav_file(file_path)) {
                copy(output, "Playing audio file: ");
                append_str(output, file_path, 240);
            } else {
                copy(output, "Failed to play WAV file (file not found or unsupported format): ");
                append_str(output, file_path, 240);
            }
        } else if (arg[0] == 'v' && arg[1] == 'o' && arg[2] == 'l') {
            const char *vp = arg + 3;
            while (*vp == ' ' || *vp == '=') vp++;
            int v = 0;
            while (*vp >= '0' && *vp <= '9') { v = v * 10 + (*vp - '0'); vp++; }
            if (v > 100) v = 100;
            audio_set_volume((u8)v);
            copy(output, "Volume set to ");
            char nbuf[12];
            number(nbuf, v);
            append_str(output, nbuf, 240);
            append_str(output, "%", 240);
        } else {
            copy(output, "Usage: sound [status | test | startup | alert | click | trash | vol <0-100> | play <path.wav>]");
        }
    } else if (eq(command, "shutdown")) {
        if (notes_changed() && !notes_save()) copy(output, "Shutdown cancelled: note could not be saved.");
        else app_host_power(0);
    } else if (eq(command, "pollikfetch") || eq(command, "fetch") || eq(command, "neofetch")) {
        char line[128];
        char n[16];

        copy(output, "\033[1;32mpollik\033[0m@\033[1;36muser\033[0m\n");
        append_str(output, "\033[90m----------------------------------------\033[0m\n", sizeof(output));
        append_str(output, "\033[1;35mOS:\033[0m          PollikOS v0.0.001 x86-32\n", sizeof(output));
        append_str(output, "\033[1;34mHost:\033[0m        Pollik Virtual Machine (QEMU/Bare-metal)\n", sizeof(output));
        append_str(output, "\033[1;36mKernel:\033[0m      Pollik-Core Monolithic (i386)\n", sizeof(output));

        u32 sec = (ticks * 10) / 119;
        u32 mins = sec / 60;
        sec %= 60;
        copy(line, "\033[1;32mUptime:\033[0m      ");
        number(n, mins);
        append_str(line, n, sizeof(line));
        append_str(line, "m ", sizeof(line));
        number(n, sec);
        append_str(line, n, sizeof(line));
        append_str(line, "s\n", sizeof(line));
        append_str(output, line, sizeof(output));

        extern int framebuffer_width(void), framebuffer_height(void);
        int scr_w = framebuffer_width(), scr_h = framebuffer_height();
        if (scr_w <= 0) scr_w = 1024;
        if (scr_h <= 0) scr_h = 768;
        copy(line, "\033[1;33mResolution:\033[0m  ");
        number(n, scr_w);
        append_str(line, n, sizeof(line));
        append_str(line, "x", sizeof(line));
        number(n, scr_h);
        append_str(line, n, sizeof(line));
        append_str(line, "\n", sizeof(line));
        append_str(output, line, sizeof(output));

        char brand[49];
        memset(brand, 0, sizeof(brand));
        int has_brand = 0;
        if (hal_cpu_has_cpuid()) {
            u32 max_ext = 0, b, c, d;
            hal_cpuid(0x80000000, 0, &max_ext, &b, &c, &d);
            if (max_ext >= 0x80000004) {
                u32 *p = (u32 *)brand;
                for (u32 leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
                    hal_cpuid(leaf, 0, &p[0], &p[1], &p[2], &p[3]);
                    p += 4;
                }
                brand[48] = 0;
                has_brand = 1;
            } else {
                u32 eax_v, ebx_v, ecx_v, edx_v;
                hal_cpuid(0, 0, &eax_v, &ebx_v, &ecx_v, &edx_v);
                ((u32 *)brand)[0] = ebx_v;
                ((u32 *)brand)[1] = edx_v;
                ((u32 *)brand)[2] = ecx_v;
                brand[12] = 0;
                has_brand = 1;
            }
        }
        const char *bp = brand;
        while (*bp == ' ') bp++;
        copy(line, "\033[1;31mCPU:\033[0m         ");
        if (has_brand && *bp) append_str(line, bp, sizeof(line));
        else append_str(line, "x86 Compatible Processor", sizeof(line));
        append_str(line, "\n", sizeof(line));
        append_str(output, line, sizeof(output));

        u32 free_pages = pmm_get_free_pages_count();
        u32 total_pages = pmm_get_total_pages_count();
        u32 used_pages = total_pages > free_pages ? (total_pages - free_pages) : 0;
        u32 used_mb = (used_pages * 4) / 1024;
        u32 total_mb = (total_pages * 4) / 1024;
        copy(line, "\033[1;35mMemory:\033[0m      ");
        number(n, used_mb);
        append_str(line, n, sizeof(line));
        append_str(line, " MiB / ", sizeof(line));
        number(n, total_mb);
        append_str(line, n, sizeof(line));
        append_str(line, " MiB\n", sizeof(line));
        append_str(output, line, sizeof(output));

        copy(line, "\033[1;34mProcesses:\033[0m   ");
        number(n, process_get_count());
        append_str(line, n, sizeof(line));
        append_str(line, " active\n", sizeof(line));
        append_str(output, line, sizeof(output));

        u32 free_blocks = pollikfs_free_blocks();
        u32 total_blocks = POLLIK2_TOTAL_BLOCKS;
        u32 used_blocks = total_blocks > free_blocks ? total_blocks - free_blocks : 0;
        copy(line, "\033[1;36mDisk:\033[0m        ");
        number(n, used_blocks);
        append_str(line, n, sizeof(line));
        append_str(line, " / ", sizeof(line));
        number(n, total_blocks);
        append_str(line, n, sizeof(line));
        append_str(line, " blocks (PollikFS v2)\n", sizeof(line));
        append_str(output, line, sizeof(output));

        NetworkInterface *iface = net_manager_get_ethernet_iface();
        copy(line, "\033[1;32mNetwork:\033[0m     RTL8139 (");
        if (iface && iface->link_up) {
            append_str(line, "Link UP, IP: ", sizeof(line));
            number(n, iface->ip[0]);
            append_str(line, n, sizeof(line));
            append_str(line, ".", sizeof(line));
            number(n, iface->ip[1]);
            append_str(line, n, sizeof(line));
            append_str(line, ".", sizeof(line));
            number(n, iface->ip[2]);
            append_str(line, n, sizeof(line));
            append_str(line, ".", sizeof(line));
            number(n, iface->ip[3]);
            append_str(line, n, sizeof(line));
            append_str(line, ")\n", sizeof(line));
        } else {
            append_str(line, "Disconnected)\n", sizeof(line));
        }
        append_str(output, line, sizeof(output));

        append_str(output, "\033[1;33mShell:\033[0m       pollik-term-gui v0.1\n", sizeof(output));

        char t_buf[16];
        rtc_format_time(t_buf, sizeof(t_buf));
        copy(line, "\033[1;35mTime:\033[0m        ");
        append_str(line, t_buf, sizeof(line));
        append_str(line, "\n", sizeof(line));
        append_str(output, line, sizeof(output));

        append_str(output, "\033[90m----------------------------------------\033[0m\n", sizeof(output));
        append_str(output,
            "\033[30m[#] \033[31m[#] \033[32m[#] \033[33m[#] \033[34m[#] \033[35m[#] \033[36m[#] \033[37m[#]\033[0m\n"
            "\033[90m[#] \033[91m[#] \033[92m[#] \033[93m[#] \033[94m[#] \033[95m[#] \033[96m[#] \033[97m[#]\033[0m", sizeof(output));
    } else if (eq(command, "time")) {
        char t_buf[16]; rtc_format_time(t_buf, sizeof(t_buf));
        copy(output, "Time: "); append_str(output, t_buf, sizeof(output));
    } else if (eq(command, "date")) {
        RtcTime now; char year[12], day[3], month[3];
        rtc_get_time(&now); number(year, now.year); number(day, now.day); number(month, now.month);
        copy(output, "Date: "); append_str(output, year, sizeof(output)); append_str(output, "-", sizeof(output));
        if (now.month < 10) append_str(output, "0", sizeof(output));
        append_str(output, month, sizeof(output)); append_str(output, "-", sizeof(output));
        if (now.day < 10) append_str(output, "0", sizeof(output));
        append_str(output, day, sizeof(output));
    } else if (eq(command, "reboot")) {
        if (notes_changed() && !notes_save()) copy(output, "Reboot cancelled: note could not be saved.");
        else app_host_power(1);
    } else if (!command[0]) output[0] = 0;
    else if (eq(command, "tcc") || eq(command, "cc")) {
        if (!*arg || eq(arg, "--help") || eq(arg, "-h")) terminal_tcc_help(output);
        else if (eq(arg, "--version") || eq(arg, "-v"))
            copy(output, "TinyCC 0.9.27 for PollikOS x86_64; this graphical i386 session cannot execute ELF64 programs.");
        else copy(output, "This GUI shell cannot launch TinyCC. Boot the x86_64 console to compile; type tcc --help for examples.");
    }
    else copy(output, "Command not found in the graphical shell. Type help; external programs run in the x86_64 console.");
    serial("SHELL command executed\n");
    int serial_len = len(output);
    if (serial_len > 512) {
        char saved = output[512]; output[512] = 0;
        serial(output); output[512] = saved;
        serial("\n... output truncated in serial log (full output is in terminal) ...\n");
    } else serial(output);
    serial("\n");
    serial("SHELL END\n");
    if (!eq(command, "clear")) {
        append_transcript("pollik:"); append_transcript(cwd); append_transcript("$ ");
        append_transcript(entered); append_transcript("\n");
        if (output[0]) { append_transcript(output); append_transcript("\n"); }
    }
    scroll_lines = 0;
    cmdlen = cmdcursor = 0;
    command[0] = 0;
    terminal_clear_selection();
    app_host_invalidate(APP_TERMINAL);
}
static void terminal_insert_char(char ch) {
    if (!ch || cmdlen >= TERM_COMMAND_LENGTH - 1) return;
    memmove(command + cmdcursor + 1, command + cmdcursor, (unsigned)(cmdlen - cmdcursor + 1));
    command[cmdcursor++] = ch;
    cmdlen++;
}

static void terminal_command_selection(int shift, int next_cursor) {
    int command_at;
    terminal_build_buffer(0, &command_at);
    if (shift) {
        if (selection_anchor < command_at || selection_anchor > command_at + cmdlen)
            selection_anchor = command_at + cmdcursor;
        cmdcursor = next_cursor;
        selection_focus = command_at + cmdcursor;
        selection_drag = 0;
    } else {
        terminal_clear_selection();
        cmdcursor = next_cursor;
    }
}

static int terminal_copy_selection(void) {
    if (selection_anchor < 0 || selection_focus < 0 || selection_anchor == selection_focus) return 0;
    int used = terminal_build_buffer(0, 0);
    int from = selection_anchor, to = selection_focus;
    if (from > to) { int tmp = from; from = to; to = tmp; }
    if (from >= used) return 0;
    if (to > used) to = used;
    app_clipboard_copy(render_buffer + from, to - from);
    return 1;
}

void terminal_key(u8 code, char ch, int shift, int control) {
    if(shift && !control) {
        const char *from="1234567890-=", *to="!@#$%^&*()_+";
        for(int i=0;from[i];i++) if(ch==from[i]) {ch=to[i];break;}
    }

    if (code == 28) { execute(); return; }
    if (control && code == 38) {
        transcript[0] = 0; scroll_lines = 0; terminal_clear_selection();
        return;
    }
    if (control && code == 30) {
        int used = terminal_build_buffer(0, 0);
        selection_anchor = 0; selection_focus = used; selection_drag = 0;
        return;
    }
    if (control && code == 46) {
        if (!terminal_copy_selection()) {
            cmdlen = cmdcursor = 0; command[0] = 0;
            terminal_clear_selection();
        }
        return;
    }
    if (control && code == 47) {
        char pasted[1024];
        int count = app_clipboard_paste(pasted, sizeof(pasted));
        terminal_clear_selection();
        for (int i = 0; i < count; i++) {
            char c = pasted[i];
            if (c == '\r') continue;
            if (c == '\n' || c == '\t') c = ' ';
            terminal_insert_char(c);
        }
        scroll_lines = 0; return;
    }
    if (control && code == 71) { terminal_scroll(-TERM_SCROLLBACK / 4); return; }
    if (control && code == 79) { terminal_scroll(TERM_SCROLLBACK / 4); return; }
    if (code == 73) {
        if (shift) terminal_scroll(-(TERM_SCROLLBACK / 4));
        else terminal_scroll(-8);
        return;
    }
    if (code == 81) {
        if (shift) terminal_scroll(TERM_SCROLLBACK / 4);
        else terminal_scroll(8);
        return;
    }
    if (shift && code == 72) { terminal_scroll(-3); return; }
    if (shift && code == 80) { terminal_scroll(3); return; }
    if (code == 72) { terminal_clear_selection(); if (history_pos > 0) history_show(history_pos - 1); return; }
    if (code == 80) { terminal_clear_selection(); if (history_pos < history_count) history_show(history_pos + 1); return; }
    if (code == 75) { terminal_command_selection(shift, cmdcursor > 0 ? cmdcursor - 1 : 0); return; }
    if (code == 77) { terminal_command_selection(shift, cmdcursor < cmdlen ? cmdcursor + 1 : cmdlen); return; }
    if (code == 71) {
        if (shift) terminal_command_selection(1, 0);
        else { terminal_clear_selection(); cmdcursor = 0; }
        return;
    }
    if (code == 79) {
        if (shift) terminal_command_selection(1, cmdlen);
        else { terminal_clear_selection(); cmdcursor = cmdlen; }
        return;
    }
    if (code == 83) {
        terminal_clear_selection();
        if (cmdcursor < cmdlen) { memmove(command + cmdcursor, command + cmdcursor + 1, (unsigned)(cmdlen - cmdcursor)); cmdlen--; }
        return;
    }
    if (code == 14) {
        terminal_clear_selection();
        if (cmdcursor > 0) { memmove(command + cmdcursor - 1, command + cmdcursor, (unsigned)(cmdlen - cmdcursor + 1)); cmdcursor--; cmdlen--; }
        return;
    }
    if (ch && cmdlen < TERM_COMMAND_LENGTH - 1) {
        terminal_clear_selection();
        terminal_insert_char(ch);
        scroll_lines = 0;
    }
}
void terminal_scroll(int delta) {
    if (terminal_scroll_by(delta)) app_host_invalidate_partial(APP_TERMINAL);
}
static int terminal_scroll_by(int delta) {
    int old = scroll_lines;
    int limit = TERM_SCROLLBACK / 4;
    if (delta > 0) scroll_lines = delta >= scroll_lines ? 0 : scroll_lines - delta;
    else if (delta < 0) {
        int add = delta <= -limit ? limit : -delta;
        scroll_lines = scroll_lines >= limit - add ? limit : scroll_lines + add;
    }
    return old != scroll_lines;
}
