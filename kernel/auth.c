#include "auth.h"
#include "graphics.h"
#include "ui.h"
#include "pollikfs.h"
#include "vfs.h"
#include "gui/apps.h"
#include "audio.h"
#include "include/bearssl/bearssl.h"
#ifdef POLLIK_INSTALL_MEDIA
#include "installer.h"
#include "hw.h"
#endif

#define ACCOUNT_MAGIC 0x31524341u /* ACR1 */
#define ACCOUNT_VERSION 1u
#define ACCOUNT_KDF_ROUNDS 8192u
#define ACCOUNT_PATH "/etc/account.db"
#define INSTALL_MARKER "/etc/pollikos-installed"

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 rounds;
    char username[32];
    u8 salt[16];
    u8 password_hash[32];
    u8 reserved[36];
} AccountRecord;

_Static_assert(sizeof(AccountRecord) == 128, "account record wire size");

enum {
    AUTH_SETUP_INTRO,
    AUTH_SETUP_NAME,
    AUTH_SETUP_PASSWORD,
    AUTH_SETUP_CONFIRM,
    AUTH_LOGIN,
    AUTH_FORMAT_WARNING,
    AUTH_FORMAT_CONFIRM,
#ifdef POLLIK_INSTALL_MEDIA
    AUTH_INSTALL_COMPLETE,
#endif
    AUTH_UNLOCKED
};

static AccountRecord account;
static int state = AUTH_UNLOCKED;
static char input[64];
static int input_len, input_cursor, input_anchor = -1;
static char new_password[64];
static char message[96];
static u32 entropy = 0x706f6c6cu;
static int pointer_was_down;
static int button_x, button_y, button_w, button_h;
static int eye_x, eye_y, eye_w, eye_h, secret_field_active, show_password;
static int field_x, field_y, field_w, field_active, input_dragging;

