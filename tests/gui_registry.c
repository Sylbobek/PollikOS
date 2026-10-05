/* Native routing test: link the real kernel/gui/apps.c, never app implementations.
 * No libc headers: system.h owns 32-bit uintptr_t and memory declarations.
 * printf's declaration is compatible with the native CRT; -fno-builtin keeps
 * Clang from treating kernel memory declarations as host libc declarations.
 */
#include "../kernel/gui/apps.h"
#include "../kernel/gui/calculator_icon.h"
#include "../kernel/browser/browser.h"
#include "../kernel/ui_data.h"
#include "../kernel/gui/pollikmark.h"
extern int printf(const char *format, ...);
u8 pollikmark_icon[256], pollikmark_alpha[128];
const u32 pollikmark_palette[4] = {0x102030, 0x405060, 0x708090, 0xa0b0c0};
static int mark_poll_result;

_Static_assert(APP_WELCOME == 0 && APP_FILES == 1 && APP_TERMINAL == 2 &&
               APP_NOTES == 3 && APP_SETTINGS == 4 && APP_BROWSER == 5 &&
               APP_POLLIKMARK == 6 && APP_CALCULATOR == 7 && APP_COUNT == 8, "stable registry IDs");

enum { INIT, RENDER, KEY, CLICK, DRAG, OPEN, CLOSE, RESIZE, SCROLL, CURSOR, POLL, OPS };
static int calls[APP_COUNT][OPS], total, last_app, last_op;
static int arg[4], cursor_result, poll_result;
static int init_order[APP_COUNT];
static unsigned checks;
#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { printf("FAIL line %d: %s\n", __LINE__, #expr); return 0; } \
} while (0)

static void reset(void) {
    for (int id = 0; id < APP_COUNT; ++id)
        for (int op = 0; op < OPS; ++op) calls[id][op] = 0;
    for (int i = 0; i < 4; ++i) arg[i] = 0;
    total = 0;
    last_app = last_op = -1;
}
static void record(int id, int op, int a, int b, int c, int d) {
    ++calls[id][op];
    ++total;
    last_app = id;
    last_op = op;
    arg[0] = a; arg[1] = b; arg[2] = c; arg[3] = d;
}
static int only(int id, int op) {
    return total == 1 && last_app == id && last_op == op && calls[id][op] == 1;
}
#define SIMPLE_RENDER(name, id) \
    void name##_render(int w, int h, int active) { record(id, RENDER, w, h, active, 0); }
SIMPLE_RENDER(welcome, APP_WELCOME)
SIMPLE_RENDER(files, APP_FILES)
SIMPLE_RENDER(terminal, APP_TERMINAL)
SIMPLE_RENDER(notes, APP_NOTES)
SIMPLE_RENDER(settings, APP_SETTINGS)
SIMPLE_RENDER(pollikmark, APP_POLLIKMARK)
SIMPLE_RENDER(calculator, APP_CALCULATOR)
void calculator_init(void) { init_order[total]=APP_CALCULATOR;record(APP_CALCULATOR,INIT,0,0,0,0); }
void calculator_key(u8 code,char ch,int shift,int control) {record(APP_CALCULATOR,KEY,code,ch,shift,control); }
void pollikmark_init(void) {
    init_order[total] = APP_POLLIKMARK;
    record(APP_POLLIKMARK, INIT, 0, 0, 0, 0);
}
void pollikmark_key(u8 code, char ch, int shift, int control) {
    record(APP_POLLIKMARK, KEY, code, ch, shift, control);
}
void pollikmark_open(void) { record(APP_POLLIKMARK, OPEN, 0, 0, 0, 0); }
void pollikmark_close(void) { record(APP_POLLIKMARK, CLOSE, 0, 0, 0, 0); }
void pollikmark_resize(int w, int h) { record(APP_POLLIKMARK, RESIZE, w, h, 0, 0); }
int pollikmark_poll(void) {
    record(APP_POLLIKMARK, POLL, 0, 0, 0, 0);
    return mark_poll_result;
}
#define CLICK_STUB(name, id) \
    void name(int x, int y) { record(id, CLICK, x, y, 0, 0); }
