#include "browser.h"
#include "../mem.h"
#include "../pollikgl.h"
#include "../net/http.h"

#include "../../third_party/elk/elk.h"
#include "../../third_party/elk/pollik_compat.h"
extern jsval_t pollik_js_get(struct js *,jsval_t,const char *);
extern unsigned pollik_js_budget;
static struct js *runtime;
static unsigned char arena[131072] __attribute__((aligned(8)));
static DomNode *g_doc;
static DomNode *bindings[128];
static int binding_count;
static void timers_reset(void);
void js_init(void) {runtime=0;g_doc=0;binding_count=0;memset(bindings,0,sizeof(bindings));timers_reset();}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* DOM query helpers live in html_parser.c (dom_get_element_by_id,
 * dom_query_selector, dom_query_all) and are shared with the CSS engine. */

extern void dom_append_child(DomNode *, DomNode *);
void dom_remove_node(DomNode *node) {
    if (!node || !node->parent) return;
    DomNode *p = node->parent;
    if (node->prev_sibling) {
        node->prev_sibling->next_sibling = node->next_sibling;
    } else {
        p->first_child = node->next_sibling;
    }
    if (node->next_sibling) {
        node->next_sibling->prev_sibling = node->prev_sibling;
    } else {
        p->last_child = node->prev_sibling;
    }
    node->parent = 0;
    node->prev_sibling = 0;
    node->next_sibling = 0;
    browser_mark_dirty();
}

/* DOM subtrees replaced through innerHTML are freed, but JS wrappers can stay
 * reachable for the lifetime of a page. Retire their binding before the
 * allocator can reuse that address for a different object. */
void js_dom_node_destroyed(DomNode *node) {
    if (!node) return;
    for (int i = 0; i < binding_count; i++)
        if (bindings[i] == node) bindings[i] = 0;
    if (g_browser.focused_input == node) {
        g_browser.focused_input = 0;
        g_browser.focused_anchor = -1;
    }
    if (g_browser.selection_node == node) {
        g_browser.selection_node = 0;
        g_browser.selection_drag = 0;
        g_browser.selection_all = 0;
    }
    if (g_browser.hover_node == node) g_browser.hover_node = 0;
}

static u32 parse_color_str(const char *val) {
    if (str_eq(val, "red")) return 0xd32f2f;
    if (str_eq(val, "green")) return 0x388e3c;
    if (str_eq(val, "blue")) return 0x1976d2;
    if (str_eq(val, "white")) return 0xffffff;
    if (str_eq(val, "black")) return 0x000000;
    if (str_eq(val, "yellow")) return 0xfbc02d;
    if (str_eq(val, "purple")) return 0x7b1fa2;
    if (str_eq(val, "gray") || str_eq(val, "grey")) return 0x757575;
    if (val[0] == '#') {
        u32 c = 0;
        int i = 1;
        while (val[i]) {
            char ch = val[i++];
            u32 v = 0;
            if (ch >= '0' && ch <= '9') v = ch - '0';
            else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
            c = (c << 4) | v;
        }
        return c;
    }
    return 0x202020;
}

static int parse_int_str(const char *val) {
    int res = 0;
    while (*val >= '0' && *val <= '9') {
        res = res * 10 + (*val - '0');
        val++;
    }
    return res;
}

void dom_set_style_property(DomNode *node, const char *prop, const char *val) {
    if (!node || !prop || !val) return;

    if (str_eq(prop, "color")) {
        node->style.color = parse_color_str(val);
        for(DomNode *c=node->first_child;c;c=c->next_sibling)if(c->type==NODE_TEXT)c->style.color=node->style.color;
    } else if (str_eq(prop, "backgroundColor") || str_eq(prop, "background-color") || str_eq(prop, "background")) {
        node->style.bg_color = parse_color_str(val);
        node->style.has_bg_color = 1;
    } else if (str_eq(prop, "display")) {
        if (str_eq(val, "none")) node->style.display = DISPLAY_NONE;
        else if (str_eq(val, "block")) node->style.display = DISPLAY_BLOCK;
        else if (str_eq(val, "flex")) node->style.display = DISPLAY_FLEX;
        else if (str_eq(val, "inline")) node->style.display = DISPLAY_INLINE;
    } else if (str_eq(prop, "fontSize") || str_eq(prop, "font-size")) {
        int px=parse_int_str(val);node->style.font_size=px>=40?4:px>=26?3:px>=18?2:1;
    } else if (str_eq(prop, "borderRadius") || str_eq(prop, "border-radius")) {
        node->style.border_radius = parse_int_str(val);
    } else if (str_eq(prop, "width")) {
        node->style.width = parse_int_str(val);
    } else if (str_eq(prop, "height")) {
        node->style.height = parse_int_str(val);
    }
    browser_mark_dirty();
}

void dom_set_text_content(DomNode *node, const char *text) {
    if (!node || !text) return;
    if (node->type == NODE_TEXT) {
        if (node->text) kfree(node->text);
        int l = strlen(text);
        node->text = (char *)kmalloc(l + 1);
        if (node->text) {
            memcpy(node->text, text, l + 1);
        }
    } else {
        /* Find first text child or create one */
        DomNode *child = node->first_child;
        while (child && child->type != NODE_TEXT) {
            child = child->next_sibling;
        }
        if (child) {
            dom_set_text_content(child, text);
        } else {
            DomNode *tn = (DomNode *)kcalloc(1, sizeof(DomNode));
            if (tn) {
                tn->type = NODE_TEXT;
                int l = strlen(text);
                tn->text = (char *)kmalloc(l + 1);
                if (tn->text) memcpy(tn->text, text, l + 1);
                tn->style = node->style;
                dom_append_child(node, tn);
            }
        }
    }
    browser_mark_dirty();
}

void dom_add_event_listener(DomNode *node, const char *event_type, const char *js_code) {
    if (!node || !event_type || !js_code) return;
    EventListener *el = (EventListener *)kcalloc(1, sizeof(EventListener));
    if (!el) return;

    int i = 0;
    while (event_type[i] && i < (int)sizeof(el->event_type) - 1) {
        el->event_type[i] = event_type[i];
        i++;
    }
    el->event_type[i] = '\0';

    i = 0;
    while (js_code[i] && i < (int)sizeof(el->js_code) - 1) {
        el->js_code[i] = js_code[i];
        i++;
    }
    el->js_code[i] = '\0';

    el->next = node->listeners;
    node->listeners = el;
}