static void copy_text(char *dst, const char *src, u32 cap) {
    u32 i = 0;
    if (!cap) return;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

static void clear_input(void) {
    memset(input, 0, sizeof(input));
    input_len = input_cursor = 0;
    input_anchor = -1;
    input_dragging = 0;
    show_password = 0;
}

static int constant_equal(const u8 *a, const u8 *b, u32 length) {
    u8 difference = 0;
    for (u32 i = 0; i < length; ++i) difference |= a[i] ^ b[i];
    return difference == 0;
}

/* Salted, deliberately expensive SHA-256 password KDF. The on-disk round
 * count makes future upgrades possible without storing plaintext passwords. */
static void password_kdf(const char *password, const u8 salt[16], u32 rounds, u8 out[32]) {
    br_sha256_context ctx;
    u32 length = 0;
    while (password[length] && length < 63) ++length;
    br_sha256_init(&ctx);
    br_sha256_update(&ctx, salt, 16);
    br_sha256_update(&ctx, password, length);
    br_sha256_out(&ctx, out);
    for (u32 round = 1; round < rounds; ++round) {
        br_sha256_init(&ctx);
        br_sha256_update(&ctx, out, 32);
        br_sha256_update(&ctx, salt, 16);
        br_sha256_update(&ctx, password, length);
        br_sha256_out(&ctx, out);
    }
}

/* 1 valid, 0 absent, -1 present but invalid. */
static int load_account(void) {
    int fd = vfs_open(ACCOUNT_PATH, O_RDONLY);
    if (fd < 0) return 0;
    AccountRecord candidate;
    int got = vfs_read(fd, &candidate, sizeof(candidate));
    vfs_close(fd);
    if (got != (int)sizeof(candidate) || candidate.magic != ACCOUNT_MAGIC ||
        candidate.version != ACCOUNT_VERSION || candidate.rounds < 1024 ||
        candidate.rounds > 1000000 || !candidate.username[0] || candidate.username[31]) return -1;
    account = candidate;
    return 1;
}

static int write_all(const char *path, const void *data, u32 length) {
    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return 0;
    int written = vfs_write(fd, data, length);
    int closed = vfs_close(fd);
    return written == (int)length && closed == 0;
}

static void make_salt(u8 salt[16]) {
    br_sha256_context ctx;
    u8 digest[32];
    u8 rtc[8];
    u32 tick_sample = ticks;
    for (int i = 0; i < 8; ++i) {
        outb(0x70, (u8)i);
        rtc[i] = inb(0x71);
    }
    entropy ^= ticks + (entropy << 7) + (entropy >> 3);
    br_sha256_init(&ctx);
    br_sha256_update(&ctx, &entropy, sizeof(entropy));
    br_sha256_update(&ctx, &tick_sample, sizeof(tick_sample));
    br_sha256_update(&ctx, rtc, sizeof(rtc));
    br_sha256_update(&ctx, account.username, sizeof(account.username));
    br_sha256_out(&ctx, digest);
    memcpy(salt, digest, 16);
    memset(digest, 0, sizeof(digest));
}

static int valid_username(const char *name) {
    int length = 0;
    while (name[length]) {
        char c = name[length];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
        ++length;
    }
    return length >= 2 && length <= 31;
}

static int create_account(void) {
    memset(&account, 0, sizeof(account));
    account.magic = ACCOUNT_MAGIC;
    account.version = ACCOUNT_VERSION;
    account.rounds = ACCOUNT_KDF_ROUNDS;
    copy_text(account.username, input, sizeof(account.username));
    make_salt(account.salt);
    password_kdf(new_password, account.salt, account.rounds, account.password_hash);
    vfs_mkdir("/etc");
    vfs_mkdir("/home");
    char home[64] = "/home/";
    int at = 6;
    for (int i = 0; account.username[i] && at + 1 < (int)sizeof(home); ++i) home[at++] = account.username[i];
    home[at] = 0;
    vfs_mkdir(home);
    if (!write_all(ACCOUNT_PATH, &account, sizeof(account))) return 0;
    const char marker[] = "PollikOS installation complete\n";
    if (!write_all(INSTALL_MARKER, marker, sizeof(marker) - 1)) return 0;
    serial("AUTH: account created; installation complete\n");
    return 1;
}

void auth_init(void) {
    pointer_was_down = 0;
    clear_input();
    memset(new_password, 0, sizeof(new_password));
    message[0] = 0;
#ifdef POLLIK_INSTALL_MEDIA
    state = AUTH_FORMAT_WARNING;
    copy_text(message, installer_target_available() ?
        "Supported ATA installation disk detected" :
        "No supported primary ATA installation disk detected", sizeof(message));
    serial("INSTALL: removable-media installer ready\n");
#else
    if (!pollikfs_mounted()) {
        state = AUTH_FORMAT_WARNING;
        serial("SETUP: PollikFS unavailable; explicit format required\n");
    } else if (load_account() == 1) {
        state = AUTH_LOGIN;
        serial("AUTH: login required\n");
    } else if (vfs_stat(ACCOUNT_PATH, &(vfs_stat_t){0}) == 0) {
        state = AUTH_FORMAT_WARNING;
        copy_text(message, "Account database is invalid; recovery format required", sizeof(message));
        serial("AUTH: invalid account database; login denied\n");
    } else {
        state = AUTH_SETUP_INTRO;
        serial("SETUP: first-run installer ready\n");
    }
#endif
}

int auth_is_active(void) { return state != AUTH_UNLOCKED; }
const char *auth_username(void) { return account.username[0] ? account.username : "User"; }

void auth_lock(void) {
    if (!account.username[0] && load_account() != 1) return;
    clear_input();
    pointer_was_down = 0;
    message[0] = 0;
    state = AUTH_LOGIN;
    serial("AUTH: session locked\n");
}

static void submit(void) {
    message[0] = 0;
    if (state == AUTH_SETUP_INTRO) {
        state = AUTH_SETUP_NAME;
        clear_input();
    } else if (state == AUTH_SETUP_NAME) {
        if (!valid_username(input)) {
            copy_text(message, "Use 2-31 lowercase letters, numbers, _ or -", sizeof(message));
            return;
        }
        copy_text(account.username, input, sizeof(account.username));
        state = AUTH_SETUP_PASSWORD;
        clear_input();
    } else if (state == AUTH_SETUP_PASSWORD) {
        if (input_len < 6) {
            copy_text(message, "Password must contain at least 6 characters", sizeof(message));
            return;
        }
        copy_text(new_password, input, sizeof(new_password));
        state = AUTH_SETUP_CONFIRM;
        clear_input();
    } else if (state == AUTH_SETUP_CONFIRM) {
        if (memcmp(new_password, input, sizeof(new_password)) != 0) {
            copy_text(message, "Passwords do not match", sizeof(message));
            memset(new_password, 0, sizeof(new_password));
            state = AUTH_SETUP_PASSWORD;
            clear_input();
            return;
        }
        copy_text(input, account.username, sizeof(input));
        input_len = (int)strlen(input);
        if (!create_account()) {
            copy_text(message, "Could not save the account to PollikFS", sizeof(message));
            state = AUTH_SETUP_INTRO;
            clear_input();
            return;
        }
        memset(new_password, 0, sizeof(new_password));
        clear_input();
#ifdef POLLIK_INSTALL_MEDIA
        state = AUTH_INSTALL_COMPLETE;
        serial("INSTALL: complete; remove media and restart\n");
#else
        state = AUTH_UNLOCKED;
        audio_play_sound(SOUND_LOGIN);
#endif
    } else if (state == AUTH_LOGIN) {
        u8 candidate[32];
        password_kdf(input, account.salt, account.rounds, candidate);
        int accepted = constant_equal(candidate, account.password_hash, sizeof(candidate));
        memset(candidate, 0, sizeof(candidate));
        clear_input();
        if (accepted) {
            state = AUTH_UNLOCKED;
            serial("AUTH: login accepted\n");
            audio_play_sound(SOUND_LOGIN);
        } else {
            copy_text(message, "Incorrect password", sizeof(message));
            serial("AUTH: login rejected\n");
        }
    } else if (state == AUTH_FORMAT_WARNING) {
        state = AUTH_FORMAT_CONFIRM;
        copy_text(message, "Press Install again to erase the data disk", sizeof(message));
    } else if (state == AUTH_FORMAT_CONFIRM) {
#ifdef POLLIK_INSTALL_MEDIA
        if (!installer_write_system()) {
            state = AUTH_FORMAT_WARNING;
            copy_text(message, "Installation failed or no supported ATA disk", sizeof(message));
            return;
        }
#endif
        pollikfs_format();
        pollikfs_init();
        if (!pollikfs_mounted()) {
            state = AUTH_FORMAT_WARNING;
            copy_text(message, "Formatting failed; disk was not mounted", sizeof(message));
            return;
        }
#ifdef POLLIK_INSTALL_MEDIA
        if (!installer_install_wallpapers()) {
            state = AUTH_FORMAT_WARNING;
            copy_text(message, "Wallpaper installation failed", sizeof(message));
            return;
        }
#endif
        serial("SETUP: explicit PollikFS format complete\n");
        state = AUTH_SETUP_NAME;
        clear_input();
        message[0] = 0;
    }
#ifdef POLLIK_INSTALL_MEDIA
    else if (state == AUTH_INSTALL_COMPLETE) {
        power_shutdown();
    }
#endif
}

static int auth_has_selection(void) { return input_anchor >= 0 && input_anchor != input_cursor; }
static void auth_delete_selection(void) {
    if (!auth_has_selection()) { input_anchor = -1; return; }
    int a = input_anchor, b = input_cursor;
    if (a > b) { int t = a; a = b; b = t; }
    memmove(input + a, input + b, (u32)(input_len - b + 1));
    input_len -= b - a; input_cursor = a; input_anchor = -1;
}
static void auth_insert(const char *text, int count) {
    if (!text || count <= 0) return;
    auth_delete_selection();
    if (count > (int)sizeof(input) - 1 - input_len) count = (int)sizeof(input) - 1 - input_len;
    if (count <= 0) return;
    memmove(input + input_cursor + count, input + input_cursor, (u32)(input_len - input_cursor + 1));
    memcpy(input + input_cursor, text, (u32)count);
    input_cursor += count; input_len += count;
    message[0] = 0;
}
void auth_key_ex(u8 code, int shift, int control) {
    static const char keys[128] = {
        [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
        [12]='-',[13]='=',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',[21]='y',[22]='u',[23]='i',
        [24]='o',[25]='p',[26]='[',[27]=']',[30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',
        [36]='j',[37]='k',[38]='l',[39]=';',[40]='\'',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',
        [48]='b',[49]='n',[50]='m',[51]=',',[52]='.',[53]='/',[57]=' '
    };
    entropy ^= ((u32)code << 24) ^ ticks ^ (entropy << 5) ^ (entropy >> 2);
    if (code == 28) { submit(); return; }
    if (state == AUTH_SETUP_INTRO || state == AUTH_FORMAT_WARNING || state == AUTH_FORMAT_CONFIRM
#ifdef POLLIK_INSTALL_MEDIA
        || state == AUTH_INSTALL_COMPLETE
#endif
       ) return;
    if (control && code == 30) {
        input_anchor = 0; input_cursor = input_len; message[0] = 0; return;
    }
    if (control && (code == 46 || code == 45)) {
        if (auth_has_selection()) {
            int a = input_anchor, b = input_cursor;
            if (a > b) { int t = a; a = b; b = t; }
            app_clipboard_copy(input + a, b - a);
            if (code == 45) auth_delete_selection();
        }
        return;
    }
    if (control && code == 47) {
        char pasted[1024]; int n = app_clipboard_paste(pasted, sizeof(pasted));
        int kept = 0;
        for (int i = 0; i < n; i++) if (pasted[i] != '\r' && pasted[i] != '\n') pasted[kept++] = pasted[i];
        auth_insert(pasted, kept); return;
    }
    if (code == 75 || code == 77 || code == 71 || code == 79) {
        int next = code == 75 ? input_cursor - 1 : code == 77 ? input_cursor + 1 : code == 71 ? 0 : input_len;
        if (next < 0) next = 0; if (next > input_len) next = input_len;
        if (shift) { if (input_anchor < 0) input_anchor = input_cursor; }
        else input_anchor = -1;
        input_cursor = next;
        return;
    }
    if (code == 14 || code == 83) {
        if (auth_has_selection()) auth_delete_selection();
        else if (code == 14 && input_cursor > 0) {
            memmove(input + input_cursor - 1, input + input_cursor, (u32)(input_len - input_cursor + 1));
            input_cursor--; input_len--;
        } else if (code == 83 && input_cursor < input_len) {
            memmove(input + input_cursor, input + input_cursor + 1, (u32)(input_len - input_cursor));
            input_len--;
        }
        message[0] = 0;
        return;
    }
    char ch = code < 128 ? keys[code] : 0;
    if (!ch || (input_len >= (int)sizeof(input) - 1 && !auth_has_selection())) return;
    if (shift && ch >= 'a' && ch <= 'z') ch -= 32;
    if (state == AUTH_SETUP_NAME && ch >= 'A' && ch <= 'Z') ch += 32;
    auth_insert(&ch, 1);
}
void auth_key(u8 code, int shift) { auth_key_ex(code, shift, 0); }

static void draw_password_toggle(u32 color, int glass) {
    if (glass) rounded(eye_x, eye_y, eye_w, eye_h, 13, 0xffffff, show_password ? 72 : 24);
    else rounded(eye_x, eye_y, eye_w, eye_h, 8, ui_theme()->accent, show_password ? 48 : 16);
    /* Supersampled almond outline, pupil and crossed-out hidden state. */
    for (int y = 0; y < 18; y++) {
        for (int x = 0; x < 24; x++) {
            int coverage = 0;
            for (int sy = 1; sy < 8; sy += 2) {
                for (int sx = 1; sx < 8; sx += 2) {
                    int dx = x * 8 + sx - 96, dy = y * 8 + sy - 72;
                    int top = dx * dx + (dy + 72) * (dy + 72);
                    int bottom = dx * dx + (dy - 72) * (dy - 72);
                    int ink = (top <= 112 * 112 && bottom <= 112 * 112 &&
                               (top >= 100 * 100 || bottom >= 100 * 100)) ||
                              dx * dx + dy * dy <= 18 * 18;
                    if (!show_password) {
                        int diagonal = dx - dy;
                        if (diagonal > -16 && diagonal < 16) ink = 0;
                        if (diagonal >= -7 && diagonal <= 7 && dx >= -56 && dx <= 56) ink = 1;
                    }
                    coverage += ink;
                }
            }
            if (coverage) rounded(eye_x + 1 + x, eye_y + 4 + y, 1, 1, 0, color, coverage * 16);
        }
    }
}

static void draw_field(int x, int y, int w, int secret) {
    ThemeColors *theme = ui_theme();
    int glass = state == AUTH_LOGIN;
    u32 bg = theme->surface_elevated;
    u32 fg = glass ? 0xffffff : theme->text;
    u32 placeholder = glass ? 0xe2ddeb : theme->text_muted;
    u32 accent = glass ? 0xd4bfff : theme->accent;
    /* Continuous 2px accent stroke: paint the ring colour as the base, then
     * repaint the interior. This keeps every corner pixel solid, unlike a thin
     * coverage-based outline that fades on the arc. */
    if (glass) {
        rounded(x, y + 3, w, 42, 21, 0x090612, 48);
        rounded(x, y, w, 42, 21, 0xffffff, 76);
        rounded(x + 1, y + 1, w - 2, 40, 20, 0x20152f, 88);
    } else {
        roundrect(x, y, w, 42, UI_RADIUS_MEDIUM, accent);
        roundrect(x + 2, y + 2, w - 4, 42 - 4, UI_RADIUS_MEDIUM - 2, bg);
    }
    secret_field_active = secret;
    field_x = x; field_y = y; field_w = w; field_active = 1;
    eye_x = x + w - 32; eye_y = y + 8; eye_w = 26; eye_h = 26;
    int available = w - (secret ? 60 : 28), start = 0, end = 0;
    if (available < 16) available = 16;
    while (start < input_cursor && text_width(input + start, 1) > available) start++;
    end = start;
    while (end < input_len) {
        char temp[64]; int n = end - start + 1;
        memcpy(temp, input + start, (u32)n); temp[n] = 0;
        if (text_width(temp, 1) > available) break;
        end++;
    }
    char shown[64];
    int shown_len = end - start;
    if (shown_len > (int)sizeof(shown) - 1) shown_len = (int)sizeof(shown) - 1;
    for (int i = 0; i < shown_len; i++) shown[i] = secret && !show_password ? '*' : input[start + i];
    shown[shown_len] = 0;
    int sel_lo = input_anchor, sel_hi = input_cursor;
    if (sel_lo > sel_hi) { int t = sel_lo; sel_lo = sel_hi; sel_hi = t; }
    if (input_len) {
        if (input_anchor >= 0 && sel_hi > sel_lo && sel_hi > start && sel_lo < end) {
            int a = sel_lo > start ? sel_lo : start, b = sel_hi < end ? sel_hi : end;
            char before[64], selected[64]; int n = a - start;
            memcpy(before, shown, (u32)n); before[n] = 0;
            for (int i = 0; i < b - a; i++) selected[i] = secret && !show_password ? '*' : input[a + i];
            selected[b - a] = 0;
            roundrect(x + 14 + text_width(before, 1), y + 10, text_width(selected, 1), 22, 3, accent);
        }
        text(x + 14, y + 13, shown, fg, 1);
        if ((ticks / 500u) & 1u && input_cursor >= start && input_cursor <= end) {
            char before[64]; int n = input_cursor - start;
            memcpy(before, shown, (u32)n); before[n] = 0;
            int caret_x = x + 14 + text_width(before, 1) + 2;
            rect(caret_x, y + 12, 2, 18, accent);
        }
    } else {
        text(x + 14, y + 13, secret ? "Password" : "Username", placeholder, 1);
    }
    if (secret) {
        draw_password_toggle(show_password ? fg : placeholder, glass);
    }
}

static int auth_input_index_at(int x) {
    int available = field_w - (secret_field_active ? 60 : 28);
    if (available < 16) available = 16;
    int start = 0;
    while (start < input_cursor && text_width(input + start, 1) > available) start++;
    int end = start;
    for (;;) {
        if (end >= input_len) break;
        char temp[64]; int n = end - start + 1;
        memcpy(temp, input + start, (u32)n); temp[n] = 0;
        if (text_width(temp, 1) > available) break;
        end++;
    }
    int px = x - (field_x + 14);
    if (px <= 0) return start;
    int pos = 0;
    for (int i = start; i < end; i++) {
        char temp[2] = {secret_field_active && !show_password ? '*' : input[i], 0};
        int advance = text_width(temp, 1);
        if (px < pos + advance / 2) return i;
        pos += advance;
    }
    return end;
}

void auth_render(int width, int height) {
    if (!auth_is_active()) return;
    field_active = 0;
    int dark = ui_is_dark();
    graphics_set_clip((GraphicsClip){0, 0, width, height});

    /* The compositor restores the cached, blurred filesystem wallpaper. */
    if (state == AUTH_LOGIN) {
        int avatar_y = height * 40 / 100 - 116;
        if (avatar_y < 40) avatar_y = 40;
        int avatar_x = width / 2 - 44;
        rounded(avatar_x - 3, avatar_y + 3, 94, 94, 47, 0x100a20, 64);
        rounded(avatar_x, avatar_y, 88, 88, 44, 0xffffff, 108);
        rounded(avatar_x + 2, avatar_y + 2, 84, 84, 42, 0x655080, 80);
        roundrect(avatar_x + 30, avatar_y + 18, 28, 28, 14, 0xf0e9fa);
        roundrect(avatar_x + 20, avatar_y + 50, 48, 28, 14, 0xf0e9fa);
        int name_scale = text_width(auth_username(), 2) > width - 48 ? 1 : 2;
        centered(16, avatar_y + 106, width - 32, auth_username(), 0xffffff, name_scale);
        int fw = width < 360 ? width - 96 : 248;
        int fx = (width - fw - 52) / 2;
        int fy = avatar_y + 150;
        draw_field(fx, fy, fw, 1);
        button_x = fx + fw + 10; button_y = fy;
        button_w = button_h = 42;
        rounded(button_x, button_y, 42, 42, 21, 0xffffff, 96);
        /* Small arrow is the same submit action as Enter. */
        rect(button_x + 12, button_y + 20, 17, 2, 0xffffff);
        for (int i = 0; i < 7; i++) {
            rect(button_x + 23 + i, button_y + 14 + i, 2, 1, 0xffffff);
            rect(button_x + 23 + i, button_y + 27 - i, 2, 1, 0xffffff);
        }
        if (message[0]) centered(16, fy + 58, width - 32, message, 0xffcad8, 1);
        centered(0, height - 48, width, "Pollik OS", 0xe1d6f5, 1);
        return;
    }

    const char *title = "PollikOS Setup";
    const char *body = "Install PollikOS on the attached data disk";
    const char *button = "Continue";
    int show_field = 0, secret = 0;
    if (state == AUTH_SETUP_NAME) { title = "Create your account"; body = "Choose the name used to sign in"; show_field = 1; button = "Next"; }
    else if (state == AUTH_SETUP_PASSWORD) { title = "Protect your account"; body = "Choose a password with at least 6 characters"; show_field = 1; secret = 1; button = "Next"; }
    else if (state == AUTH_SETUP_CONFIRM) { title = "Confirm your password"; body = "Enter the same password again"; show_field = 1; secret = 1; button = "Install"; }
    else if (state == AUTH_LOGIN) { title = auth_username(); body = "Enter your password to continue"; show_field = 1; secret = 1; button = "Sign in"; }
    else if (state == AUTH_FORMAT_WARNING || state == AUTH_FORMAT_CONFIRM) {
#ifdef POLLIK_INSTALL_MEDIA
        title = "Install PollikOS";
        body = "The primary ATA disk will be erased and replaced";
#else
        title = "Data disk needs setup";
        body = "Formatting permanently removes every file on the data disk";
#endif
        button = state == AUTH_FORMAT_CONFIRM ? "Erase and install" : "Install PollikOS";
    }
#ifdef POLLIK_INSTALL_MEDIA
    else if (state == AUTH_INSTALL_COMPLETE) {
        title = "Installation complete";
        body = "Remove the USB media, then boot from the installed disk";
        button = "Shut down";
    }
#endif
    secret_field_active = secret;

    ThemeColors *theme = ui_theme();
    u32 text_col = theme->text;
    u32 muted = theme->text_secondary;
    u32 button_col = state == AUTH_FORMAT_CONFIRM ? theme->danger : theme->accent;

    /* A centered surface uses the same palette and rounded edges as desktop
     * windows, keeping setup and sign-in visually part of the same system. */
    int card_w = width < 472 ? width - 32 : 440;
    if (card_w < 240) card_w = width - 16;
    int card_h = show_field ? 320 : 286;
    int card_x = (width - card_w) / 2;
    int card_y = (height - card_h) / 2;
    rounded(card_x - 5, card_y + 6, card_w + 10, card_h + 10,
            UI_RADIUS_LARGE + 2, dark ? 0x000000 : 0x251c3b, dark ? 80 : 44);
    roundrect_stroke(card_x, card_y, card_w, card_h, UI_RADIUS_LARGE, 1,
                     theme->border, theme->surface);

    int title_scale = text_width(title, 2) > card_w - 36 ? 1 : 2;
    centered(card_x + 18, card_y + 28, card_w - 36, "Pollik OS", theme->accent, 1);
    centered(card_x + 18, card_y + 66, card_w - 36, title, text_col, title_scale);
    if (body) centered(card_x + 20, card_y + 102, card_w - 40, body, muted, 1);

    if (show_field) {
        int fy = card_y + 166, gap = 10, btn_w = 112;
        int fw = card_w - 48 - btn_w - gap;
        if (fw < 100) fw = 100;
        draw_field(card_x + 24, fy, fw, secret);
        button_x = card_x + 24 + fw + gap;
        button_y = fy;
        button_w = btn_w;
        button_h = 42;
    } else {
        if (state == AUTH_SETUP_INTRO) {
            centered(card_x + 18, card_y + 148, card_w - 36, "This installer will create the system folders and your", muted, 1);
            centered(card_x + 18, card_y + 166, card_w - 36, "local account. Your password is stored as a salted hash.", muted, 1);
        }
        button_x = card_x + 24;
        button_y = card_y + 210;
        button_w = 150;
        button_h = 42;
    }
    roundrect(button_x, button_y, button_w, button_h, UI_RADIUS_SMALL, button_col);
    centered(button_x, button_y + 14, button_w, button, 0xffffff, 1);

    if (message[0]) centered(card_x + 18, button_y + button_h + 16, card_w - 36, message, theme->danger, 1);
}

/* Returns 1 only when the screen content changed (a button was pressed), so a
 * plain pointer move never forces a full-screen repaint. The compositor draws
 * the cursor on its own cheap path, keeping login as smooth as the desktop. */
int auth_pointer(int x, int y, int button_down) {
    entropy ^= (u32)(x * 257 + y * 17) ^ ticks;
    int changed = 0;
    if (button_down && !pointer_was_down && secret_field_active &&
        x >= eye_x && x < eye_x + eye_w && y >= eye_y && y < eye_y + eye_h) {
        show_password = !show_password;
        changed = 1;
    }
    int inside_field = field_active && x >= field_x && x < field_x + field_w &&
                       y >= field_y && y < field_y + 42 &&
                       !(secret_field_active && x >= eye_x && x < eye_x + eye_w && y >= eye_y && y < eye_y + eye_h);
    if (button_down && !pointer_was_down && inside_field) {
        int next = auth_input_index_at(x);
        input_cursor = next;
        input_anchor = next;
        input_dragging = 1;
        changed = 1;
    } else if (button_down && input_dragging) {
        int next = auth_input_index_at(x);
        if (next != input_cursor) { input_cursor = next; changed = 1; }
    }
    if (!button_down) input_dragging = 0;
    if (button_down && !pointer_was_down && x >= button_x && x < button_x + button_w &&
        y >= button_y && y < button_y + button_h) {
        submit();
        changed = 1;
    }
    pointer_was_down = button_down;
    return changed;
}