CLICK_STUB(welcome_click, APP_WELCOME)
CLICK_STUB(files_click, APP_FILES)
CLICK_STUB(notes_click, APP_NOTES)
CLICK_STUB(settings_click, APP_SETTINGS)
CLICK_STUB(browser_handle_click, APP_BROWSER)
CLICK_STUB(pollikmark_click, APP_POLLIKMARK)
CLICK_STUB(calculator_click, APP_CALCULATOR)
void files_key(u8 code) { record(APP_FILES, KEY, code, 0, 0, 0); }
void files_close(void) { record(APP_FILES, CLOSE, 0, 0, 0, 0); }
int files_poll(void) { record(APP_FILES, POLL, 0, 0, 0, 0); return poll_result; }
void notes_init(void) {
    init_order[total] = APP_NOTES;
    record(APP_NOTES, INIT, 0, 0, 0, 0);
}
void browser_client_init(void) {
    init_order[total] = APP_BROWSER;
    record(APP_BROWSER, INIT, 0, 0, 0, 0);
}
void terminal_key(u8 code, char ch, int shift, int control) {
    record(APP_TERMINAL, KEY, code, ch, shift, control);
}
void terminal_click(int x, int y) { record(APP_TERMINAL, CLICK, x, y, 0, 0); }
int terminal_drag(int x, int y, int active) { record(APP_TERMINAL, DRAG, x, y, active, 0); return 1; }
void terminal_scroll(int delta) { record(APP_TERMINAL, SCROLL, delta, 0, 0, 0); }
void notes_key(u8 code, char ch, int control) {
    record(APP_NOTES, KEY, code, ch, control, 0);
}
void notes_key_ex(u8 code, char ch, int shift, int control) {
    record(APP_NOTES, KEY, code, ch, shift, control);
}
int notes_drag(int x, int y, int active) {
    record(APP_NOTES, DRAG, x, y, active, 0);
    return 1;
}
void notes_scroll(int delta) { record(APP_NOTES, SCROLL, delta, 0, 0, 0); }
void files_scroll(int delta) { record(APP_FILES, SCROLL, delta, 0, 0, 0); }
void notes_resized(int w, int h) { record(APP_NOTES, RESIZE, w, h, 0, 0); }
int notes_cursor(int x, int y) {
    record(APP_NOTES, CURSOR, x, y, 0, 0);
    return cursor_result;
}
void browser_client_render(int w, int h, int active) {
    record(APP_BROWSER, RENDER, w, h, active, 0);
}
void browser_client_key(u8 code, char ch, int shift, int control) {
    record(APP_BROWSER, KEY, code, ch, shift, control);
}
int browser_client_drag(int x, int y, int active) {
    record(APP_BROWSER, DRAG, x, y, active, 0); return 1;
}
void browser_open(void) { record(APP_BROWSER, OPEN, 0, 0, 0, 0); }
void browser_close(void) { record(APP_BROWSER, CLOSE, 0, 0, 0, 0); }
void browser_client_resized(int w, int h) { record(APP_BROWSER, RESIZE, w, h, 0, 0); }
void browser_client_scroll(int delta) { record(APP_BROWSER, SCROLL, delta, 0, 0, 0); }
int browser_cursor(int x, int y) {
    record(APP_BROWSER, CURSOR, x, y, 0, 0);
    return cursor_result;
}
int browser_client_poll(void) {
    record(APP_BROWSER, POLL, 0, 0, 0, 0);
    return poll_result;
}