extern DomNode *dom_create_element(const char *);
static jsval_t get(jsval_t obj,const char *key){return pollik_js_get(runtime,obj,key);}
static void string(jsval_t v,char *out,int cap) {
 size_t n=0;char *p=js_getstr(runtime,v,&n);if(!p){out[0]=0;return;}
 if(n>=(size_t)cap)n=cap-1;memcpy(out,p,n);out[n]=0;
}
static DomNode *by_id(jsval_t v) {if(js_type(v)!=JS_NUM)return 0;int i=(int)js_getnum(v);return i>=0&&i<binding_count?bindings[i]:0;}
static jsval_t wrap(DomNode *n);
static jsval_t query(struct js *j,jsval_t *a,int count) {
 (void)j;if(count!=1)return js_mknull();char q[128];string(a[0],q,sizeof(q));return wrap(dom_query_selector(g_doc,q));
}
static jsval_t query_id(struct js *j,jsval_t *a,int count) {
 (void)j;if(count!=1)return js_mknull();char q[128];string(a[0],q,sizeof(q));return wrap(dom_get_element_by_id(g_doc,q));
}
static jsval_t create_node(struct js *j,jsval_t *a,int count) {
 (void)j;if(count!=1)return js_mknull();char q[16];string(a[0],q,sizeof(q));return wrap(dom_create_element(q));
}
static jsval_t append_node(struct js *j,jsval_t *a,int count) {
 (void)j;if(count!=2)return js_mkundef();DomNode *p=by_id(a[0]),*c=by_id(a[1]);if(!p||!c)return js_mkundef();
 for(DomNode *n=p;n;n=n->parent)if(n==c)return js_mkundef();
 dom_remove_node(c);dom_append_child(p,c);browser_mark_dirty();return wrap(c);
}
static jsval_t remove_node(struct js *j,jsval_t *a,int count){(void)j;if(count==1)dom_remove_node(by_id(a[0]));return js_mkundef();}
static jsval_t listen(struct js *j,jsval_t *a,int count) {
 if(count!=3 || !by_id(a[0]))return js_mkundef();char type[16],key[48];string(a[1],type,sizeof(type));
 if(!str_eq(type,"click")&&!str_eq(type,"load")&&!str_eq(type,"input")&&!str_eq(type,"change")&&!str_eq(type,"submit"))return js_mkundef();
 snprintf(key,sizeof(key),"_e%d_%s",(int)js_getnum(a[0]),type);js_set(j,js_glob(j),key,a[2]);return js_mkundef();
}
/* ---- Canvas 2D (drawn through PollikGL into a per-node RGBA buffer) ---- */
static DomNode *cv_node(jsval_t v) {
    DomNode *n = by_id(v);
    return (n && n->tag[0] == 'c' && n->tag[1] == 'a' && n->tag[2] == 'n') ? n : 0;
}
static int cv_prepare(DomNode *n) {
    if (n->canvas) return 1;
    if (n->canvas_w < 1) n->canvas_w = 300;
    if (n->canvas_h < 1) n->canvas_h = 150;
    u32 bytes = (u32)n->canvas_w * (u32)n->canvas_h * 4u;
    n->canvas = kmalloc(bytes);
    if (!n->canvas) return 0;
    memset(n->canvas, 0, bytes);
    return 1;
}
static u32 cv_color(jsval_t v) { char s[24]; string(v, s, sizeof(s)); return parse_color_str(s); }
static Pgl cv_begin(DomNode *n) { Pgl g; pgl_begin(&g, n->canvas, n->canvas_w, n->canvas_h, n->canvas_w); return g; }
static jsval_t cv_fill_rect(struct js *j, jsval_t *a, int c) {
    (void)j; if (c != 6) return js_mkundef(); DomNode *n = cv_node(a[0]); if (!n || !cv_prepare(n)) return js_mkundef();
    Pgl g = cv_begin(n);
    pgl_fill_rect(&g, (int)js_getnum(a[1]), (int)js_getnum(a[2]), (int)js_getnum(a[3]), (int)js_getnum(a[4]), cv_color(a[5]));
    browser_mark_dirty(); return js_mkundef();
}
static jsval_t cv_clear_rect(struct js *j, jsval_t *a, int c) {
    (void)j; if (c != 5) return js_mkundef(); DomNode *n = cv_node(a[0]); if (!n || !cv_prepare(n)) return js_mkundef();
    Pgl g = cv_begin(n);
    pgl_fill_rect(&g, (int)js_getnum(a[1]), (int)js_getnum(a[2]), (int)js_getnum(a[3]), (int)js_getnum(a[4]), 0x00000000u);
    browser_mark_dirty(); return js_mkundef();
}
static jsval_t cv_stroke_rect(struct js *j, jsval_t *a, int c) {
    (void)j; if (c != 6) return js_mkundef(); DomNode *n = cv_node(a[0]); if (!n || !cv_prepare(n)) return js_mkundef();
    Pgl g = cv_begin(n);
    pgl_stroke_rect(&g, (int)js_getnum(a[1]), (int)js_getnum(a[2]), (int)js_getnum(a[3]), (int)js_getnum(a[4]), cv_color(a[5]));
    browser_mark_dirty(); return js_mkundef();
}
static jsval_t cv_line(struct js *j, jsval_t *a, int c) {
    (void)j; if (c != 6) return js_mkundef(); DomNode *n = cv_node(a[0]); if (!n || !cv_prepare(n)) return js_mkundef();
    Pgl g = cv_begin(n);
    pgl_line(&g, (int)js_getnum(a[1]), (int)js_getnum(a[2]), (int)js_getnum(a[3]), (int)js_getnum(a[4]), cv_color(a[5]));
    browser_mark_dirty(); return js_mkundef();
}
static jsval_t cv_circle(struct js *j, jsval_t *a, int c) {
    (void)j; if (c != 5) return js_mkundef(); DomNode *n = cv_node(a[0]); if (!n || !cv_prepare(n)) return js_mkundef();
    Pgl g = cv_begin(n);
    pgl_fill_circle(&g, (int)js_getnum(a[1]), (int)js_getnum(a[2]), (int)js_getnum(a[3]), cv_color(a[4]));
    browser_mark_dirty(); return js_mkundef();
}
static jsval_t cv_ring(struct js *j, jsval_t *a, int c) {
    (void)j; if (c != 5) return js_mkundef(); DomNode *n = cv_node(a[0]); if (!n || !cv_prepare(n)) return js_mkundef();
    Pgl g = cv_begin(n);
    pgl_stroke_circle(&g, (int)js_getnum(a[1]), (int)js_getnum(a[2]), (int)js_getnum(a[3]), cv_color(a[4]));
    browser_mark_dirty(); return js_mkundef();
}
static jsval_t cv_text(struct js *j, jsval_t *a, int c) {
    (void)j; if (c != 6) return js_mkundef(); DomNode *n = cv_node(a[0]); if (!n || !cv_prepare(n)) return js_mkundef();
    char s[128]; string(a[1], s, sizeof(s));
    int fs = (int)js_getnum(a[5]); if (fs < 1) fs = 1; else if (fs > 5) fs = 5;
    sys_text_to_buffer(n->canvas, n->canvas_w, n->canvas_h, n->canvas_w,
                       (int)js_getnum(a[2]), (int)js_getnum(a[3]), s, cv_color(a[4]), fs);
    browser_mark_dirty(); return js_mkundef();
}
/* Per-canvas path state lives in C (Elk has neither `this` nor new props). */
static int cv_px[128], cv_py[128], cv_sx[128], cv_sy[128], cv_pen[128];
static int cv_ax[128], cv_ay[128], cv_ar[128], cv_hasarc[128];
static int cv_idx_of(jsval_t v) { if (js_type(v) != JS_NUM) return -1; int i = (int)js_getnum(v); return (i >= 0 && i < 128) ? i : -1; }
static jsval_t cv_reset(struct js *j, jsval_t *a, int c) {
    (void)j; if (c < 1) return js_mkundef(); int i = cv_idx_of(a[0]); if (i < 0) return js_mkundef();
    cv_pen[i] = 0; cv_hasarc[i] = 0; return js_mkundef();
}
static jsval_t cv_move(struct js *j, jsval_t *a, int c) {
    (void)j; if (c < 3) return js_mkundef(); int i = cv_idx_of(a[0]); if (i < 0) return js_mkundef();
    cv_px[i] = cv_sx[i] = (int)js_getnum(a[1]); cv_py[i] = cv_sy[i] = (int)js_getnum(a[2]); cv_pen[i] = 1;
    return js_mkundef();
}
static jsval_t cv_line_to(struct js *j, jsval_t *a, int c) {
    (void)j; if (c < 4) return js_mkundef(); int i = cv_idx_of(a[0]); DomNode *n = cv_node(a[0]);
    if (i < 0 || !n || !cv_prepare(n)) return js_mkundef();
    int x = (int)js_getnum(a[1]), y = (int)js_getnum(a[2]);
    if (cv_pen[i]) { Pgl g = cv_begin(n); pgl_line(&g, cv_px[i], cv_py[i], x, y, cv_color(a[3])); browser_mark_dirty(); }
    cv_px[i] = x; cv_py[i] = y; return js_mkundef();
}
static jsval_t cv_close(struct js *j, jsval_t *a, int c) {
    (void)j; if (c < 2) return js_mkundef(); int i = cv_idx_of(a[0]); DomNode *n = cv_node(a[0]);
    if (i < 0 || !n || !cv_prepare(n)) return js_mkundef();
    if (cv_pen[i]) { Pgl g = cv_begin(n); pgl_line(&g, cv_px[i], cv_py[i], cv_sx[i], cv_sy[i], cv_color(a[1])); browser_mark_dirty(); }
    cv_px[i] = cv_sx[i]; cv_py[i] = cv_sy[i]; return js_mkundef();
}
static jsval_t cv_arc(struct js *j, jsval_t *a, int c) {
    (void)j; if (c < 4) return js_mkundef(); int i = cv_idx_of(a[0]); if (i < 0) return js_mkundef();
    cv_ax[i] = (int)js_getnum(a[1]); cv_ay[i] = (int)js_getnum(a[2]); cv_ar[i] = (int)js_getnum(a[3]); cv_hasarc[i] = 1;
    return js_mkundef();
}
static jsval_t cv_fill_arc(struct js *j, jsval_t *a, int c) {
    (void)j; if (c < 2) return js_mkundef(); int i = cv_idx_of(a[0]); DomNode *n = cv_node(a[0]);
    if (i >= 0 && cv_hasarc[i] && n && cv_prepare(n)) {
        Pgl g = cv_begin(n); pgl_fill_circle(&g, cv_ax[i], cv_ay[i], cv_ar[i], cv_color(a[1])); browser_mark_dirty();
    }
    if (i >= 0) cv_hasarc[i] = 0; return js_mkundef();
}
static jsval_t cv_stroke_arc(struct js *j, jsval_t *a, int c) {
    (void)j; if (c < 2) return js_mkundef(); int i = cv_idx_of(a[0]); DomNode *n = cv_node(a[0]);
    if (i >= 0 && cv_hasarc[i] && n && cv_prepare(n)) {
        Pgl g = cv_begin(n); pgl_stroke_circle(&g, cv_ax[i], cv_ay[i], cv_ar[i], cv_color(a[1])); browser_mark_dirty();
    }
    if (i >= 0) cv_hasarc[i] = 0; return js_mkundef();
}
/* ---- DOM attribute / mutation / query bridge ---- */
static jsval_t attr_get(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 2) return js_mkundef();
    DomNode *n = by_id(a[0]); if (!n) return js_mkundef();
    char k[32]; string(a[1], k, sizeof(k));
    const char *v = dom_get_attribute(n, k);
    return v ? js_mkstr(runtime, v, strlen(v)) : js_mknull();
}
static jsval_t attr_set(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 3) return js_mkundef();
    DomNode *n = by_id(a[0]); if (!n) return js_mkundef();
    char k[32], v[DOM_ATTR_VAL_MAX];
    string(a[1], k, sizeof(k)); string(a[2], v, sizeof(v));
    dom_set_attribute(n, k, v);
    browser_mark_dirty();
    return js_mkundef();
}
static jsval_t attr_remove(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 2) return js_mkundef();
    DomNode *n = by_id(a[0]); if (!n) return js_mkundef();
    char k[32]; string(a[1], k, sizeof(k));
    dom_remove_attribute(n, k);
    browser_mark_dirty();
    return js_mkundef();
}
static jsval_t attr_has(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 2) return js_mkundef();
    DomNode *n = by_id(a[0]); if (!n) return js_mkundef();
    char k[32]; string(a[1], k, sizeof(k));
    return js_mknum(dom_get_attribute(n, k) ? 1 : 0);
}
static jsval_t remove_child_fn(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 2) return js_mkundef();
    DomNode *p = by_id(a[0]), *c = by_id(a[1]);
    if (p && c && c->parent == p) { dom_remove_node(c); browser_mark_dirty(); }
    return js_mkundef();
}
static jsval_t insert_before_fn(struct js *j, jsval_t *a, int count) {
    (void)j; if (count < 2) return js_mkundef();
    DomNode *p = by_id(a[0]), *c = by_id(a[1]);
    if (!p || !c) return js_mkundef();
    for (DomNode *n = p; n; n = n->parent) if (n == c) return js_mkundef();
    DomNode *ref = 0;
    if (count >= 3 && js_type(a[2]) == JS_NUM) {
        int ri = (int)js_getnum(a[2]);
        if (ri >= 0 && ri < binding_count) ref = bindings[ri];
    }
    dom_remove_node(c);
    if (ref && ref->parent == p) dom_insert_before(p, c, ref);
    else dom_append_child(p, c);
    browser_mark_dirty();
    return wrap(c);
}
static jsval_t replace_child_fn(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 3) return js_mkundef();
    DomNode *p = by_id(a[0]), *nw = by_id(a[1]), *old = by_id(a[2]);
    if (!p || !nw || !old || old->parent != p) return js_mkundef();
    dom_insert_before(p, nw, old);
    dom_remove_node(old);
    browser_mark_dirty();
    return wrap(nw);
}
static jsval_t unlisten(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 2) return js_mkundef();
    DomNode *n = by_id(a[0]); if (!n) return js_mkundef();
    char t[16]; string(a[1], t, sizeof(t));
    EventListener **pp = &n->listeners;
    while (*pp) {
        if (str_eq((*pp)->event_type, t)) { EventListener *dead = *pp; *pp = dead->next; kfree(dead); }
        else pp = &(*pp)->next;
    }
    return js_mkundef();
}
static jsval_t focus_fn(struct js *j, jsval_t *a, int count) {
    (void)j; if (count == 1) { DomNode *n = by_id(a[0]); if (n) g_browser.focused_input = n; }
    return js_mkundef();
}
static jsval_t blur_fn(struct js *j, jsval_t *a, int count) {
    (void)j; (void)a; (void)count; g_browser.focused_input = 0; return js_mkundef();
}
static void collect_tag(DomNode *n, const char *tag, DomNode **out, int max, int *count) {
    for (DomNode *c = n ? n->first_child : 0; c; c = c->next_sibling) {
        if (*count >= max) return;
        if (c->type == NODE_ELEMENT && str_eq(c->tag, tag)) out[(*count)++] = c;
        collect_tag(c, tag, out, max, count);
    }
}
static void collect_class(DomNode *n, const char *cls, DomNode **out, int max, int *count) {
    for (DomNode *c = n ? n->first_child : 0; c; c = c->next_sibling) {
        if (*count >= max) return;
        if (c->type == NODE_ELEMENT && dom_has_class(c, cls)) out[(*count)++] = c;
        collect_class(c, cls, out, max, count);
    }
}
static jsval_t wrap_list(DomNode **nodes, int n) {
    jsval_t arr = js_mkobj(runtime);
    struct js_root array_root={0};js_root_acquire(runtime,&array_root,&arr);
    for (int i = 0; i < n; i++) {
        char k[8]; snprintf(k, sizeof(k), "%d", i);
        jsval_t child=wrap(nodes[i]);js_set(runtime, arr, k, child);
    }
    js_set(runtime, arr, "length", js_mknum(n));
    js_root_release(runtime,&array_root);return arr;
}
static jsval_t query_all_fn(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 1) return js_mkundef();
    char q[128]; string(a[0], q, sizeof(q));
    DomNode *out[64]; int n = 0;
    dom_query_all(g_doc, q, out, 64, &n);
    return wrap_list(out, n);
}
static jsval_t by_tag_fn(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 1) return js_mkundef();
    char q[32]; string(a[0], q, sizeof(q));
    for (int i = 0; q[i]; i++) if (q[i] >= 'A' && q[i] <= 'Z') q[i] += 32;
    DomNode *out[64]; int n = 0;
    collect_tag(g_doc, q, out, 64, &n);
    return wrap_list(out, n);
}
static jsval_t by_class_fn(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 1) return js_mkundef();
    char q[64]; string(a[0], q, sizeof(q));
    DomNode *out[64]; int n = 0;
    collect_class(g_doc, q, out, 64, &n);
    return wrap_list(out, n);
}
static jsval_t create_text_fn(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 1) return js_mknull();
    char q[256]; string(a[0], q, sizeof(q));
    int l = (int)strlen(q);
    return wrap(l > 0 ? dom_create_text(q, l) : dom_create_text(" ", 1));
}
/* Serialize the children of a bound node to HTML for element.innerHTML. */
static int inner_html_of(DomNode *n, char *out, int cap) {
    if (!n) { out[0] = 0; return 0; }
    dom_serialize_inner(n, out, cap);
    return (int)strlen(out);
}

