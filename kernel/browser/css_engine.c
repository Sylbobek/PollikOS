#include "browser.h"
#ifndef POLLIK_BROWSER_STANDALONE
#include "../mem.h"
#endif

static int hexval(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
static u32 parse_color(const char *val) {
    while (*val == ' ' || *val == '\t')
        val++;

    if (val[0] == 'r' && val[1] == 'g' && val[2] == 'b' && (val[3] == '(' || (val[3]=='a' && val[4]=='('))) {
        const char *p = val + (val[3] == '(' ? 4 : 5);
        int comp[4] = {0, 0, 0, 255}, n = 0;
        while (*p && *p != ')' && n < 4) {
            while (*p == ' ' || *p == ',') p++;
            if (*p < '0' || *p > '9') break;
            int v = 0; while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            while (*p == ' ' || *p == '%') p++;
            comp[n++] = v;
        }
        if (n >= 3) return ((u32)(comp[0] & 255) << 16) | ((u32)(comp[1] & 255) << 8) | (u32)(comp[2] & 255);
    }

    if (*val == '#') {
        val++;
        int len = 0;
        while (val[len]) {
            char c = val[len];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) break;
            len++;
        }
        if (len == 3 || len == 4) {
            u32 c = 0;
            for (int i = 0; i < 3; i++) { int d = hexval(val[i]); if (d < 0) d = 0; c = (c << 8) | (u32)(d * 17); }
            return c;
        } else if (len >= 6) {
            u32 c = 0;
            for (int i = 0; i < 6; i++) { int d = hexval(val[i]); if (d < 0) d = 0; c = (c << 4) | (u32)d; }
            return c;
        }
    }

    /* Named colors (common subset). */
    {
        static const struct { const char *n; u32 c; } names[] = {
            {"red",0xff0000},{"blue",0x2b6cb0},{"green",0x2e7d32},{"white",0xffffff},
            {"black",0x000000},{"gray",0x888888},{"grey",0x888888},{"purple",0x7c4dff},
            {"orange",0xf57c00},{"yellow",0xfbc02d},{"lime",0x00ff00},{"cyan",0x00ffff},
            {"aqua",0x00ffff},{"magenta",0xff00ff},{"fuchsia",0xff00ff},{"silver",0xc0c0c0},
            {"maroon",0x800000},{"navy",0x000080},{"teal",0x008080},{"olive",0x808000},
            {"pink",0xffc0cb},{"gold",0xffd700},{"skyblue",0x87ceeb},{"steelblue",0x4682b4},
            {"tomato",0xff6347},{"crimson",0xdc143c},{"indigo",0x4b0082},{"salmon",0xfa8072},
            {"plum",0xdda0dd},{"beige",0xf5f5dc},{"ivory",0xfffff0},{"khaki",0xf0e68c},
            {"coral",0xff7f50},{"violet",0xee82ee},{"brown",0xa52a2a},{"darkgray",0xa9a9a9},
            {"darkgrey",0xa9a9a9},{"lightgray",0xd3d3d3},{"lightgrey",0xd3d3d3},
            {"transparent",0x000000},{0,0}
        };
        int n = 0; while (val[n] && val[n] != ' ' && val[n] != ';' && val[n] != '}' && n < 15) n++;
        for (int i = 0; names[i].n; i++) {
            const char *nm = names[i].n; int k = 0;
            while (k < n && nm[k] && nm[k] == val[k]) k++;
            if (k == n && nm[k] == 0) return names[i].c;
        }
    }
    return 0x333333;
}