static int text_equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
#define BIT(op) (1u << (op))
static int metadata(void) {
    static const char *names[] = {"Welcome To pollikos", "Files", "Terminal", "Notes", "Settings", "Browser", "PollikMark3D", "Calculator"};
    static const unsigned callbacks[] = {
        BIT(RENDER) | BIT(CLICK), BIT(RENDER) | BIT(KEY) | BIT(CLICK) | BIT(CLOSE) | BIT(SCROLL) | BIT(POLL),
        BIT(RENDER) | BIT(KEY) | BIT(CLICK) | BIT(DRAG) | BIT(SCROLL),
        BIT(INIT) | BIT(RENDER) | BIT(KEY) | BIT(CLICK) | BIT(DRAG) | BIT(RESIZE) | BIT(SCROLL) | BIT(CURSOR),
        BIT(RENDER) | BIT(CLICK), ((1u << OPS) - 1),
        BIT(INIT) | BIT(RENDER) | BIT(KEY) | BIT(CLICK) | BIT(OPEN) | BIT(CLOSE) | BIT(RESIZE) | BIT(POLL),
        BIT(INIT) | BIT(RENDER) | BIT(KEY) | BIT(CLICK)
    };
    reset();
    for (int id = 0; id < APP_COUNT; ++id) {
        const GuiApp *app = gui_app_get(id);
        CHECK(app != 0);
        CHECK(gui_app_get(id) == app);
        CHECK(text_equal(app->name, names[id]));
        CHECK(app->icon.width == (id == APP_POLLIKMARK || id == APP_CALCULATOR ? 16 : 72));
        CHECK(app->icon.height == app->icon.width);
        CHECK(app->icon.indices && app->icon.alpha && app->icon.palette);
        if (id == APP_POLLIKMARK) {
            CHECK(app->icon.indices == pollikmark_icon);
            CHECK(app->icon.alpha == pollikmark_alpha);
            CHECK(app->icon.palette == pollikmark_palette);
            for (unsigned i = 0; i < sizeof pollikmark_icon; ++i)
                CHECK(app->icon.indices[i] == pollikmark_icon[i]);
            for (unsigned i = 0; i < sizeof pollikmark_alpha; ++i)
                CHECK(app->icon.alpha[i] == pollikmark_alpha[i]);
            for (unsigned i = 0; i < 4; ++i)
                CHECK(app->icon.palette[i] == pollikmark_palette[i]);
        } else if(id==APP_CALCULATOR) {
            for(unsigned i=0;i<sizeof calculator_icon;i++) CHECK(app->icon.indices[i]==calculator_icon[i]);
            for(unsigned i=0;i<sizeof calculator_alpha;i++) CHECK(app->icon.alpha[i]==calculator_alpha[i]);
            for(unsigned i=0;i<4;i++) CHECK(app->icon.palette[i]==calculator_palette[i]);
        } else {
        /* ui_data.h contains static arrays, so compare contents, not addresses
         * across translation units. No generated assets or icon stubs needed. */
        for (unsigned i = 0; i < sizeof icons_index[id]; ++i)
            CHECK(app->icon.indices[i] == icons_index[id][i]);
        for (unsigned i = 0; i < sizeof icons_alpha[id]; ++i)
            CHECK(app->icon.alpha[i] == icons_alpha[id][i]);
        for (unsigned i = 0; i < sizeof icons_palette[id] / sizeof(u32); ++i)
            CHECK(app->icon.palette[i] == icons_palette[id][i]);
        }
        CHECK(app->body_active == (id == APP_POLLIKMARK ? 0x202b40u : id == APP_TERMINAL ? 0x202331u : 0xfaf9fcu));
        CHECK(app->body_inactive == (id == APP_POLLIKMARK ? 0x202b40u : id == APP_TERMINAL ? 0x1a1c27u : 0xf4f2f7u));
        CHECK(app->bottom_inset == (id == APP_BROWSER ? 18 : 0));
        CHECK(app->min_width == (id == APP_SETTINGS ? 640 : id == APP_CALCULATOR ? 320 : 480) &&
              app->min_height == (id == APP_SETTINGS ? 520 : id == APP_CALCULATOR ? 440 : 280));
        CHECK(GUI_CHROME_HEIGHT == 34);
        unsigned present = (!!app->init << INIT) | (!!app->render << RENDER) |
            (!!app->key << KEY) | (!!app->click << CLICK) | (!!app->drag << DRAG) | (!!app->open << OPEN) |
            (!!app->close << CLOSE) | (!!app->resize << RESIZE) |
            (!!app->scroll << SCROLL) | (!!app->cursor << CURSOR) | (!!app->poll << POLL);
        CHECK(present == callbacks[id]);
    }
    CHECK(total == 0);
    return 1;
}