/* ---- console ---- */
static void log_value(jsval_t v, char *out, int cap) {
    size_t n = 0;
    char *p = js_getstr(runtime, v, &n);
    if (p && n > 0) {
        if ((int)n >= cap) n = cap - 1;
        memcpy(out, p, n); out[n] = 0;
    } else {
        const char *s = js_str(runtime, v);
        int i = 0;
        if (s) while (s[i] && i < cap - 1) { out[i] = s[i]; i++; }
        out[i] = 0;
    }
}
static jsval_t console_log(struct js *j, jsval_t *a, int count) {
    (void)j;
    serial("JS console: ");
    for (int i = 0; i < count; i++) {
        char buf[128]; log_value(a[i], buf, sizeof(buf));
        if (i) serial(" ");
        serial(buf);
    }
    serial("\n");
    return js_mkundef();
}

/* ---- fetch (real DNS/TCP/TLS/HTTP through the shared kernel stack) ----
 * Elk has no native Promise, so fetch() is synchronous: it performs the
 * request through the regular transport and returns a response object with
 * status/ok/url plus text() and json() accessors. This is an honest bounded
 * model, not a claim of full WHATWG Promise semantics. */
static jsval_t g_last_response;
static jsval_t fetch_text(struct js *j, jsval_t *a, int count) {
    (void)j; (void)a; (void)count;
    jsval_t body = get(g_last_response, "_body");
    if (js_type(body) != JS_STR) return js_mkstr(runtime, "", 0);
    return body;
}
static int json_parse_value(const char **p, jsval_t *out, int depth);
static void json_skip_ws(const char **p) { while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++; }
static int json_parse_string(const char **p, char *out, int cap) {
    int n = 0;
    if (**p != '"') return 0;
    (*p)++;
    while (**p && **p != '"') {
        char c = **p;
        if (c == '\\') {
            (*p)++;
            char e = **p;
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else if (e == 'b') c = '\b';
            else if (e == 'f') c = '\f';
            else if (e == 'u') {
                int v = 0;
                for (int k = 0; k < 4 && (*p)[1]; k++) {
                    (*p)++;
                    char h = **p;
                    int d = (h >= '0' && h <= '9') ? h - '0' :
                            (h >= 'a' && h <= 'f') ? h - 'a' + 10 :
                            (h >= 'A' && h <= 'F') ? h - 'A' + 10 : 0;
                    v = v * 16 + d;
                }
                c = (v >= 32 && v < 127) ? (char)v : '?';
            } else c = e;
        }
        if (n < cap - 1) out[n++] = c;
        (*p)++;
    }
    if (**p == '"') (*p)++;
    out[n] = 0;
    return 1;
}
static jsval_t json_parse_number(const char **p) {
    char buf[48]; int n = 0;
    while ((**p == '-' || **p == '+' || **p == '.' || **p == 'e' || **p == 'E' ||
            (**p >= '0' && **p <= '9')) && n < 47) buf[n++] = *(*p)++;
    buf[n] = 0;
    int neg = 0, i = 0; double d = 0;
    if (buf[0] == '-') { neg = 1; i = 1; }
    for (; buf[i] >= '0' && buf[i] <= '9'; i++) d = d * 10 + (buf[i] - '0');
    if (buf[i] == '.') { i++; double s = 0.1; for (; buf[i] >= '0' && buf[i] <= '9'; i++) { d += (buf[i] - '0') * s; s *= 0.1; } }
    return js_mknum(neg ? -d : d);
}
static int json_parse_value(const char **p, jsval_t *out, int depth) {
    if (depth > 16) return 0;
    json_skip_ws(p);
    char c = **p;
    if (c == '{') {
        (*p)++;
        jsval_t obj = js_mkobj(runtime);
        json_skip_ws(p);
        if (**p == '}') { (*p)++; *out = obj; return 1; }
        while (**p) {
            char key[64];
            json_skip_ws(p);
            if (!json_parse_string(p, key, sizeof(key))) return 0;
            json_skip_ws(p);
            if (**p != ':') return 0;
            (*p)++;
            jsval_t val;
            if (!json_parse_value(p, &val, depth + 1)) return 0;
            js_set(runtime, obj, key, val);
            json_skip_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == '}') { (*p)++; break; }
            return 0;
        }
        *out = obj; return 1;
    }
    if (c == '[') {
        (*p)++;
        jsval_t arr = js_mkobj(runtime);
        int idx = 0;
        json_skip_ws(p);
        if (**p == ']') { (*p)++; js_set(runtime, arr, "length", js_mknum(0)); *out = arr; return 1; }
        while (**p) {
            jsval_t val;
            if (!json_parse_value(p, &val, depth + 1)) return 0;
            char k[12]; snprintf(k, sizeof(k), "%d", idx++);
            js_set(runtime, arr, k, val);
            json_skip_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == ']') { (*p)++; break; }
            return 0;
        }
        js_set(runtime, arr, "length", js_mknum(idx));
        *out = arr; return 1;
    }
    if (c == '"') {
        char s[256];
        if (!json_parse_string(p, s, sizeof(s))) return 0;
        *out = js_mkstr(runtime, s, strlen(s));
        return 1;
    }
    if (c == 't' && !memcmp(*p, "true", 4)) { *p += 4; *out = js_mknum(1); return 1; }
    if (c == 'f' && !memcmp(*p, "false", 5)) { *p += 5; *out = js_mknum(0); return 1; }
    if (c == 'n' && !memcmp(*p, "null", 4)) { *p += 4; *out = js_mknull(); return 1; }
    if (c == '-' || (c >= '0' && c <= '9')) { *out = json_parse_number(p); return 1; }
    return 0;
}
static jsval_t json_parse_fn(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 1) return js_mknull();
    char text[2048]; string(a[0], text, sizeof(text));
    const char *p = text;
    jsval_t out = js_mknull();
    if (!json_parse_value(&p, &out, 0)) return js_mknull();
    return out;
}
static void json_stringify_value(jsval_t v, char *out, int cap, int *n, int depth) {
    if (*n >= cap - 1 || depth > 16) return;
    int t = js_type(v);
    if (t == JS_NUM) {
        char buf[40]; int x = 0;
        int num = (int)js_getnum(v);
        if (num == 0) buf[x++] = '0';
        else { int neg = num < 0; unsigned u = neg ? (unsigned)(-num) : (unsigned)num; char tmp[16]; int k = 0;
               while (u) { tmp[k++] = '0' + u % 10; u /= 10; }
               if (neg) buf[x++] = '-'; while (k) buf[x++] = tmp[--k]; }
        buf[x] = 0;
        for (int i = 0; buf[i] && *n < cap - 1; i++) out[(*n)++] = buf[i];
    } else if (t == JS_STR) {
        size_t sl = 0; char *s = js_getstr(runtime, v, &sl);
        if (*n < cap - 1) out[(*n)++] = '"';
        for (size_t i = 0; i < sl && *n < cap - 1; i++) {
            char c = s[i];
            if (c == '"' || c == '\\') { out[(*n)++] = '\\'; if (*n < cap - 1) out[(*n)++] = c; }
            else if (c == '\n') { out[(*n)++] = '\\'; if (*n < cap - 1) out[(*n)++] = 'n'; }
            else out[(*n)++] = c;
        }
        if (*n < cap - 1) out[(*n)++] = '"';
    } else if (t == JS_PRIV) {
        /* Elk objects/arrays are JS_PRIV. Arrays carry a numeric "length". */
        jsval_t len = get(v, "length");
        if (js_type(len) == JS_NUM) {
            int l = (int)js_getnum(len);
            if (l > 64) l = 64;
            if (*n < cap - 1) out[(*n)++] = '[';
            for (int i = 0; i < l; i++) {
                char k[12]; snprintf(k, sizeof(k), "%d", i);
                jsval_t item = get(v, k);
                json_stringify_value(item, out, cap, n, depth + 1);
                if (i + 1 < l && *n < cap - 1) out[(*n)++] = ',';
            }
            if (*n < cap - 1) out[(*n)++] = ']';
        } else {
            /* Elk does not expose key enumeration; emit an empty object rather
             * than guessing at untracked properties. */
            if (*n < cap - 1) out[(*n)++] = '{';
            if (*n < cap - 1) out[(*n)++] = '}';
        }
    } else if (js_type(v) == JS_UNDEF || js_type(v) == JS_NULL) {
        const char *nul = "null";
        for (int i = 0; nul[i] && *n < cap - 1; i++) out[(*n)++] = nul[i];
    }
}
static jsval_t json_stringify_fn(struct js *j, jsval_t *a, int count) {
    (void)j; if (count < 1) return js_mkstr(runtime, "null", 4);
    char out[2048]; int n = 0;
    json_stringify_value(a[0], out, sizeof(out), &n, 0);
    out[n] = 0;
    return js_mkstr(runtime, out, n);
}
static jsval_t fetch_fn(struct js *j, jsval_t *a, int count) {
    (void)j; if (count < 1) return js_mknull();
    char ref[256], url[256]; string(a[0], ref, sizeof(ref));
    if (!http_resolve_url(g_browser.url, ref, url, sizeof(url))) return js_mknull();
    HttpResponse r;
    if (!http_get(url, &r)) { http_response_free(&r); return js_mknull(); }
    jsval_t resp = js_mkobj(runtime);
    js_set(runtime, resp, "status", js_mknum(r.status_code));
    js_set(runtime, resp, "ok", js_mknum(r.status_code >= 200 && r.status_code < 300));
    js_set(runtime, resp, "url", js_mkstr(runtime, r.final_url[0] ? r.final_url : url, strlen(r.final_url[0] ? r.final_url : url)));
    int blen = r.body_len > 0 ? r.body_len : 0;
    if (blen > 4096) blen = 4096;
    char *body = kmalloc((u32)blen + 1);
    if (body) {
        if (r.body && blen > 0) memcpy(body, r.body, blen);
        body[blen] = 0;
        js_set(runtime, resp, "_body", js_mkstr(runtime, body, blen));
        kfree(body);
    } else {
        js_set(runtime, resp, "_body", js_mkstr(runtime, "", 0));
    }
    g_last_response = resp;
    js_set(runtime, resp, "text", js_mkfun(fetch_text));
    js_set(runtime, resp, "json", js_mkfun(json_parse_fn));
    http_response_free(&r);
    return resp;
}

