#ifndef POLLIK_BROWSER_H
#define POLLIK_BROWSER_H

#include "../system.h"
#include "../mem.h"

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

typedef struct DomNode {
    NodeType type;
    char tag[16];
    char id[32];
    char class_name[64];
    char *text;
    u8 *image;
    int image_w,image_h;
    char href[128];
    char src[128];
    char rel[16];
    char style_attr[128];
    char value[64];
    char input_type[16];
    char name[32];
    char action[128];
    char method[8];
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
    char input_url[256];
    int input_cursor;
    int is_typing_url;
    DomNode *focused_input;
    int focused_cursor;
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
void browser_focus_address(void); /* Ctrl+L, ignored throughout an active load. */
void browser_handle_scroll(int delta);
int browser_cursor(int x, int y);
void browser_mark_dirty(void);

/* HTML Parser */
DomNode *html_parse(const char *html, int len);
void dom_free_tree(DomNode *node);

/* CSS Engine */
void css_apply_styles(DomNode *root, const char *extra_css);

/* Layout Engine */
void layout_compute(DomNode *root, int viewport_w, int *out_total_h);

void browser_load_images(DomNode *,const char *);
/* Painter */
void render_dom(DomNode *node, int origin_x, int origin_y, int viewport_w, int viewport_h, int scroll_y);

/* JavaScript Runtime */
void js_init(void);
void js_execute(const char *code, DomNode *document);
void js_dispatch_event(DomNode *node, const char *event_type);

#endif