static int eq(const char *a,const char *b) {while(*a && *a==*b){a++;b++;}return *a==*b;}
static int css_viewport_width(void) {
    return g_browser.w > 20 ? g_browser.w - 20 : 660;
}
static int css_viewport_height(void) {
    return g_browser.h > 110 ? g_browser.h - 110 : 300;
}
static int parse_pixel_val(const char *val) {
    while(*val==' ')val++;
    if(eq(val,"auto"))return -1;
    int sign=1,whole=0,frac=0,div=1;
    if(*val=='-'){sign=-1;val++;}
    while(*val>='0'&&*val<='9'){if(whole>10000)return 10000;whole=whole*10+*val++-'0';}
    if(*val=='.') {val++;while(*val>='0'&&*val<='9'){if(div<1000){frac=frac*10+*val-'0';div*=10;}val++;}}
    int v=whole*div+frac,base=1,den=1;
    if(eq(val,"vw")||eq(val,"%")){base=css_viewport_width();den=100;}
    else if(eq(val,"vh")){base=css_viewport_height();den=100;}
    else if(eq(val,"em")||eq(val,"rem")){base=14;}
    return sign*v*base/(div*den);
}
static void spacing(ComputedStyle *s,const char *prop,const char *val,int padding) {
    int v[4]={0},n=0;const char *p=val;
    while(*p && n<4) {while(*p==' ')p++;char token[32]={0};int k=0;while(*p && *p!=' '){if(k<31)token[k++]=*p;p++;}v[n++]=parse_pixel_val(token);}
    if(!n)return;
    int *t=padding?&s->padding_top:&s->margin_top,*r=padding?&s->padding_right:&s->margin_right,*b=padding?&s->padding_bottom:&s->margin_bottom,*l=padding?&s->padding_left:&s->margin_left;
    const char *side=prop+(padding?7:6);
    if(*side=='-') {side++;if(eq(side,"top"))*t=v[0];else if(eq(side,"right"))*r=v[0];else if(eq(side,"bottom"))*b=v[0];else if(eq(side,"left"))*l=v[0];return;}
    *t=v[0];*r=n>1?v[1]:v[0];*b=n>2?v[2]:v[0];*l=n>3?v[3]:*r;
}