/* ---- timers (tick-based; the browser event loop drains them) ---- */
typedef struct { int used; int is_interval; u32 due; u32 period; } JsTimer;
static JsTimer g_timers[WEB_MAX_TIMERS];
static int g_raf_pending;
static void timers_reset(void) { memset(g_timers, 0, sizeof(g_timers)); g_raf_pending = 0; }
static int timer_alloc(void) {
    for (int i = 0; i < WEB_MAX_TIMERS; i++) if (!g_timers[i].used) return i;
    return -1;
}
static jsval_t timer_start(struct js *j, jsval_t *a, int count, int interval) {
    (void)j;
    if (count < 1) return js_mknum(0);
    int slot = timer_alloc();
    if (slot < 0) return js_mknum(0);
    char name[16]; snprintf(name, sizeof(name), "_tm%d", slot);
    js_set(runtime, js_glob(runtime), name, a[0]);
    int ms = count >= 2 ? (int)js_getnum(a[1]) : 0;
    if (ms < 0) ms = 0;
    if (ms > 60000) ms = 60000;
    u32 period = (u32)((ms + 9) / 10);
    if (period == 0) period = 1;
    g_timers[slot].used = 1;
    g_timers[slot].is_interval = interval;
    g_timers[slot].period = period;
    g_timers[slot].due = ticks + period;
    return js_mknum(slot + 1);
}
static jsval_t timeout_fn(struct js *j, jsval_t *a, int count) { return timer_start(j, a, count, 0); }
static jsval_t interval_fn(struct js *j, jsval_t *a, int count) { return timer_start(j, a, count, 1); }
static jsval_t clear_timer_fn(struct js *j, jsval_t *a, int count) {
    (void)j;
    if (count >= 1) { int slot = (int)js_getnum(a[0]) - 1; if (slot >= 0 && slot < WEB_MAX_TIMERS) g_timers[slot].used = 0; }
    return js_mkundef();
}
static jsval_t raf_fn(struct js *j, jsval_t *a, int count) {
    (void)j;
    if (count >= 1) { js_set(runtime, js_glob(runtime), "_raf", a[0]); g_raf_pending = 1; }
    return js_mknum(1);
}
static jsval_t cancel_raf_fn(struct js *j, jsval_t *a, int count) { (void)j; (void)a; (void)count; g_raf_pending = 0; return js_mkundef(); }
void js_service_tasks(void) {
    if (!runtime || !g_doc) return;
    int ran = 0;
    for (int i = 0; i < WEB_MAX_TIMERS && ran < 4; i++) {
        if (!g_timers[i].used) continue;
        if ((int)(ticks - g_timers[i].due) < 0) continue;
        char name[16]; snprintf(name, sizeof(name), "_tm%d", i);
        jsval_t fn = get(js_glob(runtime), name);
        if (js_type(fn) == JS_UNDEF) { g_timers[i].used = 0; continue; }
        if (g_timers[i].is_interval) g_timers[i].due = ticks + g_timers[i].period;
        else g_timers[i].used = 0;
        char call[24]; snprintf(call, sizeof(call), "%s();", name);
        js_execute(call, g_doc);
        ran++;
    }
    if (g_raf_pending) {
        g_raf_pending = 0;
        jsval_t fn = get(js_glob(runtime), "_raf");
        if (js_type(fn) != JS_UNDEF) js_execute("_raf();", g_doc);
    }
}
int js_has_pending_tasks(void) {
    for (int i = 0; i < WEB_MAX_TIMERS; i++) if (g_timers[i].used) return 1;
    return g_raf_pending;
}

