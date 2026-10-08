#ifndef POLLIK_BROWSER_H
#define POLLIK_BROWSER_H

#ifdef POLLIK_BROWSER_STANDALONE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
void *kmalloc(u32 size);
void kfree(void *ptr);
int sys_text_width(const char *text, int scale);
int sys_get_glyph_advance(u8 ch, int scale);
int sys_get_codepoint_advance(uint32_t codepoint,int scale);
void sys_draw_codepoint_clipped(int x,int y,uint32_t codepoint,u32 color,int scale,int x1,int y1,int x2,int y2);
void sys_draw_rect_clipped(int x,int y,int w,int h,u32 c,int x1,int y1,int x2,int y2);
void sys_draw_rounded_clipped(int x,int y,int w,int h,int r,u32 c,int x1,int y1,int x2,int y2);
void sys_draw_letter_clipped(int x,int y,u8 ch,u32 c,int scale,int x1,int y1,int x2,int y2);
void sys_draw_canvas_clipped(int x,int y,int w,int h,const u32 *src,int sw,int sh,int x1,int y1,int x2,int y2);
int ui_is_dark(void);
#else
#include "../system.h"
#endif

typedef enum {
    NODE_DOCUMENT = 0,
    NODE_ELEMENT  = 1,
    NODE_TEXT     = 2
} NodeType;

typedef enum {
    DISPLAY_BLOCK        = 0,
    DISPLAY_INLINE       = 1,
    DISPLAY_INLINE_BLOCK = 2,
    DISPLAY_FLEX         = 3,
    DISPLAY_NONE         = 4
} DisplayType;

typedef enum {
    POS_STATIC   = 0,
    POS_RELATIVE = 1,
    POS_ABSOLUTE = 2,
    POS_FIXED    = 3
} PositionType;

typedef enum {
    TEXT_ALIGN_LEFT   = 0,
    TEXT_ALIGN_CENTER = 1,
    TEXT_ALIGN_RIGHT  = 2
} TextAlign;

typedef enum {
    FLEX_DIR_ROW    = 0,
    FLEX_DIR_COLUMN = 1
} FlexDirection;

typedef enum {
    JUSTIFY_START   = 0,
    JUSTIFY_CENTER  = 1,
    JUSTIFY_BETWEEN = 2,
    JUSTIFY_END     = 3
} JustifyContent;

typedef enum {
    ALIGN_ITEMS_START   = 0,
    ALIGN_ITEMS_CENTER  = 1,
    ALIGN_ITEMS_STRETCH = 2,
    ALIGN_ITEMS_END     = 3
} AlignItems;

typedef struct {
    u32 color;
    u32 bg_color;
    int has_bg_color;
    int width, height; /* -1 for auto */
    int margin_top, margin_bottom, margin_left, margin_right;
    int padding_top, padding_bottom, padding_left, padding_right;
    int border_width;
    u32 border_color;
    int border_radius;
    DisplayType display;
    PositionType position;
    int top, left, right, bottom;
    int font_size;   /* scale 1..5 */
    int font_weight; /* 400 normal, 700 bold */
    TextAlign text_align;
    int line_height;
    int opacity;     /* 0..256 */
    int z_index;
    int gap;
    FlexDirection flex_dir;
    JustifyContent justify;
    AlignItems align_items;
    /* Extended layout model. */
    int min_width, max_width, min_height, max_height; /* -1 = none */
    int box_border_box;      /* 1 = box-sizing: border-box */
    int flex_grow;           /* default 0 */
    int flex_shrink;         /* default 1 */
    int flex_basis;          /* -1 = auto */
    int align_self;          /* -1 = auto, else AlignItems */
    int white_space_pre;     /* 1 = preserve whitespace */
    int underline;           /* text-decoration: underline */
    int visible;             /* visibility (default 1) */
} ComputedStyle;

typedef struct {
    int x, y, w, h;
    int content_w, content_h;
} LayoutBox;

typedef struct EventListener {
    char event_type[16]; /* "click", "input", etc. */
    char js_code[128];
    struct EventListener *next;
} EventListener;

/* Central, conservative limits for untrusted web data. */
#ifdef POLLIK_BROWSER_STANDALONE
#define DOM_MAX_NODES        16384
#define DOM_MAX_DEPTH        128
#define DOM_MAX_ATTRS        64
#define DOM_ATTR_NAME_MAX    256
#define DOM_ATTR_VAL_MAX     65536
#define DOM_TAG_CAP          64
#define DOM_ID_CAP           256
#define DOM_CLASS_CAP        1024
#define DOM_URL_CAP          1024
#define DOM_STYLE_CAP        4096
#else
#define DOM_MAX_NODES        4096
#define DOM_MAX_DEPTH        64
#define DOM_MAX_ATTRS        10
#define DOM_ATTR_NAME_MAX    16
#define DOM_ATTR_VAL_MAX     64
#define DOM_TAG_CAP          16
#define DOM_ID_CAP           32
#define DOM_CLASS_CAP        64
#define DOM_URL_CAP          128
#define DOM_STYLE_CAP        128
#endif
#define DOM_MAX_CLASSES      64
#ifdef POLLIK_BROWSER_STANDALONE
#define WEB_MAX_CSS_SIZE     (512 * 1024)
#else
#define WEB_MAX_CSS_SIZE     (48 * 1024)
#endif
#define WEB_MAX_JS_SIZE      (128 * 1024)
#define WEB_MAX_SCRIPT_COUNT 16
#define WEB_MAX_STYLESHEET_COUNT 8
#define WEB_MAX_IMAGES       24
#define WEB_MAX_TIMERS       24
#define WEB_MAX_LISTENERS    64