void css_apply_property(ComputedStyle *s, const char *prop, const char *val) {
    while (*prop == ' ') prop++;
    while (*val == ' ') val++;

    #define PMATCH(p) (prop[0] == (p)[0] && prop[1] == (p)[1] && prop[2] == (p)[2])

    if (prop[0] == 'c' && prop[1] == 'o' && prop[2] == 'l' && prop[3] == 'o' && prop[4] == 'r') {
        s->color = parse_color(val);
    } else if (prop[0] == 'b' && prop[1] == 'a' && prop[2] == 'c' && prop[3] == 'k') {
        if (eq(val, "none") || eq(val, "transparent")) { s->has_bg_color = 0; }
        else { s->bg_color = parse_color(val); s->has_bg_color = 1; }
    } else if (prop[0] == 'w' && prop[1] == 'i' && prop[2] == 'd' && prop[3] == 't' && prop[4] == 'h') {
        s->width = parse_pixel_val(val);
    } else if (prop[0] == 'h' && prop[1] == 'e' && prop[2] == 'i' && prop[3] == 'g' && prop[4] == 'h' && prop[5] == 't') {
        s->height = parse_pixel_val(val);
    } else if (PMATCH("mar")) {
        spacing(s,prop,val,0);
    } else if (PMATCH("pad")) {
        spacing(s,prop,val,1);
    } else if (PMATCH("bor") && prop[6] == '-') { /* border-radius */
        s->border_radius = parse_pixel_val(val);
    } else if (PMATCH("bor")) { /* border shorthand */
        int bw = parse_pixel_val(val);
        if (bw > 0) s->border_width = bw;
        else if (eq(val, "none")) s->border_width = 0;
        /* pick up an explicit colour token if present */
        const char *q = val;
        while (*q) {
            while (*q == ' ') q++;
            const char *b2 = q;
            while (*q && *q != ' ') q++;
            char tok[24]; int tl = (int)(q - b2); if (tl > 23) tl = 23;
            for (int k = 0; k < tl; k++) tok[k] = b2[k]; tok[tl] = 0;
            if (tok[0] == '#' || (tok[0] >= 'a' && tok[0] <= 'z')) {
                if (!eq(tok,"solid") && !eq(tok,"none") && !eq(tok,"dashed") &&
                    !eq(tok,"dotted") && !eq(tok,"thin") && !eq(tok,"medium") && !eq(tok,"thick")) {
                    u32 c = parse_color(tok);
                    if (c != 0x333333 || eq(tok,"#333333")) s->border_color = c;
                }
            }
        }
        if (s->border_color == 0) s->border_color = 0xd0cce0;
    } else if (PMATCH("dis")) { /* display */
        if (val[0] == 'b') s->display = DISPLAY_BLOCK;
        else if (val[0] == 'i' && val[6] == '-') s->display = DISPLAY_INLINE_BLOCK;
        else if (val[0] == 'i') s->display = DISPLAY_INLINE;
        else if (val[0] == 'f') s->display = DISPLAY_FLEX;
        else if (val[0] == 'n') s->display = DISPLAY_NONE;
    } else if (PMATCH("fon") && prop[5] == 's') { /* font-size */
        int px = parse_pixel_val(val);
        if (px >= 28) s->font_size = 4;
        else if (px >= 20) s->font_size = 3;
        else if (px >= 15) s->font_size = 2;
        else s->font_size = 1;
    } else if (PMATCH("fon") && prop[5] == 'w') { /* font-weight */
        if (val[0] == 'b' || val[0] == '7' || val[0] == '8' || val[0] == '9')
            s->font_weight = 700;
        else
            s->font_weight = 400;
    } else if (PMATCH("tex") && prop[5] == 'a') { /* text-align */
        if (val[0] == 'c') s->text_align = TEXT_ALIGN_CENTER;
        else if (val[0] == 'r') s->text_align = TEXT_ALIGN_RIGHT;
        else s->text_align = TEXT_ALIGN_LEFT;
    } else if(eq(prop,"line-height")) {
        s->line_height=parse_pixel_val(val);
    } else if(eq(prop,"position")) {
        s->position=eq(val,"absolute")?POS_ABSOLUTE:eq(val,"fixed")?POS_FIXED:eq(val,"relative")?POS_RELATIVE:POS_STATIC;
    } else if(eq(prop,"top")) { s->top=parse_pixel_val(val);
    } else if(eq(prop,"left")) { s->left=parse_pixel_val(val);
    } else if(eq(prop,"right")) { s->right=parse_pixel_val(val);
    } else if(eq(prop,"bottom")) { s->bottom=parse_pixel_val(val);
    } else if(eq(prop,"z-index")) { s->z_index=parse_pixel_val(val);
    } else if (PMATCH("gap")) {
        s->gap = parse_pixel_val(val);
    } else if (PMATCH("fle") && prop[5] == 'd') { /* flex-direction */
        if (val[0] == 'c') s->flex_dir = FLEX_DIR_COLUMN;
        else s->flex_dir = FLEX_DIR_ROW;
    } else if (PMATCH("jus")) { /* justify-content */
        if (val[0] == 'c') s->justify = JUSTIFY_CENTER;
        else if (val[0] == 's') s->justify = JUSTIFY_BETWEEN;
        else if (val[0] == 'e') s->justify = JUSTIFY_END;
        else s->justify = JUSTIFY_START;
    } else if (prop[0] == 'a' && prop[1] == 'l' && prop[2] == 'i' && prop[3] == 'g' && prop[4] == 'n' &&
               prop[5] == '-' && prop[6] == 's') { /* align-self */
        if (val[0] == 'a') s->align_self = -1;
        else if (val[0] == 'c') s->align_self = ALIGN_ITEMS_CENTER;
        else if (val[0] == 's' && val[1] == 't' && val[2] == 'r') s->align_self = ALIGN_ITEMS_STRETCH;
        else if (val[0] == 'e') s->align_self = ALIGN_ITEMS_END;
        else s->align_self = ALIGN_ITEMS_START;
    } else if (PMATCH("ali")) { /* align-items */
        if (val[0] == 'c') s->align_items = ALIGN_ITEMS_CENTER;
        else if (val[0] == 's' && val[1] == 't' && val[2] == 'r') s->align_items = ALIGN_ITEMS_STRETCH;
        else if (val[0] == 'e') s->align_items = ALIGN_ITEMS_END;
        else s->align_items = ALIGN_ITEMS_START;
    } else if (prop[0]=='m'&&prop[1]=='i'&&prop[2]=='n'&&prop[3]=='-') { /* min-width/height */
        if (prop[4]=='w') s->min_width = parse_pixel_val(val); else s->min_height = parse_pixel_val(val);
    } else if (prop[0]=='m'&&prop[1]=='a'&&prop[2]=='x'&&prop[3]=='-') { /* max-width/height */
        if (prop[4]=='w') s->max_width = parse_pixel_val(val); else s->max_height = parse_pixel_val(val);
    } else if (prop[0]=='b'&&prop[1]=='o'&&prop[2]=='x'&&prop[3]=='-') { /* box-sizing */
        s->box_border_box = (val[0] == 'b');
    } else if (prop[0]=='b'&&prop[1]=='o'&&prop[2]=='r'&&prop[3]=='d'&&prop[4]=='e'&&prop[5]=='r'&&prop[6]=='-') {
        if (prop[7]=='c') s->border_color = parse_color(val);         /* border-color */
        else if (prop[7]=='r') s->border_radius = parse_pixel_val(val);
        else if (prop[7]=='w') s->border_width = parse_pixel_val(val);
        /* border-style: accepted but widths already parsed from shorthand */
    } else if (prop[0]=='f'&&prop[1]=='l'&&prop[2]=='e'&&prop[3]=='x'&&prop[4]=='-') {
        if (prop[5]=='g') s->flex_grow = parse_pixel_val(val);
        else if (prop[5]=='s') s->flex_shrink = parse_pixel_val(val);
        else if (prop[5]=='b') s->flex_basis = (val[0]=='a') ? -1 : parse_pixel_val(val);
    } else if (prop[0]=='f'&&prop[1]=='l'&&prop[2]=='e'&&prop[3]=='x'&&prop[4]==0) { /* flex shorthand */
        int parts[3] = {0, 1, -1}, n = 0; const char *q = val;
        while (*q && n < 3) {
            while (*q == ' ') q++;
            const char *b2 = q; while (*q && *q != ' ') q++;
            char tok[24]; int tl = (int)(q - b2); if (tl > 23) tl = 23;
            for (int k = 0; k < tl; k++) tok[k] = b2[k]; tok[tl] = 0;
            if (tok[0]=='a'&&tok[1]=='u'&&tok[2]=='t') parts[n] = -1;
            else if (tok[0]=='n') parts[n] = 0;
            else parts[n] = parse_pixel_val(tok);
            n++;
        }
        if (n >= 1) s->flex_grow = parts[0] < 0 ? 1 : parts[0];
        if (n >= 2) s->flex_shrink = parts[1] < 0 ? 1 : parts[1];
        if (n >= 3) s->flex_basis = parts[2];
    } else if (prop[0]=='w'&&prop[1]=='h'&&prop[2]=='i'&&prop[3]=='t'&&prop[4]=='e') { /* white-space */
        s->white_space_pre = (val[0] == 'p');
    } else if (prop[0]=='t'&&prop[1]=='e'&&prop[2]=='x'&&prop[3]=='t'&&prop[4]=='-') { /* text-decoration */
        s->underline = (val[0] == 'u');
    } else if (prop[0]=='v'&&prop[1]=='i'&&prop[2]=='s') { /* visibility */
        s->visible = !(val[0] == 'h');
    } else if (prop[0]=='o'&&prop[1]=='p'&&prop[2]=='a') { /* opacity */
        s->opacity = parse_pixel_val(val) * 256 / 100;
        if (s->opacity < 0) s->opacity = 0;
        if (s->opacity > 256) s->opacity = 256;
    }
}

