#include "browser.h"
#ifndef POLLIK_BROWSER_STANDALONE
#include "../mem.h"
#endif

/* JS wrappers keep stable binding slots for DOM nodes. Standalone parser builds
 * use this weak no-op; the kernel JS engine supplies the strong implementation. */
__attribute__((weak)) void js_dom_node_destroyed(DomNode *node) { (void)node; }
__attribute__((weak)) void browser_media_node_destroyed(DomNode *node) { (void)node; }

static int str_eq_n(const char *a, const char *b);
static void store_attr(DomNode *node, const char *name, const char *val, int overwrite);

DomNode *dom_create_node(NodeType type) {
    browser_work_checkpoint();
    DomNode *node = (DomNode *)kmalloc(sizeof(DomNode));
    if (!node)
        return 0;
    memset(node, 0, sizeof(*node));
    node->type = type;
    node->style.width = -1;
    node->style.height = -1;
    node->style.font_size = 1;
    node->style.font_weight = 400;
    node->style.color = 0x222222;
    node->style.line_height = 18;
    node->style.min_width = node->style.max_width = -1;
    node->style.min_height = node->style.max_height = -1;
    node->style.flex_shrink = 1;
    node->style.flex_basis = -1;
    node->style.align_self = -1;
    node->style.visible = 1;
    return node;
}

