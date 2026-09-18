#include "browser.h"

static u32 parse_color(const char *val) {
    while (*val == ' ')
        val++;

    if (*val == '#') {
        val++;
        u32 c = 0;
        int len = 0;
        while (val[len] && val[len] != ';' && val[len] != ' ' && val[len] != '}')
            len++;
        if (len == 3) {
            /* #RGB -> #RRGGBB */
            for (int i = 0; i < 3; i++) {
                int d = 0;
                char ch = val[i];
                if (ch >= '0' && ch <= '9') d = ch - '0';
                else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
                c = (c << 8) | (u32)(d * 17);
            }
            return c;
        } else if (len >= 6) {
            for (int i = 0; i < 6; i++) {
                int d = 0;
                char ch = val[i];
                if (ch >= '0' && ch <= '9') d = ch - '0';
                else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
                c = (c << 4) | (u32)d;
            }
            return c;
        }
    }

    /* Named colors */
    #define CMATCH(s) (val[0] == (s)[0] && val[1] == (s)[1])
    if (CMATCH("re") && val[2] == 'd') return 0xe03838;
    if (CMATCH("bl") && val[2] == 'u' && val[3] == 'e') return 0x2b6cb0;
    if (CMATCH("gr") && val[2] == 'e' && val[3] == 'e') return 0x2e7d32;
    if (CMATCH("wh") && val[2] == 'i') return 0xffffff;
    if (CMATCH("bl") && val[2] == 'a') return 0x000000;
    if (CMATCH("gr") && val[2] == 'a') return 0x888888;
    if (CMATCH("pu") && val[2] == 'r') return 0x7c4dff;
    if (CMATCH("or") && val[2] == 'a') return 0xf57c00;
    if (CMATCH("ye") && val[2] == 'l') return 0xfbc02d;

    return 0x333333;
}