static void parse_declarations(ComputedStyle *s, const char *decl_str) {
    if (!decl_str || !decl_str[0])
        return;

    const char *p = decl_str;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (!*p || *p == '}')
            break;

        char prop[32]={0};
        int pi = 0;
        while (*p && *p != ':' && *p != ';' && *p != '}' && pi < 31) {
            prop[pi++] = *p++;
        }
        prop[pi] = 0;
        if (*p != ':') {
            if (*p) p++;
            continue;
        }
        p++; /* Skip ':' */

        char val[64]={0};
        int vi = 0;
        while (*p && *p != ';' && *p != '}' && vi < 63) {
            val[vi++] = *p++;
        }
        val[vi] = 0;
        if (*p == ';') p++;

        css_apply_property(s, prop, val);
    }
}

static void apply_user_agent_styles(DomNode *node) {
    const char *t = node->tag;
    if (!memcmp(t,"head",5) || !memcmp(t,"style",6) || !memcmp(t,"script",7) || !memcmp(t,"title",6) || !memcmp(t,"meta",5) || !memcmp(t,"link",5)) {
        node->style.display = DISPLAY_NONE; return;
    }
    if (t[0] == 'h' && t[1] == '1' && t[2] == 0) {
        node->style.font_size = 3;
        node->style.font_weight = 700;
        node->style.margin_top = 14;
        node->style.margin_bottom = 8;
    } else if (t[0] == 'h' && t[1] == '2' && t[2] == 0) {
        node->style.font_size = 2;
        node->style.font_weight = 700;
        node->style.margin_top = 10;
        node->style.margin_bottom = 6;
    } else if (t[0] == 'h' && (t[1] >= '3' && t[1] <= '6') && t[2] == 0) {
        node->style.font_size = 1;
        node->style.font_weight = 700;
        node->style.margin_top = 8;
        node->style.margin_bottom = 4;
    } else if (t[0] == 'p' && t[1] == 0) {
        node->style.margin_top = 6;
        node->style.margin_bottom = 6;
        node->style.line_height = 20;
    } else if (t[0] == 'a' && t[1] == 0) {
        extern int ui_is_dark(void);
        node->style.color = ui_is_dark() ? 0x60a5fa : 0x38488f;
        node->style.display = DISPLAY_INLINE;
    } else if (t[0] == 'b' && t[1] == 'u' && t[2] == 't' && t[3] == 't' && t[4] == 'o' && t[5] == 'n') {
        extern int ui_is_dark(void);
        int dark = ui_is_dark();
        node->style.bg_color = dark ? 0x1e2436 : 0xe9e1f5;
        node->style.has_bg_color = 1;
        node->style.color = dark ? 0xf1f5f9 : 0x513a72;
        node->style.padding_top = node->style.padding_bottom = 6;
        node->style.padding_left = node->style.padding_right = 14;
        node->style.border_radius = 8;
        node->style.display = DISPLAY_INLINE_BLOCK;
    } else if (t[0] == 'i' && t[1] == 'n' && t[2] == 'p' && t[3] == 'u' && t[4] == 't') {
        extern int ui_is_dark(void);
        int dark = ui_is_dark();
        node->style.bg_color = dark ? 0x0e111a : 0xffffff;
        node->style.has_bg_color = 1;
        node->style.color = dark ? 0xf1f5f9 : 0x222222;
        node->style.border_width = 1;
        node->style.border_color = dark ? 0x2b344a : 0xcdc7e0;
        node->style.padding_top = node->style.padding_bottom = 4;
        node->style.padding_left = node->style.padding_right = 8;
        node->style.border_radius = 6;
        node->style.display = DISPLAY_INLINE_BLOCK;
    } else if (t[0] == 'u' && (t[1] == 'l' || t[1] == 'o') && t[2] == 0) {
        node->style.padding_left = 18;
        node->style.margin_top = node->style.margin_bottom = 6;
    } else if (t[0] == 'l' && t[1] == 'i' && t[2] == 0) {
        node->style.margin_top = node->style.margin_bottom = 3;
    } else if (t[0] == 'h' && t[1] == 'r' && t[2] == 0) {
        node->style.border_width = 1;
        node->style.border_color = 0xe5e1f0;
        node->style.margin_top = node->style.margin_bottom = 12;
    } else if (t[0] == 'c' && t[1] == 'e' && t[2] == 'n' && t[3] == 't' && t[4] == 'e' && t[5] == 'r') {
        node->style.display = DISPLAY_BLOCK;
        node->style.text_align = TEXT_ALIGN_CENTER;
    }
}