/* ---- navigation / location / history ---- */
static jsval_t nav_back(struct js *j, jsval_t *a, int count) {
    (void)j; (void)a; (void)count;
    if (g_browser.history_index > 0) {
        g_browser.history_index--;
        browser_navigate(g_browser.history[g_browser.history_index]);
    }
    return js_mkundef();
}
static jsval_t nav_forward(struct js *j, jsval_t *a, int count) {
    (void)j; (void)a; (void)count;
    if (g_browser.history_index < g_browser.history_count - 1) {
        g_browser.history_index++;
        browser_navigate(g_browser.history[g_browser.history_index]);
    }
    return js_mkundef();
}
static jsval_t nav_assign(struct js *j, jsval_t *a, int count) {
    (void)j; if (count != 1) return js_mkundef();
    char u[256]; string(a[0], u, sizeof(u));
    char abs[256];
    if (http_resolve_url(g_browser.url, u, abs, sizeof(abs))) browser_navigate(abs);
    return js_mkundef();
}
static jsval_t nav_reload(struct js *j, jsval_t *a, int count) {
    (void)j; (void)a; (void)count;
    browser_navigate(g_browser.url);
    return js_mkundef();
}

static jsval_t wrap(DomNode *n) {
 char name[16];
 if(!n)return js_mknull();int i=0;while(i<binding_count&&bindings[i]!=n)i++;
 snprintf(name,sizeof(name),"_n%d",i);
 if(i<binding_count)return get(js_glob(runtime),name);
 if(binding_count==128)return js_mknull();bindings[binding_count++]=n;
 jsval_t obj=js_mkobj(runtime);
 struct js_root object_root={0};js_root_acquire(runtime,&object_root,&obj);
 js_set(runtime,js_glob(runtime),name,obj);
 js_set(runtime,obj,"__node",js_mknum(i));
 jsval_t style=js_mkobj(runtime);js_set(runtime,obj,"style",style);
 static const char *props[]={"color","backgroundColor","background","display","fontSize","width","height",
   "borderRadius","borderColor","borderWidth","marginTop","marginBottom","marginLeft","marginRight",
   "paddingTop","paddingBottom","paddingLeft","paddingRight","position","top","left","right","bottom",
   "opacity","textAlign","fontWeight","minWidth","maxWidth","minHeight","maxHeight","lineHeight",
   "textDecoration","visibility","zIndex","gap","flexDirection","justifyContent","alignItems",
   "flexGrow","flexShrink","flexBasis","boxSizing","whiteSpace","overflow"};
 for(unsigned k=0;k<sizeof(props)/sizeof(props[0]);k++)js_set(runtime,style,props[k],js_mkundef());
 js_set(runtime,obj,"appendChild",js_mkundef());js_set(runtime,obj,"remove",js_mkundef());
 js_set(runtime,obj,"removeChild",js_mkundef());js_set(runtime,obj,"insertBefore",js_mkundef());
 js_set(runtime,obj,"replaceChild",js_mkundef());js_set(runtime,obj,"addEventListener",js_mkundef());
 js_set(runtime,obj,"removeEventListener",js_mkundef());
 js_set(runtime,obj,"getAttribute",js_mkundef());js_set(runtime,obj,"setAttribute",js_mkundef());
 js_set(runtime,obj,"removeAttribute",js_mkundef());js_set(runtime,obj,"hasAttribute",js_mkundef());
 js_set(runtime,obj,"focus",js_mkundef());js_set(runtime,obj,"blur",js_mkundef());
 js_set(runtime,obj,"querySelector",js_mkundef());js_set(runtime,obj,"querySelectorAll",js_mkundef());
 js_set(runtime,obj,"getElementsByTagName",js_mkundef());js_set(runtime,obj,"getElementsByClassName",js_mkundef());
 const char *text=n->text?n->text:(n->first_child&&n->first_child->text?n->first_child->text:"");
 js_set(runtime,obj,"textContent",js_mkstr(runtime,text,strlen(text)));
 {
   char ih[512];
   int hl = inner_html_of(n, ih, sizeof(ih));
   js_set(runtime,obj,"innerHTML",js_mkstr(runtime,ih,hl));
 }
 js_set(runtime,obj,"className",js_mkstr(runtime,n->class_name,strlen(n->class_name)));
 js_set(runtime,obj,"id",js_mkstr(runtime,n->id,strlen(n->id)));
 js_set(runtime,obj,"value",js_mkstr(runtime,n->value,strlen(n->value)));
 js_set(runtime,obj,"checked",js_mknum(n->value[0] ? 1 : 0));
 js_set(runtime,obj,"disabled",js_mknum(0));
 js_set(runtime,obj,"tagName",js_mkstr(runtime,n->tag,strlen(n->tag)));
 /* Navigation snapshots (real DOM links). */
 /* Read the rooted target only AFTER a recursive wrapper may collect. */
 jsval_t related=n->parent?wrap(n->parent):js_mknull();js_set(runtime,obj,"parentNode",related);
 related=n->first_child?wrap(n->first_child):js_mknull();js_set(runtime,obj,"firstChild",related);
 related=n->last_child?wrap(n->last_child):js_mknull();js_set(runtime,obj,"lastChild",related);
 related=n->next_sibling?wrap(n->next_sibling):js_mknull();js_set(runtime,obj,"nextSibling",related);
 related=n->prev_sibling?wrap(n->prev_sibling):js_mknull();js_set(runtime,obj,"previousSibling",related);
 int kid=0; for(DomNode *c=n->first_child;c;c=c->next_sibling) if(c->type==NODE_ELEMENT) kid++;
 js_set(runtime,obj,"childElementCount",js_mknum(kid));
 static char code[2048];
 snprintf(code,sizeof(code),
   "%s.appendChild=function(c){return _append(%d,c.__node);};"
   "%s.remove=function(){_remove(%d);};"
   "%s.removeChild=function(c){_removeChild(%d,c.__node);};"
   "%s.insertBefore=function(c,r){return _insertBefore(%d,c.__node,r?r.__node:-1);};"
   "%s.replaceChild=function(nw,old){return _replaceChild(%d,nw.__node,old.__node);};"
   "%s.addEventListener=function(t,f){_listen(%d,t,f);};"
   "%s.removeEventListener=function(t,f){_unlisten(%d,t);};"
   "%s.getAttribute=function(k){return _attrGet(%d,k);};"
   "%s.setAttribute=function(k,v){_attrSet(%d,k,v);};"
   "%s.removeAttribute=function(k){_attrRemove(%d,k);};"
   "%s.hasAttribute=function(k){return _attrHas(%d,k);};"
   "%s.focus=function(){_focus(%d);};"
   "%s.blur=function(){_blur(%d);};"
   "%s.querySelector=function(q){return _qsel(%d,q);};"
   "%s.querySelectorAll=function(q){return _qall(%d,q);};"
   "%s.getElementsByTagName=function(q){return _byTag(%d,q);};"
   "%s.getElementsByClassName=function(q){return _byClass(%d,q);};",
   name,i,name,i,name,i,name,i,name,i,name,i,name,i,name,i,name,i,name,i,name,i,name,i,name,i,
   name,i,name,i,name,i,name,i);
 js_eval(runtime,code,strlen(code));
 if(n->tag[0]=='c'&&n->tag[1]=='a'&&n->tag[2]=='n'&&n->tag[3]=='v') {
   jsval_t ctx=js_mkobj(runtime);
   static const char *cvprops[]={"fillStyle","strokeStyle","lineWidth","globalAlpha","font","textAlign",
     "fillRect","clearRect","strokeRect","rect","beginPath","moveTo","lineTo","closePath","arc","fill",
     "stroke","fillText","measureText","save","restore","translate","scale","rotate"};
   for(unsigned q=0;q<sizeof(cvprops)/sizeof(cvprops[0]);q++)js_set(runtime,ctx,cvprops[q],js_mkundef());
   js_set(runtime,ctx,"fillStyle",js_mkstr(runtime,"#000000",7));
   js_set(runtime,ctx,"strokeStyle",js_mkstr(runtime,"#000000",7));
   js_set(runtime,ctx,"lineWidth",js_mknum(1));
   js_set(runtime,ctx,"globalAlpha",js_mknum(1));
   char cname[20];snprintf(cname,sizeof(cname),"_c%d",i);
   js_set(runtime,js_glob(runtime),cname,ctx);
   js_set(runtime,obj,"_ctx",ctx);
   js_set(runtime,obj,"getContext",js_mkundef());
   static char cvcode[1600];int k=0;
   #define CV(...) k+=snprintf(cvcode+k,sizeof(cvcode)-k,__VA_ARGS__)
   CV("%s.getContext=function(t){if(t!=='2d')return null;return %s;};",name,cname);
   CV("%s.fillRect=function(x,y,w,h){_cv_rect(%d,x|0,y|0,w|0,h|0,%s.fillStyle);};",cname,i,cname);
   CV("%s.clearRect=function(x,y,w,h){_cv_clear(%d,x|0,y|0,w|0,h|0);};",cname,i);
   CV("%s.strokeRect=function(x,y,w,h){_cv_srect(%d,x|0,y|0,w|0,h|0,%s.strokeStyle);};",cname,i,cname);
   CV("%s.rect=function(x,y,w,h){_cv_srect(%d,x|0,y|0,w|0,h|0,%s.strokeStyle);};",cname,i,cname);
   CV("%s.beginPath=function(){_cv_reset(%d);};",cname,i);
   CV("%s.moveTo=function(x,y){_cv_move(%d,x|0,y|0);};",cname,i);
   CV("%s.lineTo=function(x,y){_cv_lineto(%d,x|0,y|0,%s.strokeStyle);};",cname,i,cname);
   CV("%s.closePath=function(){_cv_close(%d,%s.strokeStyle);};",cname,i,cname);
   CV("%s.arc=function(x,y,r,a0,a1){_cv_arc(%d,x|0,y|0,r|0);};",cname,i);
   CV("%s.fill=function(){_cv_fillarc(%d,%s.fillStyle);};",cname,i,cname);
   CV("%s.stroke=function(){_cv_strokearc(%d,%s.strokeStyle);};",cname,i,cname);
   CV("%s.fillText=function(s,x,y,fs){_cv_text(%d,s,x|0,y|0,%s.fillStyle,fs||1);};",cname,i,cname);
   CV("%s.save=function(){};%s.restore=function(){};",cname,cname);
   CV("%s.translate=function(){};%s.scale=function(){};%s.rotate=function(){};",cname,cname,cname);
   CV("%s.measureText=function(s){return {width:((s.length|0)*6)};};",cname);
   #undef CV
   jsval_t er=js_eval(runtime,cvcode,strlen(cvcode));
   if(js_type(er)==JS_ERR){serial("CVJS: ");serial(js_str(runtime,er));serial("\n");}
 }
 js_root_release(runtime,&object_root);
 return get(js_glob(runtime),name);
}
/* Map a camelCase style property to the kebab-case CSS name. */
static void css_name(const char *in, char *out, int cap) {
    int o = 0;
    for (int i = 0; in[i] && o < cap - 1; i++) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z') {
            if (o < cap - 2) out[o++] = '-';
            out[o++] = (char)(c + 32);
        } else {
            out[o++] = c;
        }
    }
    out[o] = 0;
}
static void sync_dom(void) {
 static const char *props[]={"color","backgroundColor","background","display","fontSize","width","height",
   "borderRadius","borderColor","borderWidth","marginTop","marginBottom","marginLeft","marginRight",
   "paddingTop","paddingBottom","paddingLeft","paddingRight","position","top","left","right","bottom",
   "opacity","textAlign","fontWeight","minWidth","maxWidth","minHeight","maxHeight","lineHeight",
   "textDecoration","visibility","zIndex","gap","flexDirection","justifyContent","alignItems",
   "flexGrow","flexShrink","flexBasis","boxSizing","whiteSpace","overflow"};
 for(int i=0;i<binding_count;i++) {
  DomNode *n=bindings[i];if(!n)continue;char name[16];snprintf(name,sizeof(name),"_n%d",i);
  jsval_t obj=get(js_glob(runtime),name),style=get(obj,"style");
  for(unsigned k=0;k<sizeof(props)/sizeof(props[0]);k++){
    jsval_t v=get(style,props[k]);
    if(js_type(v)==JS_STR){
      char val[64];string(v,val,sizeof(val));
      char prop[48];css_name(props[k],prop,sizeof(prop));
      css_apply_property(&n->style,prop,val);
    }
  }
  jsval_t tv=get(obj,"textContent");if(js_type(tv)==JS_STR){char val[256];string(tv,val,sizeof(val));const char *old=n->text?n->text:(n->first_child&&n->first_child->text?n->first_child->text:"");if(!str_eq(val,old))dom_set_text_content(n,val);}
  jsval_t cv=get(obj,"className");if(js_type(cv)==JS_STR){char val[64];string(cv,val,sizeof(val));if(!str_eq(val,n->class_name))dom_set_attribute(n,"class",val);}
  jsval_t iv=get(obj,"id");if(js_type(iv)==JS_STR){char val[32];string(iv,val,sizeof(val));if(!str_eq(val,n->id))dom_set_attribute(n,"id",val);}
  jsval_t vv=get(obj,"value");if(js_type(vv)==JS_STR){char val[64];string(vv,val,sizeof(val));if(!str_eq(val,n->value)){int m=0;while(val[m]&&m<63){n->value[m]=val[m];m++;}n->value[m]=0;}}
  jsval_t hv=get(obj,"innerHTML");
  if(js_type(hv)==JS_STR){
   char val[512];string(hv,val,sizeof(val));
   char current[512];inner_html_of(n,current,sizeof(current));
   if(!str_eq(val,current))dom_set_inner_html(n,val,(int)strlen(val));
  }
 }
 jsval_t title=get(get(js_glob(runtime),"document"),"title");
 if(js_type(title)==JS_STR) {
  char previous[sizeof(g_browser.title)];memcpy(previous,g_browser.title,sizeof(previous));
  string(title,g_browser.title,sizeof(g_browser.title));
  if(!str_eq(previous,g_browser.title)) {serial("BROWSER title: ");serial(g_browser.title);serial("\n");}
 }
}
/* Elk supports `let` but not `var`/`const`; rewrite declarations so ordinary
 * page scripts load like a normal browser. */