static int eq(const char *a,const char *b) {while(*a && *a==*b){a++;b++;}return *a==*b;}
static int parse_pixel_val(const char *val) {
    while(*val==' ')val++;
    if(eq(val,"auto"))return -1;
    int sign=1,whole=0,frac=0,div=1;
    if(*val=='-'){sign=-1;val++;}
    while(*val>='0'&&*val<='9'){if(whole>10000)return 10000;whole=whole*10+*val++-'0';}
    if(*val=='.') {val++;while(*val>='0'&&*val<='9'){if(div<1000){frac=frac*10+*val-'0';div*=10;}val++;}}
    int v=whole*div+frac,base=1,den=1;
    if(eq(val,"vw")||eq(val,"%")){base=660;den=100;}
    else if(eq(val,"vh")){base=300;den=100;}
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

static void apply_declaration(ComputedStyle *s, const char *prop, const char *val) {
    while (*prop == ' ') prop++;
    while (*val == ' ') val++;

    #define PMATCH(p) (prop[0] == (p)[0] && prop[1] == (p)[1] && prop[2] == (p)[2])

    if (prop[0] == 'c' && prop[1] == 'o' && prop[2] == 'l' && prop[3] == 'o' && prop[4] == 'r') {
        s->color = parse_color(val);
    } else if (prop[0] == 'b' && prop[1] == 'a' && prop[2] == 'c' && prop[3] == 'k') {
        s->bg_color = parse_color(val);
        s->has_bg_color = 1;
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
    } else if (PMATCH("bor")) { /* border */
        s->border_width = parse_pixel_val(val);
        s->border_color = 0xd0cce0;
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
    } else if (PMATCH("ali")) { /* align-items */
        if (val[0] == 'c') s->align_items = ALIGN_ITEMS_CENTER;
        else if (val[0] == 's' && val[1] == 't' && val[2] == 'r') s->align_items = ALIGN_ITEMS_STRETCH;
        else if (val[0] == 'e') s->align_items = ALIGN_ITEMS_END;
        else s->align_items = ALIGN_ITEMS_START;
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

        apply_declaration(s, prop, val);
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
        node->style.color = 0x38488f;
        node->style.display = DISPLAY_INLINE;
    } else if (t[0] == 'b' && t[1] == 'u' && t[2] == 't' && t[3] == 't' && t[4] == 'o' && t[5] == 'n') {
        node->style.bg_color = 0xe9e1f5;
        node->style.has_bg_color = 1;
        node->style.color = 0x513a72;
        node->style.padding_top = node->style.padding_bottom = 6;
        node->style.padding_left = node->style.padding_right = 14;
        node->style.border_radius = 8;
        node->style.display = DISPLAY_INLINE_BLOCK;
    } else if (t[0] == 'i' && t[1] == 'n' && t[2] == 'p' && t[3] == 'u' && t[4] == 't') {
        node->style.bg_color = 0xffffff;
        node->style.has_bg_color = 1;
        node->style.color = 0x222222;
        node->style.border_width = 1;
        node->style.border_color = 0xcdc7e0;
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
    }
}

static void apply_css_rules_recursive(DomNode *node, const char *css) {
    if (!node)
        return;
    browser_work_checkpoint();

    if (node->parent) {
        node->style.color = node->parent->style.color;
        node->style.font_size = node->parent->style.font_size;
        node->style.line_height = node->parent->style.line_height;
        node->style.text_align = node->parent->style.text_align;
    }
    /* 1. Apply user agent styles */
    if (node->type == NODE_ELEMENT)
        apply_user_agent_styles(node);

    /* 2. Inherit from parent */
    if (node->parent) {
        if (!node->style.color)
            node->style.color = node->parent->style.color;
        if (node->style.font_size == 1 && node->parent->style.font_size > 1 && node->type == NODE_TEXT)
            node->style.font_size = node->parent->style.font_size;
    }

    /* Apply simple selectors in increasing specificity, source order within each rank. */
    if(css && node->type==NODE_ELEMENT) for(int rank=0;rank<3;rank++) {
        const char *p=css;
        while(*p) {
            browser_work_checkpoint();
            while(*p && *p<=32)p++;
            if(p[0]=='/'&&p[1]=='*'){p+=2;while(*p && !(p[0]=='*'&&p[1]=='/'))p++;if(*p)p+=2;continue;}
            const char *selectors=p;while(*p && *p!='{')p++;if(!*p)break;
            const char *selend=p++;const char *decl=p;while(*p && *p!='}')p++;
            int dl=p-decl;if(*p)p++;
            for(const char *q=selectors;q<selend;) {
                while(q<selend&&*q<=32)q++;const char *e=q;while(e<selend&&*e!=',')e++;
                const char *tail=e;while(tail>q&&tail[-1]<=32)tail--;
                int n=tail-q,match=0,r=(*q=='#'?2:*q=='.'?1:0);
                if(r==rank && n>0) {
                    if(r==2)match=n-1==(int)strlen(node->id)&&!memcmp(q+1,node->id,n-1);
                    else if(r==1) {
                        const char *c=node->class_name;
                        while(*c) {while(*c==' ')c++;const char *end=c;while(*end&&*end!=' ')end++;
                            if(end-c==n-1&&!memcmp(c,q+1,n-1)){match=1;break;}c=end;}
                    } else match=(n==1&&*q=='*')||(n==(int)strlen(node->tag)&&!memcmp(q,node->tag,n));
                }
                if(match && dl>0 && dl<2048) {
                    char buf[2048];memcpy(buf,decl,dl);buf[dl]=0;parse_declarations(&node->style,buf);
                }
                q=e<selend?e+1:selend;
            }
        }
    }

    /* 4. Apply inline style attribute (highest specificity) */
    if (node->style_attr[0]) {
        parse_declarations(&node->style, node->style_attr);
    }

    /* Recurse for children */
    DomNode *child = node->first_child;
    while (child) {
        apply_css_rules_recursive(child, css);
        child = child->next_sibling;
    }
}

static void collect_styles(DomNode *n, char *css, int *used) {
    if (!n) return;
    browser_work_checkpoint();
    if (!memcmp(n->tag,"style",6)) for(DomNode *c=n->first_child;c;c=c->next_sibling) {
        if(c->text) for(const char *p=c->text;*p && *used<16382;p++)css[(*used)++]=*p;
        if (*used < 16383) css[(*used)++]='\n';
    }
    for(DomNode *c=n->first_child;c;c=c->next_sibling)collect_styles(c,css,used);
}
void css_apply_styles(DomNode *root, const char *extra_css) {
    if (!root)
        return;
    char *css=kmalloc(16384); if(!css)return;
    int used=0; collect_styles(root,css,&used);
    if(extra_css)for(const char *p=extra_css;*p && used<16383;p++)css[used++]=*p;
    css[used]=0; apply_css_rules_recursive(root, css); kfree(css);
}