typedef struct { const char *sel; int slen; const char *decl; int dlen; } CssRule;
static CssRule g_rules[256];
static int g_rule_count;
/* Limit selector work on untrusted pages. CSS parsing and cascade remain
 * useful for typical pages, while pathological selectors cannot monopolize
 * the single-core GUI for minutes. */
static u32 g_selector_work;
#define CSS_SELECTOR_WORK_LIMIT 262144u
#define CSS_SELECTOR_LENGTH_LIMIT 128

static int selector_is_bounded(const char *s, int len) {
    if (len <= 0 || len > CSS_SELECTOR_LENGTH_LIMIT) return 0;
    int chain = 0, in_brackets = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] == '[') in_brackets = 1;
        else if (s[i] == ']') in_brackets = 0;
        else if (!in_brackets && (s[i] == '>' || s[i] == ' ' || s[i] == '\t')) {
            if (++chain > 8) return 0;
        }
    }
    return 1;
}

static void build_rules(const char *css) {
    g_rule_count = 0;
    const char *p = css;
    while (*p && g_rule_count < 256) {
        while (*p && *p <= 32) p++;
        if (p[0]=='/' && p[1]=='*') { p+=2; while (*p && !(p[0]=='*'&&p[1]=='/')) p++; if (*p) p+=2; continue; }
        const char *sel = p; while (*p && *p != '{') p++; if (!*p) break;
        const char *selend = p; p++;
        const char *decl = p; while (*p && *p != '}') p++;
        const char *declend = p; if (*p) p++;
        while (selend > sel && selend[-1] <= 32) selend--;
        while (declend > decl && declend[-1] <= 32) declend--;
        if (selend > sel && declend > decl && selector_is_bounded(sel, (int)(selend-sel))) {
            g_rules[g_rule_count].sel = sel; g_rules[g_rule_count].slen = (int)(selend - sel);
            g_rules[g_rule_count].decl = decl; g_rules[g_rule_count].dlen = (int)(declend - decl);
            g_rule_count++;
        }
    }
}
/* a*100 + b*10 + c (id, class/attr/pseudo, type). */
static int specificity(const char *s, int len) {
    int a = 0, b = 0, c = 0, in_tag = 0;
    for (int i = 0; i < len; i++) {
        char ch = s[i];
        if (ch == '#') { a++; in_tag = 0; }
        else if (ch == '.' || ch == '[' || ch == ':') { b++; in_tag = 0; }
        else if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '-') { if (!in_tag) { c++; in_tag = 1; } }
        else in_tag = 0;
    }
    return a * 100 + b * 10 + c;
}
static int rule_spec_for_node(DomNode *n, const CssRule *r) {
    int best = -1, i = 0;
    while (i < r->slen) {
        while (i < r->slen && (r->sel[i]==' ' || r->sel[i]=='\t' || r->sel[i]==',')) i++;
        int b = i; while (i < r->slen && r->sel[i] != ',') i++; int e = i;
        while (e > b && (r->sel[e-1]==' ' || r->sel[e-1]=='\t')) e--;
        if (e > b && css_match_one(n, r->sel + b, e - b)) {
            int sp = specificity(r->sel + b, e - b);
            if (sp > best) best = sp;
        }
        if (i < r->slen) i++;
    }
    return best;
}