static int invalid_ids(void) {
    static const int ids[] = {-2147483647 - 1, -100, -1, APP_COUNT, APP_COUNT + 1, 2147483647};
    reset();
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; ++i) {
        int id = ids[i];
        CHECK(gui_app_get(id) == 0);
        gui_app_render(id, 640, 480, 1);
        gui_app_key(id, 30, 1, 1);
        gui_app_click(id, 99, -4);
        CHECK(gui_app_drag(id, 99, -4, 1) == -1);
        gui_app_scroll(id, -2);
        gui_app_opened(id);
        gui_app_closed(id);
        gui_app_resized(id, 480, 280);
        CHECK(gui_app_cursor(id, 100, 100) == 0);
        CHECK(total == 0);
    }
    return 1;
}

static int initialization(void) {
    reset();
    gui_apps_init();
    CHECK(total == 4);
    CHECK(calls[APP_NOTES][INIT] == 1 && calls[APP_BROWSER][INIT] == 1 && calls[APP_POLLIKMARK][INIT] == 1 && calls[APP_CALCULATOR][INIT] == 1);
    CHECK(init_order[0] == APP_NOTES && init_order[1] == APP_BROWSER && init_order[2] == APP_POLLIKMARK && init_order[3] == APP_CALCULATOR);
    return 1;
}

static int rendering(void) {
    static const int sizes[][2] = {{480, 280}, {680, 410}, {1200, 800}, {640, 480}, {123, 57}, {0, 0}};
    for (int id = 0; id < APP_COUNT; ++id)
        for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; ++s)
            for (int active = 0; active <= 1; ++active) {
                reset();
                gui_app_render(id, sizes[s][0], sizes[s][1], active);
                CHECK(only(id, RENDER));
                CHECK(arg[0] == sizes[s][0] && arg[1] == sizes[s][1] && arg[2] == active);
            }
    return 1;
}

/* Independent keyboard-row oracle, including unmapped/release scan codes. */
static char mapped(int code) {
    if (code >= 2 && code <= 13) return "1234567890-="[code - 2];
    if (code >= 16 && code <= 27) return "qwertyuiop[]"[code - 16];
    if (code >= 30 && code <= 40) return "asdfghjkl;'"[code - 30];
    if (code >= 43 && code <= 53) return "\\zxcvbnm,./"[code - 43];
    return code == 57 ? ' ' : 0;
}
static int keyboard(void) {
    for (int id = 0; id < APP_COUNT; ++id)
        for (int code = 0; code <= 255; ++code)
            for (int shift = 0; shift <= 1; ++shift)
                for (int control = 0; control <= 1; ++control) {
                    reset();
                    gui_app_key(id, (u8)code, shift, control);
                    if (id == APP_FILES) {
                        CHECK(only(id, KEY) && arg[0] == code);
                        continue;
                    }
                    if (id != APP_TERMINAL && id != APP_NOTES && id != APP_BROWSER && id != APP_POLLIKMARK && id != APP_CALCULATOR) {
                        CHECK(total == 0);
                        continue;
                    }
                    char ch = mapped(code);
                    if ((id == APP_TERMINAL || id == APP_NOTES) && shift && ch >= 'a' && ch <= 'z')
                        ch = (char)(ch - 'a' + 'A');
                    CHECK(only(id, KEY));
                    CHECK(arg[0] == code && arg[1] == ch);
                    if (id == APP_BROWSER || id == APP_POLLIKMARK || id == APP_TERMINAL || id == APP_NOTES || id == APP_CALCULATOR) {
                        CHECK(arg[2] == shift && arg[3] == control);
                    } else {
                        CHECK(arg[2] == control);
                    }
                }
    return 1;
}