typedef struct DomNode {
    NodeType type;
    char tag[DOM_TAG_CAP];
    char id[DOM_ID_CAP];
    char class_name[DOM_CLASS_CAP];
    char *text;
    u8 *image;
    int image_w,image_h;
    struct MediaGif *gif;
    u32 *canvas;            /* <canvas> RGBA8 backing store (PollikGL target) */
    int canvas_w, canvas_h, canvas_dirty;
    char href[DOM_URL_CAP];
    char src[DOM_URL_CAP];
    char rel[16];
    char style_attr[DOM_STYLE_CAP];
    char value[64];
    char input_type[16];
    char name[32];
    char action[DOM_URL_CAP];
    char method[8];
    /* Generic attributes so getAttribute/setAttribute work for any name. */
#ifdef POLLIK_BROWSER_STANDALONE
    char *attr_names[DOM_MAX_ATTRS],*attr_vals[DOM_MAX_ATTRS];
    void *upstream_style_data;
#else
    char attr_names[DOM_MAX_ATTRS][DOM_ATTR_NAME_MAX];
    char attr_vals[DOM_MAX_ATTRS][DOM_ATTR_VAL_MAX];
#endif
    int attr_count;
    struct DomNode *parent;
    struct DomNode *first_child;
    struct DomNode *last_child;
    struct DomNode *next_sibling;
    struct DomNode *prev_sibling;
    ComputedStyle style;
    LayoutBox box;
    EventListener *listeners;
} DomNode;

typedef struct {
    int open;
    int minimized;
    int x, y, w, h;
    char url[256];
    char base_url[256];   /* resolved <base href>, or empty when absent */
    char input_url[256];
    int input_cursor;
    int input_anchor;
    int is_typing_url;
    DomNode *focused_input;
    int focused_cursor;
    int focused_anchor;
    DomNode *selection_node;
    int selection_anchor, selection_focus, selection_drag, selection_all;
    DomNode *hover_node;   /* element under the pointer, for :hover */
    DomNode *active_node;  /* element currently pressed, for :active */
    char title[64];
    char status_msg[64];
    char pending_url[256];
    int has_pending_navigation;
    int is_loading;
    int script_errors;
    int scroll_y;
    int content_height;
    DomNode *document;
    char history[16][256];
    int history_index;
    int history_count;
    char hover_url[128];
    int layout_dirty;
} BrowserApp;

extern BrowserApp g_browser;

/* Lifecycle and GUI */
void browser_init(void);
void browser_open(void);
void browser_close(void);
void browser_navigate(const char *url);
void browser_poll(void);
/* Safe points in load-time CPU work only; inert during client rendering/events. */
void browser_work_checkpoint(void);
void browser_submit_form(DomNode *node);
void browser_render(int is_active);
void browser_handle_click(int mx, int my);
void browser_handle_key(u8 scancode, char ch);
void browser_handle_key_ex(u8 scancode, char ch, int shift, int control);
int browser_drag(int x, int y, int active);
void browser_focus_address(void); /* Ctrl+L, ignored throughout an active load. */
void browser_handle_scroll(int delta);
int browser_cursor(int x, int y);
/* Track the pointer for :hover / :active and repaint when it changes. */
void browser_handle_mouse_move(int mx, int my);
void browser_mark_dirty(void);

/* HTML Parser */
DomNode *html_parse(const char *html, int len);
DomNode *dom_create_node(NodeType type);
DomNode *dom_create_element(const char *tag);
DomNode *dom_create_text(const char *text, int len);
void dom_free_tree(DomNode *node);
void dom_append_child(DomNode *parent, DomNode *child);
void dom_insert_before(DomNode *parent, DomNode *child, DomNode *ref);
void dom_remove_child(DomNode *parent, DomNode *child);
const char *dom_get_attribute(DomNode *node, const char *name);
void dom_set_attribute(DomNode *node, const char *name, const char *val);
void dom_remove_attribute(DomNode *node, const char *name);
int dom_has_class(DomNode *node, const char *cls);
DomNode *dom_get_element_by_id(DomNode *root, const char *id);
DomNode *dom_query_selector(DomNode *root, const char *selector);
DomNode *dom_query_all(DomNode *root, const char *selector, DomNode **out, int max, int *count);
void dom_set_inner_html(DomNode *node, const char *html, int len);
/* Serialize a node's children to HTML into out (NUL-terminated, bounded). */
void dom_serialize_inner(DomNode *node, char *out, int cap);
/* CSS selector matching primitive (compound + descendant/child combinators). */
int css_match_one(DomNode *node, const char *selector, int len);

/* CSS Engine */
void css_apply_styles(DomNode *root, const char *extra_css);
/* Apply one declaration (kebab-case property) to a computed style. Shared with
 * the JavaScript element.style bridge. */
void css_apply_property(ComputedStyle *s, const char *prop, const char *val);

/* Layout Engine */
void layout_compute(DomNode *root, int viewport_w, int *out_total_h);

void browser_load_images(DomNode *,const char *);
int browser_load_cancelled(void);
int browser_advance_media(DomNode *);
/* Painter */
void render_dom(DomNode *node, int origin_x, int origin_y, int viewport_w, int viewport_h, int scroll_y);
int browser_text_offset_at(DomNode *node, int x, int y);

/* JavaScript Runtime */
void js_init(void);
void js_execute(const char *code, DomNode *document);
void js_dispatch_event(DomNode *node, const char *event_type);
/* Mouse position / key made visible to the event object during dispatch. */
void js_set_event_pos(int mx, int my);
void js_set_event_key(int key);
/* Browser task queue: run due timers and animation-frame callbacks. */
void js_service_tasks(void);
int js_has_pending_tasks(void);

#endif