static void apply_css_rules_recursive(DomNode *node) {
    if (!node) return;
    browser_work_checkpoint();

    if (node->parent) {
        node->style.color = node->parent->style.color;
        node->style.font_size = node->parent->style.font_size;
        node->style.line_height = node->parent->style.line_height;
        node->style.text_align = node->parent->style.text_align;
    }
    if (node->type == NODE_ELEMENT)
        apply_user_agent_styles(node);

    if (node->type == NODE_ELEMENT) {
        int idx[64], sp[64], m = 0;
        for (int r = 0; r < g_rule_count && m < 64; r++) {
            if (g_selector_work >= CSS_SELECTOR_WORK_LIMIT) break;
            u32 cost = (u32)(g_rules[r].slen > 0 ? g_rules[r].slen : 1);
            g_selector_work += cost;
            int s = rule_spec_for_node(node, &g_rules[r]);
            if (s >= 0) { idx[m] = r; sp[m] = s; m++; }
        }
        /* Stable insertion sort by specificity (source order preserved). */
        for (int i = 1; i < m; i++) {
            int ki = idx[i], ks = sp[i], j = i - 1;
            while (j >= 0 && sp[j] > ks) { idx[j+1] = idx[j]; sp[j+1] = sp[j]; j--; }
            idx[j+1] = ki; sp[j+1] = ks;
        }
        for (int i = 0; i < m; i++) {
            const CssRule *r = &g_rules[idx[i]];
            if (r->dlen > 0 && r->dlen < 2048) {
                char buf[2048];
                memcpy(buf, r->decl, r->dlen);
                buf[r->dlen] = 0;
                parse_declarations(&node->style, buf);
            }
        }
    }

    /* Inline style attribute overrides author rules. */
    if (node->type == NODE_ELEMENT && node->style_attr[0])
        parse_declarations(&node->style, node->style_attr);

    DomNode *child = node->first_child;
    while (child) {
        apply_css_rules_recursive(child);
        child = child->next_sibling;
    }
}

static void collect_styles(DomNode *n, char *css, int *used) {
    if (!n) return;
    browser_work_checkpoint();
    if (!memcmp(n->tag,"style",6)) for(DomNode *c=n->first_child;c;c=c->next_sibling) {
        if(c->text) for(const char *p=c->text;*p && *used<WEB_MAX_CSS_SIZE-2;p++)css[(*used)++]=*p;
        if (*used < WEB_MAX_CSS_SIZE-1) css[(*used)++]='\n';
    }
    for(DomNode *c=n->first_child;c;c=c->next_sibling)collect_styles(c,css,used);
}
#ifdef POLLIK_BROWSER_UPSTREAM
void css_apply_styles_legacy(DomNode *root, const char *extra_css) {
#else
void css_apply_styles(DomNode *root, const char *extra_css) {
#endif
    if (!root)
        return;
    char *css=kmalloc(WEB_MAX_CSS_SIZE); if(!css)return;
    int used=0; collect_styles(root,css,&used);
    if(extra_css)for(const char *p=extra_css;*p && used<WEB_MAX_CSS_SIZE-1;p++)css[used++]=*p;
    css[used]=0;
    g_selector_work = 0;
    build_rules(css);
    apply_css_rules_recursive(root);
    kfree(css);
}