static int pointer_routing(void) {
    static const int points[][2] = {{0, 0}, {-17, 4096}, {100, 200}};
    static const int deltas[] = {-4, -1, 0, 1, 7};
    static const int xs[] = {-1, 23, 24, 100, 646, 647, 648};
    static const int ys[] = {-1, 80, 81, 200, 350, 351, 352};
    for (int id = 0; id < APP_COUNT; ++id) {
        for (unsigned p = 0; p < sizeof points / sizeof points[0]; ++p) {
            int x = points[p][0], y = points[p][1];
            reset();
            gui_app_click(id, x, y);
            if (id == APP_WELCOME || id == APP_FILES || id == APP_TERMINAL || id == APP_NOTES || id == APP_SETTINGS || id == APP_BROWSER || id == APP_POLLIKMARK || id == APP_CALCULATOR) {
                CHECK(only(id, CLICK));
                CHECK(arg[0] == x && arg[1] == y);
            } else CHECK(total == 0);
            reset();
            int dragged = gui_app_drag(id, x, y, 1);
            if (id == APP_TERMINAL || id == APP_BROWSER || id == APP_NOTES) {
                CHECK(dragged == 1 && only(id, DRAG));
                CHECK(arg[0] == x && arg[1] == y && arg[2] == 1);
            } else CHECK(dragged == -1 && total == 0);
            for (int result = -1; result <= 1; ++result) {
                reset();
                cursor_result = result;
                int actual = gui_app_cursor(id, x, y);
                if (id == APP_BROWSER || id == APP_NOTES) {
                    CHECK(actual == result && only(id, CURSOR));
                    CHECK(arg[0] == x && arg[1] == y);
                } else {
                    CHECK(total == 0);
                    CHECK(actual == 0);
                }
            }
        }
        for (unsigned d = 0; d < sizeof deltas / sizeof deltas[0]; ++d) {
            reset();
            gui_app_scroll(id, deltas[d]);
            if (id == APP_NOTES || id == APP_BROWSER || id == APP_FILES || id == APP_TERMINAL) {
                CHECK(only(id, SCROLL));
                CHECK(arg[0] == deltas[d] * ((id == APP_NOTES || id == APP_TERMINAL) ? 3 : 1));
            } else CHECK(total == 0);
        }
    }
    for (unsigned x = 0; x < sizeof xs / sizeof xs[0]; ++x)
        for (unsigned y = 0; y < sizeof ys / sizeof ys[0]; ++y) {
            reset();
            cursor_result = (x >= 2 && x <= 4 && y >= 2 && y <= 4);
            CHECK(gui_app_cursor(APP_NOTES, xs[x], ys[y]) == cursor_result);
            CHECK(only(APP_NOTES, CURSOR));
            CHECK(arg[0] == xs[x] && arg[1] == ys[y]);
        }
    return 1;
}