DomNode *dom_create_element(const char *tag) {
    DomNode *node = dom_create_node(NODE_ELEMENT);
    if (!node)
        return 0;
    int i = 0;
    while (tag[i] && i < (int)sizeof(node->tag)-1) {
        char c = tag[i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        node->tag[i] = c;
        i++;
    }
    node->tag[i] = 0;

    /* Default display property */
    if (tag[0] == 's' && tag[1] == 'p' && tag[2] == 'a' && tag[3] == 'n')
        node->style.display = DISPLAY_INLINE;
    else if (tag[0] == 'a' && tag[1] == 0)
        node->style.display = DISPLAY_INLINE;
    else if (tag[0] == 's' && tag[1] == 't' && tag[2] == 'r' && tag[3] == 'o' && tag[4] == 'n' && tag[5] == 'g')
        node->style.display = DISPLAY_INLINE;
    else if (tag[0] == 'e' && tag[1] == 'm' && tag[2] == 0)
        node->style.display = DISPLAY_INLINE;
    else if (tag[0] == 'b' && tag[1] == 'u' && tag[2] == 't' && tag[3] == 't' && tag[4] == 'o' && tag[5] == 'n')
        node->style.display = DISPLAY_INLINE_BLOCK;
    else if (tag[0] == 'i' && tag[1] == 'n' && tag[2] == 'p' && tag[3] == 'u' && tag[4] == 't')
        node->style.display = DISPLAY_INLINE_BLOCK;
    else if (tag[0] == 'f' && tag[1] == 'o' && tag[2] == 'r' && tag[3] == 'm') {
        node->style.display = DISPLAY_BLOCK;
        node->method[0] = 'G'; node->method[1] = 'E'; node->method[2] = 'T'; node->method[3] = 0;
    } else if (tag[0] == 'c' && tag[1] == 'a' && tag[2] == 'n' && tag[3] == 'v' && tag[4] == 'a' && tag[5] == 's') {
        node->style.display = DISPLAY_INLINE_BLOCK;
        node->canvas_w = 300; node->canvas_h = 150;
        node->style.width = 300; node->style.height = 150;
    } else
        node->style.display = DISPLAY_BLOCK;

    return node;
}

static int decode_html_entities(const char *src, int src_len, char *dst, int dst_max) {
    int s = 0, d = 0;
    while (s < src_len && d < dst_max - 1) {
        if (!(d & 255)) browser_work_checkpoint();
        if (src[s] == '&') {
            int semi = s + 1;
            while (semi < src_len && semi < s + 12 && src[semi] != ';') semi++;
            if (semi < src_len && src[semi] == ';') {
                int elen = semi - s - 1;
                const char *ent = src + s + 1;
                if (elen == 4 && ent[0]=='n' && ent[1]=='b' && ent[2]=='s' && ent[3]=='p') {
                    dst[d++] = ' '; s = semi + 1; continue;
                } else if (elen == 3 && ent[0]=='a' && ent[1]=='m' && ent[2]=='p') {
                    dst[d++] = '&'; s = semi + 1; continue;
                } else if (elen == 2 && ent[0]=='l' && ent[1]=='t') {
                    dst[d++] = '<'; s = semi + 1; continue;
                } else if (elen == 2 && ent[0]=='g' && ent[1]=='t') {
                    dst[d++] = '>'; s = semi + 1; continue;
                } else if (elen == 4 && ent[0]=='q' && ent[1]=='u' && ent[2]=='o' && ent[3]=='t') {
                    dst[d++] = '"'; s = semi + 1; continue;
                } else if (elen == 4 && ent[0]=='a' && ent[1]=='p' && ent[2]=='o' && ent[3]=='s') {
                    dst[d++] = '\''; s = semi + 1; continue;
                } else if (elen >= 2 && ent[0] == '#') {
                    long num = 0;
                    int valid = 0;
                    if (ent[1] == 'x' || ent[1] == 'X') {
                        for (int k = 2; k < elen; k++) {
                            char c = ent[k];
                            int v = (c >= '0' && c <= '9') ? (c - '0') :
                                    (c >= 'a' && c <= 'f') ? (c - 'a' + 10) :
                                    (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : -1;
                            if (v < 0) break;
                            num = num * 16 + v;
                            valid = 1;
                        }
                    } else {
                        for (int k = 1; k < elen; k++) {
                            if (ent[k] >= '0' && ent[k] <= '9') {
                                num = num * 10 + (ent[k] - '0');
                                valid = 1;
                            } else break;
                        }
                    }
                    if (valid) {
                        if (num == 0 || num == 160) num = ' ';
                        if (num > 0 && num < 32) num = '?';
                        if (num > 0x10FFFF) num = 0xFFFD;
                        /* Encode as UTF-8 so arbitrary Unicode survives parsing even
                         * when the glyph atlas cannot render it yet. */
                        u32 cp = (u32)num;
                        if (cp < 0x80) {
                            if (d < dst_max - 1) dst[d++] = (char)cp;
                        } else if (cp < 0x800) {
                            if (d < dst_max - 2) {
                                dst[d++] = (char)(0xC0 | (cp >> 6));
                                dst[d++] = (char)(0x80 | (cp & 0x3F));
                            }
                        } else if (cp < 0x10000) {
                            if (d < dst_max - 3) {
                                dst[d++] = (char)(0xE0 | (cp >> 12));
                                dst[d++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                dst[d++] = (char)(0x80 | (cp & 0x3F));
                            }
                        } else {
                            if (d < dst_max - 4) {
                                dst[d++] = (char)(0xF0 | (cp >> 18));
                                dst[d++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                                dst[d++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                dst[d++] = (char)(0x80 | (cp & 0x3F));
                            }
                        }
                        s = semi + 1;
                        continue;
                    }
                } else if (elen == 4 && ent[0]=='c' && ent[1]=='o' && ent[2]=='p' && ent[3]=='y') {
                    dst[d++]='('; dst[d++]='c'; dst[d++]=')'; s = semi + 1; continue;
                } else if (elen == 3 && ent[0]=='r' && ent[1]=='e' && ent[2]=='g') {
                    dst[d++]='('; dst[d++]='R'; dst[d++]=')'; s = semi + 1; continue;
                } else if (elen == 6 && !memcmp(ent,"hellip",6)) {
                    dst[d++]='.'; dst[d++]='.'; dst[d++]='.'; s = semi + 1; continue;
                } else if (elen == 5 && !memcmp(ent,"mdash",5)) {
                    dst[d++]='-'; s = semi + 1; continue;
                } else if (elen == 5 && !memcmp(ent,"ndash",5)) {
                    dst[d++]='-'; s = semi + 1; continue;
                } else if (elen == 6 && !memcmp(ent,"middot",6)) {
                    dst[d++]='.'; s = semi + 1; continue;
                } else if (elen == 4 && !memcmp(ent,"euro",4)) {
                    dst[d++]='E'; dst[d++]='U'; dst[d++]='R'; s = semi + 1; continue;
                } else if (elen == 5 && !memcmp(ent,"trade",5)) {
                    dst[d++]='('; dst[d++]='T'; dst[d++]='M'; dst[d++]=')'; s = semi + 1; continue;
                }
            }
        }
        dst[d++] = src[s++];
    }
    dst[d] = '\0';
    return d;
}

DomNode *dom_create_text(const char *text, int len) {
    if (len <= 0)
        return 0;
    DomNode *node = dom_create_node(NODE_TEXT);
    if (!node)
        return 0;
    node->text = (char *)kmalloc((u32)len + 1);
    if (node->text) {
        int dlen = decode_html_entities(text, len, node->text, len + 1);
        node->text[dlen] = 0;
    }
    node->style.display = DISPLAY_INLINE;
    return node;
}

void dom_append_child(DomNode *parent, DomNode *child) {
    if (!parent || !child)
        return;
    child->parent = parent;
    child->next_sibling = 0;
    child->prev_sibling = parent->last_child;
    if (parent->last_child)
        parent->last_child->next_sibling = child;
    else
        parent->first_child = child;
    parent->last_child = child;
}

void dom_remove_child(DomNode *parent, DomNode *child) {
    if (!parent || !child || child->parent != parent)
        return;

    if (child->prev_sibling)
        child->prev_sibling->next_sibling = child->next_sibling;
    else
        parent->first_child = child->next_sibling;

    if (child->next_sibling)
        child->next_sibling->prev_sibling = child->prev_sibling;
    else
        parent->last_child = child->prev_sibling;

    child->parent = 0;
    child->next_sibling = 0;
    child->prev_sibling = 0;
}

void dom_free_tree(DomNode *node) {
    if (!node)
        return;
    js_dom_node_destroyed(node);
    browser_media_node_destroyed(node);
    browser_work_checkpoint();
    DomNode *curr = node->first_child;
    while (curr) {
        DomNode *next = curr->next_sibling;
        dom_free_tree(curr);
        curr = next;
    }
    if (node->text) {
        kfree(node->text);
        node->text = 0;
    }
    kfree(node->image);
#ifdef POLLIK_BROWSER_STANDALONE
    for(int i=0;i<node->attr_count;i++){kfree(node->attr_names[i]);kfree(node->attr_vals[i]);}
#endif
    if (node->canvas) { kfree(node->canvas); node->canvas = 0; }
    EventListener *el = node->listeners;
    while (el) {
        EventListener *next_el = el->next;
        kfree(el);
        el = next_el;
    }
    kfree(node);
}

void dom_insert_before(DomNode *parent, DomNode *child, DomNode *ref) {
    if (!parent || !child) return;
    if (!ref || ref->parent != parent) { dom_append_child(parent, child); return; }
    child->parent = parent;
    child->next_sibling = ref;
    child->prev_sibling = ref->prev_sibling;
    if (ref->prev_sibling) ref->prev_sibling->next_sibling = child;
    else parent->first_child = child;
    ref->prev_sibling = child;
}

const char *dom_get_attribute(DomNode *node, const char *name) {
    if (!node || !name) return 0;
    for (int i = 0; i < node->attr_count; i++)
        if (str_eq_n(node->attr_names[i], name)) return node->attr_vals[i];
    return 0;
}
void dom_set_attribute(DomNode *node, const char *name, const char *val) {
    if (!node || !name) return;
    if(!val)val="";
    store_attr(node, name, val, 1);
    /* Mirror the legacy fast fields so layout/render see the change. */
    #define MIRROR(attribute,field) if(str_eq_n(name,attribute)){unsigned i=0;while(val[i] && i<sizeof(node->field)-1){node->field[i]=val[i];i++;}node->field[i]=0;}
    MIRROR("id",id) else MIRROR("class",class_name) else MIRROR("href",href)
    else MIRROR("src",src) else MIRROR("value",value) else MIRROR("name",name)
    else MIRROR("style",style_attr) else MIRROR("rel",rel) else MIRROR("type",input_type)
    else MIRROR("action",action) else MIRROR("method",method)
    #undef MIRROR
}
void dom_remove_attribute(DomNode *node, const char *name) {
    if (!node || !name) return;
    for (int i = 0; i < node->attr_count; i++) {
        if (!str_eq_n(node->attr_names[i], name)) continue;
#ifdef POLLIK_BROWSER_STANDALONE
        kfree(node->attr_names[i]);kfree(node->attr_vals[i]);
        for(int j=i;j+1<node->attr_count;j++){node->attr_names[j]=node->attr_names[j+1];node->attr_vals[j]=node->attr_vals[j+1];}
        node->attr_names[node->attr_count-1]=0;node->attr_vals[node->attr_count-1]=0;
#else
        for (int j = i; j + 1 < node->attr_count; j++) {
            int k = 0; while (node->attr_names[j+1][k] && k < DOM_ATTR_NAME_MAX) { node->attr_names[j][k]=node->attr_names[j+1][k]; k++; } node->attr_names[j][k]=0;
            k = 0; while (node->attr_vals[j+1][k] && k < DOM_ATTR_VAL_MAX) { node->attr_vals[j][k]=node->attr_vals[j+1][k]; k++; } node->attr_vals[j][k]=0;
        }
#endif
        node->attr_count--;
        /* Clear mirrored fast fields so layout/selectors stop seeing them. */
        if (str_eq_n(name, "id")) node->id[0] = 0;
        else if (str_eq_n(name, "class")) node->class_name[0] = 0;
        else if (str_eq_n(name, "href")) node->href[0] = 0;
        else if (str_eq_n(name, "src")) node->src[0] = 0;
        else if (str_eq_n(name, "value")) node->value[0] = 0;
        else if (str_eq_n(name, "name")) node->name[0] = 0;
        else if (str_eq_n(name, "rel")) node->rel[0] = 0;
        else if (str_eq_n(name, "style")) node->style_attr[0] = 0;
        return;
    }
}
int dom_has_class(DomNode *node, const char *cls) {
    if (!node || !cls) return 0;
    const char *c = node->class_name;
    int cl = (int)strlen(cls);
    while (*c) {
        while (*c == ' ' || *c == '\t' || *c == '\n') c++;
        const char *end = c;
        while (*end && *end != ' ' && *end != '\t' && *end != '\n') end++;
        if (end - c == cl) { int k = 0; while (k < cl && c[k] == cls[k]) k++; if (k == cl) return 1; }
        c = end;
    }
    return 0;
}
DomNode *dom_get_element_by_id(DomNode *root, const char *id) {
    if (!root || !id) return 0;
    DomNode *stack[DOM_MAX_DEPTH * 4];
    int top = 0;
    if (root->type == NODE_ELEMENT && str_eq_n(root->id, id)) return root;
    stack[top++] = root;
    while (top > 0) {
        DomNode *n = stack[--top];
        for (DomNode *c = n->first_child; c; c = c->next_sibling) {
            if (c->type == NODE_ELEMENT && str_eq_n(c->id, id)) return c;
            if (top < DOM_MAX_DEPTH * 4 - 1) stack[top++] = c;
        }
    }
    return 0;
}

/* ---- Minimal CSS selector matching (compound + descendant/child) ---- */
static int sel_tag_matches(const char *tag, const char *name) {
    int k = 0; while (name[k] && tag[k] == name[k]) k++;
    return name[k] == 0 && (tag[k] == 0);
}
/* Match one compound selector (tag / .class / #id / [attr] / [attr=val]). */
static int match_compound(DomNode *n, const char *s, int len) {
    if (!n || n->type != NODE_ELEMENT) return 0;
    int i = 0;
    while (i < len) {
        char c = s[i];
        if (c == '*') { i++; continue; }
        if (c == '.') {
            int b = ++i; while (i < len && s[i] != '.' && s[i] != '#' && s[i] != '[' && s[i] != ':') i++;
            char cls[64]; int cl = i - b; if (cl > 63) cl = 63; for (int k = 0; k < cl; k++) cls[k] = s[b+k]; cls[cl] = 0;
            if (!dom_has_class(n, cls)) return 0;
        } else if (c == '#') {
            int b = ++i; while (i < len && s[i] != '.' && s[i] != '#' && s[i] != '[' && s[i] != ':') i++;
            int il = i - b; char id[32]; if (il > 31) il = 31; for (int k = 0; k < il; k++) id[k] = s[b+k]; id[il] = 0;
            int k = 0; while (id[k] && n->id[k] == id[k]) k++; if (id[k] != 0 || n->id[k] != 0) return 0;
        } else if (c == '[') {
            int b = ++i; while (i < len && s[i] != ']') i++;
            char spec[64]; int sl = i - b; if (sl > 63) sl = 63; for (int k = 0; k < sl; k++) spec[k] = s[b+k]; spec[sl] = 0;
            if (i < len) i++; /* skip ] */
            char *eqp = 0; for (int k = 0; spec[k]; k++) if (spec[k] == '=') { eqp = spec + k; break; }
            if (eqp) { *eqp = 0; const char *av = dom_get_attribute(n, spec); if (!av || !str_eq_n(av, eqp + 1)) return 0; }
            else if (!dom_get_attribute(n, spec) && !dom_has_class(n, spec)) return 0;
        } else if (c == ':') {
            int b = ++i; while (i < len && s[i] != '.' && s[i] != '#' && s[i] != '[' && s[i] != ':') i++;
            char pseudo[16]; int pl = i - b; if (pl > 15) pl = 15; for (int k = 0; k < pl; k++) pseudo[k] = s[b+k]; pseudo[pl] = 0;
            if (str_eq_n(pseudo, "first-child")) {
                DomNode *p = n->parent;
                if (!p) return 0;
                for (DomNode *c = p->first_child; c && c != n; c = c->next_sibling)
                    if (c->type == NODE_ELEMENT) return 0;
            } else if (str_eq_n(pseudo, "last-child")) {
                DomNode *p = n->parent;
                if (!p) return 0;
                for (DomNode *c = n->next_sibling; c; c = c->next_sibling)
                    if (c->type == NODE_ELEMENT) return 0;
            } else if (str_eq_n(pseudo, "focus")) { if (n != g_browser.focused_input) return 0; }
            else if (str_eq_n(pseudo, "hover")) { if (n != g_browser.hover_node) return 0; }
            else if (str_eq_n(pseudo, "active")) { if (n != g_browser.active_node) return 0; }
            /* unknown pseudo-classes simply don't match */
            else return 0;
        } else {
            int b = i; while (i < len && s[i] != '.' && s[i] != '#' && s[i] != '[' && s[i] != ':') i++;
            char tag[16]; int tl = i - b; if (tl > 15) tl = 15; for (int k = 0; k < tl; k++) tag[k] = s[b+k]; tag[tl] = 0;
            if (!sel_tag_matches(n->tag, tag)) return 0;
        }
    }
    return 1;
}
/* Right-to-left match of a complex selector with descendant and child combinators. */
static int match_complex(DomNode *n, const char *s, int len) {
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) len--;
    /* find last combinator at top level */
    int comb = -1, comb_is_child = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (s[i] == ' ') { comb = i; comb_is_child = 0; break; }
        if (s[i] == '>') { comb = i; comb_is_child = 1; break; }
    }
    const char *comp = comb >= 0 ? s + comb + 1 : s;
    int clen = len - (comb >= 0 ? comb + 1 : 0);
    while (clen > 0 && (*comp == ' ' || *comp == '>')) { comp++; clen--; }
    if (!match_compound(n, comp, clen)) return 0;
    if (comb < 0) return 1;
    int plen = comb;
    while (plen > 0 && (s[plen-1] == ' ' || s[plen-1] == '>')) plen--;
    if (plen <= 0) return 1; /* trailing combinator, ignore */
    if (comb_is_child) return n->parent ? match_complex(n->parent, s, plen) : 0;
    for (DomNode *a = n->parent; a; a = a->parent)
        if (match_complex(a, s, plen)) return 1;
    return 0;
}
int css_match_one(DomNode *n, const char *sel, int len) {
    return match_complex(n, sel, len);
}
static int match_selector_list(DomNode *n, const char *sel) {    const char *p = sel;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        const char *start = p;
        while (*p && *p != ',') p++;
        const char *end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if (end > start && match_complex(n, start, (int)(end - start))) return 1;
    }
    return 0;
}
static void query_collect(DomNode *n, const char *sel, DomNode **out, int max, int *count) {
    if (!n) return;
    for (DomNode *c = n->first_child; c; c = c->next_sibling) {
        if (*count >= max) return;
        if (c->type == NODE_ELEMENT && match_selector_list(c, sel)) out[(*count)++] = c;
        query_collect(c, sel, out, max, count);
    }
}
DomNode *dom_query_all(DomNode *root, const char *selector, DomNode **out, int max, int *count) {
    if (count) *count = 0;
    if (!root || !selector) return 0;
    query_collect(root, selector, out, max, count);
    return (count && *count) ? out[0] : 0;
}
DomNode *dom_query_selector(DomNode *root, const char *selector) {
    DomNode *out[1]; int count = 0;
    dom_query_all(root, selector, out, 1, &count);
    return count ? out[0] : 0;
}
void dom_set_inner_html(DomNode *node, const char *html, int len) {
    if (!node) return;
    DomNode *old = node->first_child;
    while (old) { DomNode *next = old->next_sibling; dom_remove_child(node, old); dom_free_tree(old); old = next; }
    node->first_child = node->last_child = 0;
    if (!html || len <= 0) return;
    DomNode *frag = html_parse(html, len);
    if (!frag) return;
#ifdef POLLIK_BROWSER_UPSTREAM
    DomNode *body=dom_query_selector(frag,"body");
    DomNode *c = body?body->first_child:frag->first_child;
#else
    DomNode *c = frag->first_child;
#endif
    while (c) {
        DomNode *next = c->next_sibling;
        dom_remove_child(c->parent, c);
        dom_append_child(node, c);
        c = next;
    }
    dom_free_tree(frag);
}

/* Serialize a node's children back to HTML (for element.innerHTML). */
static void serialize_escape(const char *s, char *out, int cap, int *n, int attr) {
    for (; s && *s; s++) {
        char c = *s;
        const char *rep = 0;
        if (c == '&') rep = "&amp;";
        else if (c == '<') rep = "&lt;";
        else if (c == '>') rep = "&gt;";
        else if (attr && c == '"') rep = "&quot;";
        if (rep) { for (const char *r = rep; *r && *n < cap - 1; r++) out[(*n)++] = *r; }
        else if (*n < cap - 1) out[(*n)++] = c;
    }
}
static void serialize_node(DomNode *n, char *out, int cap, int *used) {
    for (DomNode *c = n->first_child; c; c = c->next_sibling) {
        if (*used >= cap - 1) return;
        if (c->type == NODE_TEXT) { serialize_escape(c->text, out, cap, used, 0); continue; }
        if (c->type != NODE_ELEMENT) continue;
        out[(*used)++] = '<';
        for (const char *t = c->tag; *t && *used < cap - 1; t++) out[(*used)++] = *t;
        for (int i = 0; i < c->attr_count; i++) {
            if (*used >= cap - 1) break;
            out[(*used)++] = ' ';
            for (const char *a = c->attr_names[i]; *a && *used < cap - 1; a++) out[(*used)++] = *a;
            if (*used < cap - 1) out[(*used)++] = '=';
            if (*used < cap - 1) out[(*used)++] = '"';
            serialize_escape(c->attr_vals[i], out, cap, used, 1);
            if (*used < cap - 1) out[(*used)++] = '"';
        }
        if (*used < cap - 1) out[(*used)++] = '>';
        serialize_node(c, out, cap, used);
        if (*used < cap - 1) out[(*used)++] = '<';
        if (*used < cap - 1) out[(*used)++] = '/';
        for (const char *t = c->tag; *t && *used < cap - 1; t++) out[(*used)++] = *t;
        if (*used < cap - 1) out[(*used)++] = '>';
    }
}
void dom_serialize_inner(DomNode *node, char *out, int cap) {
    if (!out || cap <= 0) return;
    int used = 0;
    out[0] = 0;
    if (node) serialize_node(node, out, cap, &used);
    out[used < cap ? used : cap - 1] = 0;
}

static int is_void_tag(const char *tag) {
    static const char *voids[] = {"area","base","br","col","embed","hr","img","input",
                                  "link","meta","param","source","track","wbr",0};
    for (int i = 0; voids[i]; i++) {
        const char *v = voids[i]; int k = 0;
        while (v[k] && v[k] == tag[k]) k++;
        if (v[k] == 0 && tag[k] == 0) return 1;
    }
    return 0;
}
/* Elements that implicitly close an open element of the same/other group. */
static int causes_implicit_close(const char *open, const char *closing) {
    /* `closing` opening tag closes an open `open` tag. */
    if (open[0] == 'p' && open[1] == 0) {
        static const char *block[] = {"div","p","ul","ol","li","table","tr","td","th",
            "h1","h2","h3","h4","h5","h6","section","article","aside","header","footer",
            "nav","main","form","pre","blockquote","hr",0};
        for (int i = 0; block[i]; i++) { const char *b = block[i]; int k=0; while(b[k]&&b[k]==closing[k])k++; if(b[k]==0&&closing[k]==0) return 1; }
        return 0;
    }
    if (open[0] == 'l' && open[1] == 'i' && open[2] == 0) return closing[0]=='l'&&closing[1]=='i'&&closing[2]==0;
    if (open[0] == 'o' && open[1] == 'p' && open[2] == 't' && open[3] == 'i') return closing[0]=='o'&&closing[1]=='p';
    if (open[0]=='t'&&open[1]=='r'&&open[2]==0) return (closing[0]=='t'&&closing[1]=='r')||(closing[0]=='t'&&(closing[1]=='d'||closing[1]=='h'));
    if (open[0]=='t'&&(open[1]=='d'||open[1]=='h')) return (closing[0]=='t'&&(closing[1]=='d'||closing[1]=='h'))||(closing[0]=='t'&&closing[1]=='r');
    return 0;
}

int str_eq_n(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void store_attr(DomNode *node, const char *name, const char *val, int overwrite) {
    if (!name[0]) return;
#ifdef POLLIK_BROWSER_STANDALONE
    size_t nl=strlen(name),vl=strlen(val);
    if(nl>=DOM_ATTR_NAME_MAX || vl>=DOM_ATTR_VAL_MAX)return;
    int index=node->attr_count;
    for(int i=0;i<node->attr_count;i++)if(str_eq_n(node->attr_names[i],name)){if(!overwrite)return;index=i;break;}
    if(index==DOM_MAX_ATTRS)return;
    char *n=kmalloc((u32)nl+1),*v=kmalloc((u32)vl+1);
    if(!n || !v){kfree(n);kfree(v);return;}
    memcpy(n,name,nl+1);
    if(overwrite)memcpy(v,val,vl+1);else decode_html_entities(val,(int)vl,v,(int)vl+1);
    if(index<node->attr_count){kfree(node->attr_names[index]);kfree(node->attr_vals[index]);}
    else ++node->attr_count;
    node->attr_names[index]=n;node->attr_vals[index]=v;
#else
    for (int i = 0; i < node->attr_count; i++)
        if (str_eq_n(node->attr_names[i], name)) {
            if (!overwrite) return; /* first wins when parsing */
            char decoded[DOM_ATTR_VAL_MAX];
            decode_html_entities(val, (int)strlen(val), decoded, sizeof(decoded));
            int m = 0; while (decoded[m] && m < DOM_ATTR_VAL_MAX - 1) { node->attr_vals[i][m] = decoded[m]; m++; }
            node->attr_vals[i][m] = 0;
            return;
        }
    if (node->attr_count >= DOM_MAX_ATTRS) return;
    int i = node->attr_count++;
    int n = 0; while (name[n] && n < DOM_ATTR_NAME_MAX - 1) { node->attr_names[i][n] = name[n]; n++; }
    node->attr_names[i][n] = 0;
    char decoded[DOM_ATTR_VAL_MAX];
    decode_html_entities(val, (int)strlen(val), decoded, sizeof(decoded));
    int m = 0; while (decoded[m] && m < DOM_ATTR_VAL_MAX - 1) { node->attr_vals[i][m] = decoded[m]; m++; }
    node->attr_vals[i][m] = 0;
#endif
}

static void parse_attribute(DomNode *node, const char *attr_name, const char *attr_val) {
    store_attr(node, attr_name, attr_val, 0);
    #define MATCH(n) (attr_name[0] == (n)[0] && attr_name[1] == (n)[1])
    if (attr_name[0] == 'i' && attr_name[1] == 'd' && attr_name[2] == 0) {
        int i = 0;
        while (attr_val[i] && i < 31) {
            node->id[i] = attr_val[i];
            i++;
        }
        node->id[i] = 0;
    } else if (attr_name[0] == 'c' && attr_name[1] == 'l' && attr_name[2] == 'a' && attr_name[3] == 's' && attr_name[4] == 's' && attr_name[5] == 0) {
        int i = 0;
        while (attr_val[i] && i < 63) {
            node->class_name[i] = attr_val[i];
            i++;
        }
        node->class_name[i] = 0;
    } else if (attr_name[0] == 'h' && attr_name[1] == 'r' && attr_name[2] == 'e' && attr_name[3] == 'f' && attr_name[4] == 0) {
        int i = 0;
        while (attr_val[i] && i < 127) {
            node->href[i] = attr_val[i];
            i++;
        }
        node->href[i] = 0;
    } else if (attr_name[0] == 's' && attr_name[1] == 'r' && attr_name[2] == 'c' && attr_name[3] == 0) {
        int i = 0;
        while (attr_val[i] && i < 127) {
            node->src[i] = attr_val[i];
            i++;
        }
        node->src[i] = 0;
    } else if (node->tag[0] == 'c' && node->tag[1] == 'a' &&
               attr_name[0] == 'w' && attr_name[1] == 'i' && attr_name[2] == 'd') {
        int v = 0, i = 0;
        while (attr_val[i] >= '0' && attr_val[i] <= '9') { v = v * 10 + (attr_val[i] - '0'); i++; }
        if (v > 0) { node->canvas_w = v > 2048 ? 2048 : v; node->style.width = node->canvas_w; }
    } else if (node->tag[0] == 'c' && node->tag[1] == 'a' &&
               attr_name[0] == 'h' && attr_name[1] == 'e' && attr_name[2] == 'i') {
        int v = 0, i = 0;
        while (attr_val[i] >= '0' && attr_val[i] <= '9') { v = v * 10 + (attr_val[i] - '0'); i++; }
        if (v > 0) { node->canvas_h = v > 2048 ? 2048 : v; node->style.height = node->canvas_h; }
    } else if (attr_name[0] == 'r' && attr_name[1] == 'e' && attr_name[2] == 'l' && attr_name[3] == 0) {
        int i = 0;
        while (attr_val[i] && i < 15) {
            char c = attr_val[i];
            if (c >= 'A' && c <= 'Z') c += 32;
            node->rel[i] = c;
            i++;
        }
        node->rel[i] = 0;
    } else if (attr_name[0] == 's' && attr_name[1] == 't' && attr_name[2] == 'y' && attr_name[3] == 'l' && attr_name[4] == 'e' && attr_name[5] == 0) {
        int i = 0;
        while (attr_val[i] && i < 127) {
            node->style_attr[i] = attr_val[i];
            i++;
        }
        node->style_attr[i] = 0;
    } else if (attr_name[0] == 'v' && attr_name[1] == 'a' && attr_name[2] == 'l' && attr_name[3] == 'u' && attr_name[4] == 'e' && attr_name[5] == 0) {
        int dlen = decode_html_entities(attr_val, strlen(attr_val), node->value, sizeof(node->value));
        node->value[dlen] = 0;
    } else if (attr_name[0] == 't' && attr_name[1] == 'y' && attr_name[2] == 'p' && attr_name[3] == 'e' && attr_name[4] == 0) {
        int i = 0;
        while (attr_val[i] && i < 15) {
            char c = attr_val[i];
            if (c >= 'A' && c <= 'Z') c += 32;
            node->input_type[i] = c;
            i++;
        }
        node->input_type[i] = 0;
        if (node->input_type[0] == 'h' && node->input_type[1] == 'i' && node->input_type[2] == 'd' &&
            node->input_type[3] == 'd' && node->input_type[4] == 'e' && node->input_type[5] == 'n') {
            node->style.display = DISPLAY_NONE;
        }
    } else if (attr_name[0] == 'n' && attr_name[1] == 'a' && attr_name[2] == 'm' && attr_name[3] == 'e' && attr_name[4] == 0) {
        int i = 0;
        while (attr_val[i] && i < 31) {
            node->name[i] = attr_val[i];
            i++;
        }
        node->name[i] = 0;
    } else if (attr_name[0] == 'a' && attr_name[1] == 'c' && attr_name[2] == 't' && attr_name[3] == 'i' && attr_name[4] == 'o' && attr_name[5] == 'n' && attr_name[6] == 0) {
        int i = 0;
        while (attr_val[i] && i < 127) {
            node->action[i] = attr_val[i];
            i++;
        }
        node->action[i] = 0;
    } else if (attr_name[0] == 'm' && attr_name[1] == 'e' && attr_name[2] == 't' && attr_name[3] == 'h' && attr_name[4] == 'o' && attr_name[5] == 'd' && attr_name[6] == 0) {
        int i = 0;
        while (attr_val[i] && i < 7) {
            char c = attr_val[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            node->method[i] = c;
            i++;
        }
        node->method[i] = 0;
    }
}

#ifdef POLLIK_BROWSER_UPSTREAM
DomNode *html_parse_legacy(const char *html, int len) {
#else
DomNode *html_parse(const char *html, int len) {
#endif
    if (!html || len <= 0)
        return 0;

    DomNode *root = dom_create_node(NODE_DOCUMENT);
    if (!root)
        return 0;

    DomNode *stack[DOM_MAX_DEPTH];
    int stack_top = 0;
    stack[0] = root;
    int node_count = 0;

    int pos = 0;
    while (pos < len) {
        browser_work_checkpoint();
        /* Stop consuming untrusted markup once the DOM budget is exhausted. */
        if (node_count >= DOM_MAX_NODES)
            break;
        if (html[pos] == '<') {
            pos++;
            if (pos >= len)
                break;

            /* HTML Comment <!-- ... --> */
            if (pos + 2 < len && html[pos] == '!' && html[pos + 1] == '-' && html[pos + 2] == '-') {
                pos += 3;
                while (pos + 2 < len && !(html[pos] == '-' && html[pos + 1] == '-' && html[pos + 2] == '>')) {
                    if (!(pos & 255)) browser_work_checkpoint();
                    pos++;
                }
                pos += 3;
                continue;
            }

            /* DOCTYPE <!DOCTYPE ...> */
            if (html[pos] == '!') {
                while (pos < len && html[pos] != '>') {
                    if (!(pos & 255)) browser_work_checkpoint();
                    pos++;
                }
                if (pos < len && html[pos] == '>')
                    pos++;
                continue;
            }

            /* Closing tag </tag> */
            if (html[pos] == '/') {
                pos++;
                char close_tag[16];
                int cti = 0;
                while (pos < len && html[pos] != '>' && html[pos] != ' ' && cti < 15) {
                    char c = html[pos++];
                    if (c >= 'A' && c <= 'Z')
                        c += 32;
                    close_tag[cti++] = c;
                }
                close_tag[cti] = 0;
                while (pos < len && html[pos] != '>') {
                    if (!(pos & 255)) browser_work_checkpoint();
                    pos++;
                }
                if (pos < len && html[pos] == '>')
                    pos++;

                /* Pop stack until matching tag */
                for (int s = stack_top; s > 0; s--) {
                    int match = 1;
                    for (int k = 0; close_tag[k] || stack[s]->tag[k]; k++) {
                        if (close_tag[k] != stack[s]->tag[k]) {
                            match = 0;
                            break;
                        }
                    }
                    if (match) {
                        stack_top = s - 1;
                        break;
                    }
                }
                continue;
            }

            /* Opening tag <tag attr="val"> */
            char tag[16];
            int ti = 0;
            while (pos < len && html[pos] != '>' && html[pos] != ' ' && html[pos] != '/' && ti < 15) {
                char c = html[pos++];
                if (c >= 'A' && c <= 'Z')
                    c += 32;
                tag[ti++] = c;
            }
            tag[ti] = 0;

            DomNode *elem = dom_create_element(tag);
            if (!elem)
                break;
            node_count++;

            /* Parse attributes */
            while (pos < len && html[pos] != '>' && html[pos] != '/') {
                browser_work_checkpoint();
                while (pos < len && (html[pos] == ' ' || html[pos] == '\t' || html[pos] == '\r' || html[pos] == '\n')) {
                    if (!(pos & 255)) browser_work_checkpoint();
                    pos++;
                }
                if (pos >= len || html[pos] == '>' || html[pos] == '/')
                    break;

                char attr_name[32];
                int ai = 0;
                while (pos < len && html[pos] != '=' && html[pos] != ' ' && html[pos] != '>' && html[pos] != '/' && ai < 31) {
                    char c = html[pos++];
                    if (c >= 'A' && c <= 'Z')
                        c += 32;
                    attr_name[ai++] = c;
                }
                attr_name[ai] = 0;

                while (pos < len && (html[pos] == ' ' || html[pos] == '\t'))
                    pos++;

                char attr_val[128] = {0};
                if (pos < len && html[pos] == '=') {
                    pos++;
                    while (pos < len && (html[pos] == ' ' || html[pos] == '\t'))
                        pos++;
                    char quote = 0;
                    if (pos < len && (html[pos] == '"' || html[pos] == '\''))
                        quote = html[pos++];
                    int vi = 0;
                    while (pos < len && vi < 127) {
                        if (quote && html[pos] == quote) {
                            pos++;
                            break;
                        }
                        if (!quote && (html[pos] == ' ' || html[pos] == '>'))
                            break;
                        attr_val[vi++] = html[pos++];
                    }
                    attr_val[vi] = 0;
                }

                parse_attribute(elem, attr_name, attr_val);
            }

            int self_closing = 0;
            if (pos < len && html[pos] == '/') {
                self_closing = 1;
                pos++;
            }
            if (pos < len && html[pos] == '>')
                pos++;

            /* Append element to current top of stack */
            while (stack_top > 0 && causes_implicit_close(stack[stack_top]->tag, tag))
                stack_top--;
            dom_append_child(stack[stack_top], elem);

            /* Special case: <script> ... </script> */
            if (tag[0] == 's' && tag[1] == 'c' && tag[2] == 'r' && tag[3] == 'i' && tag[4] == 'p' && tag[5] == 't') {
                int script_start = pos;
                while (pos + 8 < len && !(html[pos] == '<' && html[pos + 1] == '/' && html[pos + 2] == 's' &&
                                          html[pos + 3] == 'c' && html[pos + 4] == 'r' && html[pos + 5] == 'i' &&
                                          html[pos + 6] == 'p' && html[pos + 7] == 't' && html[pos + 8] == '>')) {
                    if (!(pos & 255)) browser_work_checkpoint();
                    pos++;
                }
                int script_len = pos - script_start;
                if (script_len > 0) {
                    DomNode *txt = dom_create_text(html + script_start, script_len);
                    if (txt)
                        dom_append_child(elem, txt);
                }
                if (pos + 8 < len)
                    pos += 9;
                continue;
            }

            /* Special case: <style> ... </style> */
            if (tag[0] == 's' && tag[1] == 't' && tag[2] == 'y' && tag[3] == 'l' && tag[4] == 'e') {
                int style_start = pos;
                while (pos + 7 < len && !(html[pos] == '<' && html[pos + 1] == '/' && html[pos + 2] == 's' &&
                                         html[pos + 3] == 't' && html[pos + 4] == 'y' && html[pos + 5] == 'l' &&
                                         html[pos + 6] == 'e' && html[pos + 7] == '>')) {
                    if (!(pos & 255)) browser_work_checkpoint();
                    pos++;
                }
                int style_len = pos - style_start;
                if (style_len > 0) {
                    DomNode *txt = dom_create_text(html + style_start, style_len);
                    if (txt)
                        dom_append_child(elem, txt);
                }
                if (pos + 7 < len)
                    pos += 8;
                continue;
            }

            /* Special case: raw text <textarea>/<title> */
            if (tag[0] == 't' && tag[1] == 'e' && tag[2] == 'x' && tag[3] == 't' && tag[4] == 'a') {
                static const char et[] = "</textarea>";
                int el = 11, start = pos;
                while (pos + el <= len) { int m = 1; for (int k = 0; k < el; k++) if (html[pos+k] != et[k]) { m = 0; break; } if (m) break; pos++; }
                if (pos > start) { DomNode *txt = dom_create_text(html + start, pos - start); if (txt) dom_append_child(elem, txt); }
                pos += el;
                if (!self_closing && stack_top < DOM_MAX_DEPTH - 1) stack[++stack_top] = elem;
                continue;
            }
            if (tag[0] == 't' && tag[1] == 'i' && tag[2] == 't' && tag[3] == 'l' && tag[4] == 'e') {
                static const char et[] = "</title>";
                int el = 8, start = pos;
                while (pos + el <= len) { int m = 1; for (int k = 0; k < el; k++) if (html[pos+k] != et[k]) { m = 0; break; } if (m) break; pos++; }
                if (pos > start) { DomNode *txt = dom_create_text(html + start, pos - start); if (txt) dom_append_child(elem, txt); }
                pos += el;
                if (!self_closing && stack_top < DOM_MAX_DEPTH - 1) stack[++stack_top] = elem;
                continue;
            }

/* Push to stack if not void and not self-closing. Deeply nested
             * markup beyond DOM_MAX_DEPTH is flattened, not overflowed. */
            if (!self_closing && !is_void_tag(tag) && stack_top < DOM_MAX_DEPTH - 1) {
                stack[++stack_top] = elem;
            } else if (!self_closing && !is_void_tag(tag) && stack_top > 0) {
                stack_top--; /* drop one level to make room, keep the element */
                if (stack_top < DOM_MAX_DEPTH - 1) stack[++stack_top] = elem;
            }
        } else {
            /* Text content */
            int text_start = pos;
            while (pos < len && html[pos] != '<') {
                if (!(pos & 255)) browser_work_checkpoint();
                pos++;
            }
            int text_len = pos - text_start;

            /* Check if text contains non-whitespace */
            int has_content = 0, has_ws = 0;
            for (int k = 0; k < text_len; k++) {
                if (!(k & 255)) browser_work_checkpoint();
                char c = html[text_start + k];
                if (c > 32) has_content = 1;
                else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') has_ws = 1;
            }

if (has_content) {
                if (node_count < DOM_MAX_NODES) {
                    DomNode *txt = dom_create_text(html + text_start, text_len);
                    if (txt) { dom_append_child(stack[stack_top], txt); node_count++; }
                }
            } else if (has_ws && stack_top > 0) {
                /* Preserve a single collapsing space between inline content. */
                DomNode *prev = stack[stack_top]->last_child;
                int inline_prev = prev && (prev->type == NODE_TEXT ||
                    prev->style.display == DISPLAY_INLINE || prev->style.display == DISPLAY_INLINE_BLOCK);
                if (inline_prev) {
                    DomNode *txt = dom_create_text(" ", 1);
                    if (txt) dom_append_child(stack[stack_top], txt);
                }
            }
        }
    }

    return root;
}