static char *js_decl_shim(const char *code) {
    u32 n = strlen(code);
    char *out = kmalloc(n + 8);
    if (!out) return 0;
    u32 o = 0;
    for (u32 i = 0; i < n;) {
        int boundary = (i == 0) || !((code[i-1] >= 'a' && code[i-1] <= 'z') ||
            (code[i-1] >= 'A' && code[i-1] <= 'Z') ||
            (code[i-1] >= '0' && code[i-1] <= '9') || code[i-1] == '_' || code[i-1] == '$');
        if (boundary && code[i] == 'v' && code[i+1] == 'a' && code[i+2] == 'r' &&
            (code[i+3] == ' ' || code[i+3] == '\t' || code[i+3] == '\n' || code[i+3] == '\r')) {
            out[o++]='l'; out[o++]='e'; out[o++]='t'; out[o++]=code[i+3]; i += 4; continue;
        }
        if (boundary && code[i] == 'c' && code[i+1] == 'o' && code[i+2] == 'n' && code[i+3] == 's' &&
            code[i+4] == 't' && (code[i+5]==' ' || code[i+5]=='\t' || code[i+5]=='\n' || code[i+5]=='\r')) {
            out[o++]='l'; out[o++]='e'; out[o++]='t'; out[o++]=code[i+5]; i += 6; continue;
        }
        out[o++] = code[i++];
    }
    out[o] = 0;
    return out;
}
void js_execute(const char *code,DomNode *document) {
 if(!code||!document)return;g_doc=document;pollik_js_budget=100000;

 if(!runtime) {
  runtime=js_create(arena,sizeof(arena));if(!runtime)return;js_setmaxcss(runtime,8192);
  jsval_t global=js_glob(runtime),doc=js_mkobj(runtime);js_set(runtime,global,"document",doc);
  js_set(runtime,doc,"querySelector",js_mkfun(query));js_set(runtime,doc,"querySelectorAll",js_mkfun(query_all_fn));
  js_set(runtime,doc,"getElementById",js_mkfun(query_id));js_set(runtime,doc,"createElement",js_mkfun(create_node));
  js_set(runtime,doc,"createTextNode",js_mkfun(create_text_fn));
  js_set(runtime,doc,"getElementsByTagName",js_mkfun(by_tag_fn));
  js_set(runtime,doc,"getElementsByClassName",js_mkfun(by_class_fn));
  js_set(runtime,doc,"title",js_mkstr(runtime,g_browser.title,strlen(g_browser.title)));
  js_set(runtime,global,"_append",js_mkfun(append_node));js_set(runtime,global,"_remove",js_mkfun(remove_node));js_set(runtime,global,"_listen",js_mkfun(listen));
  js_set(runtime,global,"_removeChild",js_mkfun(remove_child_fn));js_set(runtime,global,"_insertBefore",js_mkfun(insert_before_fn));
  js_set(runtime,global,"_replaceChild",js_mkfun(replace_child_fn));js_set(runtime,global,"_unlisten",js_mkfun(unlisten));
  js_set(runtime,global,"_attrGet",js_mkfun(attr_get));js_set(runtime,global,"_attrSet",js_mkfun(attr_set));
  js_set(runtime,global,"_attrRemove",js_mkfun(attr_remove));js_set(runtime,global,"_attrHas",js_mkfun(attr_has));
  js_set(runtime,global,"_focus",js_mkfun(focus_fn));js_set(runtime,global,"_blur",js_mkfun(blur_fn));
  js_set(runtime,global,"_qsel",js_mkfun(query));js_set(runtime,global,"_qall",js_mkfun(query_all_fn));
  js_set(runtime,global,"_byTag",js_mkfun(by_tag_fn));js_set(runtime,global,"_byClass",js_mkfun(by_class_fn));
  jsval_t console=js_mkobj(runtime);js_set(runtime,global,"console",console);
  js_set(runtime,console,"log",js_mkfun(console_log));js_set(runtime,console,"warn",js_mkfun(console_log));
  js_set(runtime,console,"error",js_mkfun(console_log));js_set(runtime,console,"info",js_mkfun(console_log));
  js_set(runtime,global,"setTimeout",js_mkfun(timeout_fn));js_set(runtime,global,"clearTimeout",js_mkfun(clear_timer_fn));
  js_set(runtime,global,"setInterval",js_mkfun(interval_fn));js_set(runtime,global,"clearInterval",js_mkfun(clear_timer_fn));
  js_set(runtime,global,"requestAnimationFrame",js_mkfun(raf_fn));js_set(runtime,global,"cancelAnimationFrame",js_mkfun(cancel_raf_fn));
  js_set(runtime,global,"window",global);
  jsval_t loc=js_mkobj(runtime);js_set(runtime,global,"location",loc);
  js_set(runtime,loc,"href",js_mkstr(runtime,g_browser.url,strlen(g_browser.url)));
  js_set(runtime,loc,"assign",js_mkfun(nav_assign));js_set(runtime,loc,"reload",js_mkfun(nav_reload));
  jsval_t hist=js_mkobj(runtime);js_set(runtime,global,"history",hist);
  js_set(runtime,hist,"back",js_mkfun(nav_back));js_set(runtime,hist,"forward",js_mkfun(nav_forward));
  jsval_t nav=js_mkobj(runtime);js_set(runtime,global,"navigator",nav);
  js_set(runtime,nav,"userAgent",js_mkstr(runtime,"PollikOS Web/0.1",16));
  js_set(runtime,nav,"platform",js_mkstr(runtime,"PollikOS i386",12));
  js_set(runtime,global,"fetch",js_mkfun(fetch_fn));
  jsval_t json=js_mkobj(runtime);js_set(runtime,global,"JSON",json);
  js_set(runtime,json,"parse",js_mkfun(json_parse_fn));
  js_set(runtime,json,"stringify",js_mkfun(json_stringify_fn));
  js_set(runtime,global,"_cv_rect",js_mkfun(cv_fill_rect));js_set(runtime,global,"_cv_clear",js_mkfun(cv_clear_rect));
  js_set(runtime,global,"_cv_srect",js_mkfun(cv_stroke_rect));js_set(runtime,global,"_cv_line",js_mkfun(cv_line));
  js_set(runtime,global,"_cv_circle",js_mkfun(cv_circle));js_set(runtime,global,"_cv_ring",js_mkfun(cv_ring));js_set(runtime,global,"_cv_text",js_mkfun(cv_text));
  js_set(runtime,global,"_cv_reset",js_mkfun(cv_reset));js_set(runtime,global,"_cv_move",js_mkfun(cv_move));
  js_set(runtime,global,"_cv_lineto",js_mkfun(cv_line_to));js_set(runtime,global,"_cv_close",js_mkfun(cv_close));
  js_set(runtime,global,"_cv_arc",js_mkfun(cv_arc));js_set(runtime,global,"_cv_fillarc",js_mkfun(cv_fill_arc));
  js_set(runtime,global,"_cv_strokearc",js_mkfun(cv_stroke_arc));
  jsval_t body=wrap(dom_query_selector(document,"body"));doc=get(js_glob(runtime),"document");js_set(runtime,doc,"body",body);
 }
 char *shim = js_decl_shim(code);
 const char *src = shim ? shim : code;
 jsval_t result=js_eval(runtime,src,strlen(src));
 if(shim)kfree(shim);
 if(js_type(result)==JS_ERR){g_browser.script_errors++;serial("JS: ");serial(js_str(runtime,result));serial("\n");}
 sync_dom();browser_mark_dirty();
}
static int g_ev_x, g_ev_y, g_ev_key;
void js_set_event_pos(int mx,int my){g_ev_x=mx;g_ev_y=my;}
void js_set_event_key(int key){g_ev_key=key;}
/* Build a minimal event object exposed as the global `event` during dispatch. */
static void make_event(DomNode *node,const char *type) {
    if (!runtime) return;
    jsval_t ev = js_mkobj(runtime);
    struct js_root event_root={0};js_root_acquire(runtime,&event_root,&ev);
    js_set(runtime, ev, "type", js_mkstr(runtime, type, strlen(type)));
    js_set(runtime, ev, "clientX", js_mknum(g_ev_x));
    js_set(runtime, ev, "clientY", js_mknum(g_ev_y));
    js_set(runtime, ev, "pageX", js_mknum(g_ev_x));
    js_set(runtime, ev, "pageY", js_mknum(g_ev_y + g_browser.scroll_y));
    js_set(runtime, ev, "keyCode", js_mknum(g_ev_key));
    js_set(runtime, ev, "which", js_mknum(g_ev_key));
    jsval_t target=wrap(node);
    js_set(runtime, ev, "target", target);
    js_set(runtime, ev, "currentTarget", target);
    js_set(runtime, ev, "preventDefault", js_mkundef());
    js_set(runtime, ev, "stopPropagation", js_mkundef());
    js_set(runtime, js_glob(runtime), "event", ev);
    js_root_release(runtime,&event_root);
}
/* Dispatch to the target and its ancestors (bubbling), running JS listeners
 * registered through addEventListener. */
void js_dispatch_event(DomNode *node,const char *type) {
 if(!node||!type)return;
 make_event(node,type);
 for(DomNode *n=node;n;n=n->parent) {
  for(int i=0;i<binding_count;i++)if(bindings[i]==n) {
   char key[48],code[64];snprintf(key,sizeof(key),"_e%d_%s",i,type);
   if(runtime && js_type(get(js_glob(runtime),key))!=JS_UNDEF){snprintf(code,sizeof(code),"%s(event);",key);js_execute(code,g_doc);}
  }
 for(EventListener *e=n->listeners;e;e=e->next)if(str_eq(e->event_type,type))js_execute(e->js_code,g_doc);
 }
}