static int lifecycle(void) {
    void (*const dispatch[])(int) = {gui_app_opened, gui_app_closed};
    static const int sizes[][2] = {{480, 280}, {680, 410}, {1200, 800}};
    for (int id = 0; id < APP_COUNT; ++id) {
        CHECK(gui_app_size(id).width == 680 && gui_app_size(id).height == 410);
        for (int op = OPEN; op <= CLOSE; ++op) {
            reset();
            dispatch[op - OPEN](id);
            if (id == APP_BROWSER || id == APP_POLLIKMARK || (id == APP_FILES && op == CLOSE)) CHECK(only(id, op));
            else CHECK(total == 0); /* Includes persistent Terminal/Notes close. */
        }
        for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; ++s) {
            reset();
            gui_app_resized(id, sizes[s][0], sizes[s][1]);
            if (id == APP_BROWSER || id == APP_NOTES || id == APP_POLLIKMARK) {
                CHECK(only(id, RESIZE));
                CHECK(arg[0] == sizes[s][0] && arg[1] == sizes[s][1]);
            } else CHECK(total == 0);
            CHECK(gui_app_size(id).width == sizes[s][0]);
            CHECK(gui_app_size(id).height == sizes[s][1]);
        }
        reset();
        gui_app_resized(id, 0, 280);
        gui_app_resized(id, 480, GUI_CHROME_HEIGHT);
        gui_app_resized(id, -1, -1);
        CHECK(total == 0);
        CHECK(gui_app_size(id).width == 1200 && gui_app_size(id).height == 800);
    }
    return 1;
}

static int polling(void) {
    static const int results[] = {0, 1, 0, -7, 42, 0};
    for (unsigned i = 0; i < sizeof results / sizeof results[0]; ++i)
    for (unsigned j = 0; j < sizeof results / sizeof results[0]; ++j) {
        reset();
        poll_result = results[i];
        mark_poll_result = results[j];
        u32 mask = gui_apps_poll();
        CHECK(total == 3 && calls[APP_FILES][POLL] == 1 &&
              calls[APP_BROWSER][POLL] == 1 && calls[APP_POLLIKMARK][POLL] == 1);
        CHECK(mask == ((results[i] ? (1u << APP_BROWSER) : 0u) |
                       (results[i] ? (1u << APP_FILES) : 0u) |
                       (results[j] ? (1u << APP_POLLIKMARK) : 0u)));
    }
    /* Only apps whose bit is set in active_mask may run their poll callback;
     * a closed/hidden app must never be polled in the background. */
    static const u32 masks[] = {0, 1u << APP_FILES, 1u << APP_BROWSER,
                                1u << APP_POLLIKMARK, (1u << APP_FILES) | (1u << APP_BROWSER)};
    for (unsigned m = 0; m < sizeof masks / sizeof masks[0]; ++m) {
        reset();
        poll_result = 1;
        mark_poll_result = 1;
        u32 mask = gui_apps_poll_mask(masks[m]);
        int expected_files = (masks[m] >> APP_FILES) & 1u;
        int expected_browser = (masks[m] >> APP_BROWSER) & 1u;
        int expected_mark = (masks[m] >> APP_POLLIKMARK) & 1u;
        CHECK(calls[APP_FILES][POLL] == expected_files);
        CHECK(calls[APP_BROWSER][POLL] == expected_browser);
        CHECK(calls[APP_POLLIKMARK][POLL] == expected_mark);
        CHECK(total == expected_files + expected_browser + expected_mark);
        u32 expected_mask = (expected_files ? (1u << APP_FILES) : 0u) |
                            (expected_browser ? (1u << APP_BROWSER) : 0u) |
                            (expected_mark ? (1u << APP_POLLIKMARK) : 0u);
        CHECK(mask == expected_mask);
    }
    return 1;
}

int main(void) {
    static const struct { const char *name; int (*run)(void); } tests[] = {
        {"metadata and optional callbacks", metadata},
        {"invalid IDs are no-ops", invalid_ids},
        {"initialization", initialization},
        {"render adapters", rendering},
        {"key mapping (all 256 codes, Shift/Control)", keyboard},
        {"click, scroll and cursor routing/bounds", pointer_routing},
        {"open/close/resize", lifecycle},
        {"poll bitmask and reset", polling}
    };
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (!tests[i].run()) { printf("FAILED: %s\n", tests[i].name); return 1; }
        printf("PASS: %s\n", tests[i].name);
    }
    printf("PASS: gui registry (%u checks)\n", checks);
    return 0;
}
