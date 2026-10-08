#include "browser.h"
#ifdef POLLIK_BROWSER_STANDALONE
#include "../../common/utf8.h"
#endif

/* Extern system rendering primitives declared in system.h */
extern void sys_draw_rect_clipped(int x, int y, int w, int h, u32 c, int cx1, int cy1, int cx2, int cy2);
extern void sys_draw_rounded_clipped(int x, int y, int w, int h, int r, u32 c, int cx1, int cy1, int cx2, int cy2);
extern void sys_draw_letter_clipped(int x, int y, u8 c, u32 color, int scale, int cx1, int cy1, int cx2, int cy2);
extern int sys_get_glyph_advance(u8 c, int scale);
extern int ui_is_dark(void);

static int get_font_height(int scale) {
    if (scale < 1) scale = 1;
    if (scale > 5) scale = 5;
    static const int heights[5] = { 13, 16, 20, 24, 28 };
    return heights[scale - 1];
}

static void render_text_string(int x, int y, const char *s, u32 color, int scale, int cx1, int cy1, int cx2, int cy2) {
#ifdef POLLIK_BROWSER_STANDALONE
    size_t left=strlen(s);while(left){size_t used;uint32_t c=pollik_utf8_next(s,left,&used);
        if(c>=32){sys_draw_codepoint_clipped(x,y,c,color,scale,cx1,cy1,cx2,cy2);x+=sys_get_codepoint_advance(c,scale);}s+=used;left-=used;}
#else
    while (*s) {
        u8 c = (u8)*s++;
        if (c >= 32 && c < 127) {
            sys_draw_letter_clipped(x, y, c, color, scale, cx1, cy1, cx2, cy2);
            x += sys_get_glyph_advance(c, scale);
        }
    }
#endif
}
static void copy_text_range(char *out, int cap, const char *src, int begin, int end) {
    if (cap <= 0) return;
    int i = 0;
    while (src[begin] && begin < end && i < cap - 1) out[i++] = src[begin++];
    out[i] = 0;
}

static void draw_selected_word(DomNode *node, const char *word, int start, int x, int y,
                               int scale, int line_h, int lo, int hi,
                               int cx1, int cy1, int cx2, int cy2) {
    u32 selection = ui_is_dark() ? 0x344866 : 0xbcd8fa;
    for (int i = 0; word[i]; i++) {
        int at = start + i;
        int advance = sys_get_glyph_advance((u8)word[i], scale);
        if (at >= lo && at < hi)
            sys_draw_rect_clipped(x, y + 2, advance, line_h - 4, selection, cx1, cy1, cx2, cy2);
        x += advance;
    }
    (void)node;
}

static void render_text_selection(DomNode *node, int sx, int sy, int scale, int line_h,
                                  int avail_w, int space_w, int cx1, int cy1, int cx2, int cy2) {
    if (!g_browser.selection_all &&
        (g_browser.selection_node != node || g_browser.selection_anchor == g_browser.selection_focus)) return;
    int lo = g_browser.selection_all ? 0 : g_browser.selection_anchor;
    int hi = g_browser.selection_all ? (int)strlen(node->text) : g_browser.selection_focus;
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    int length = (int)strlen(node->text);
    if (lo < 0) lo = 0;
    if (hi > length) hi = length;
    if (lo >= hi) return;
    int cw = (node->parent && node->parent->box.content_w > 0) ? node->parent->box.content_w
           : (node->parent && node->parent->box.w > 0) ? node->parent->box.w : avail_w;
    int has_newline = 0;
    for (const char *q = node->text; *q; q++) if (*q == '\n') { has_newline = 1; break; }
    if (!has_newline && node->style.text_align != TEXT_ALIGN_LEFT) {
        int tw = sys_text_width(node->text, scale);
        if (tw < cw && node->style.text_align == TEXT_ALIGN_CENTER) sx += (cw - tw) / 2;
        else if (tw < cw && node->style.text_align == TEXT_ALIGN_RIGHT) sx += cw - tw;
    }
    int cur_x = sx, cur_y = sy, word_start = 0, w_idx = 0;
    char word[128];
    for (int at = 0;; at++) {
        char ch = node->text[at];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || !ch) {
            if (w_idx > 0) {
                word[w_idx] = 0;
                int ww = sys_text_width(word, scale);
                if (cur_x + ww > sx + avail_w && cur_x > sx) { cur_y += line_h; cur_x = sx; }
                draw_selected_word(node, word, word_start, cur_x, cur_y, scale, line_h, lo, hi,
                                   cx1, cy1, cx2, cy2);
                cur_x += ww + space_w;
                w_idx = 0;
            }
            if (!ch) break;
            if (ch == '\n') {
                if (at >= lo && at < hi)
                    sys_draw_rect_clipped(cur_x - space_w, cur_y + 2, space_w, line_h - 4,
                                          ui_is_dark() ? 0x344866 : 0xbcd8fa, cx1, cy1, cx2, cy2);
                cur_y += line_h; cur_x = sx;
            } else if (at >= lo && at < hi) {
                sys_draw_rect_clipped(cur_x - space_w, cur_y + 2, space_w, line_h - 4,
                                      ui_is_dark() ? 0x344866 : 0xbcd8fa, cx1, cy1, cx2, cy2);
            }
        } else {
            if (!w_idx) word_start = at;
            if (w_idx < (int)sizeof(word) - 1) word[w_idx++] = ch;
        }
    }
}

int browser_text_offset_at(DomNode *node, int target_x, int target_y) {
    if (!node || node->type != NODE_TEXT || !node->text) return 0;
    int scale = node->style.font_size > 0 ? node->style.font_size : 1;
    int line_h = node->style.line_height > 0 ? node->style.line_height : get_font_height(scale) + 4;
    int space_w = sys_text_width(" ", scale);
    if (space_w <= 0) space_w = 6 * scale;
    int sx = node->box.x, sy = node->box.y;
    int avail_w = node->box.w > 0 ? node->box.w : 600;
    int cw = (node->parent && node->parent->box.content_w > 0) ? node->parent->box.content_w
           : (node->parent && node->parent->box.w > 0) ? node->parent->box.w : avail_w;
    int has_newline = 0;
    for (const char *q = node->text; *q; q++) if (*q == '\n') { has_newline = 1; break; }
    if (!has_newline && node->style.text_align != TEXT_ALIGN_LEFT) {
        int tw = sys_text_width(node->text, scale);
        if (tw < cw && node->style.text_align == TEXT_ALIGN_CENTER) sx += (cw - tw) / 2;
        else if (tw < cw && node->style.text_align == TEXT_ALIGN_RIGHT) sx += cw - tw;
    }
    int cur_x = sx, cur_y = sy, word_start = 0, w_idx = 0;
    char word[128];
    for (int at = 0;; at++) {
        char ch = node->text[at];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || !ch) {
            if (w_idx > 0) {
                word[w_idx] = 0;
                int ww = sys_text_width(word, scale);
                if (cur_x + ww > sx + avail_w && cur_x > sx) { cur_y += line_h; cur_x = sx; }
                for (int k = 0; k < w_idx; k++) {
                    int advance = sys_get_glyph_advance((u8)word[k], scale);
                    if (target_y < cur_y + line_h && target_x < cur_x + advance / 2)
                        return word_start + k;
                    cur_x += advance;
                }
                cur_x += space_w;
                w_idx = 0;
            }
            if (!ch) return at;
            if (ch == '\n') {
                if (target_y < cur_y + line_h) return at;
                cur_y += line_h; cur_x = sx;
            } else if (target_y < cur_y + line_h && target_x < cur_x) {
                return at;
            }
        } else {
            if (!w_idx) word_start = at;
            if (w_idx < (int)sizeof(word) - 1) word[w_idx++] = ch;
        }
    }
}

static void render_node_text(DomNode *node, int sx, int sy, int cx1, int cy1, int cx2, int cy2) {
    if (!node || !node->text) return;

    int scale = node->style.font_size > 0 ? node->style.font_size : 1;
    int line_h = node->style.line_height > 0 ? node->style.line_height : (get_font_height(scale) + 4);
    int space_w = sys_text_width(" ", scale);
    if (space_w <= 0) space_w = 6 * scale;

    u32 color = node->style.color;
    int dark = ui_is_dark();
    /* If inside an <a> tag, use link color */
    DomNode *p = node->parent;
    int is_link = 0;
    while (p) {
        if (p->tag[0] == 'a' && p->tag[1] == '\0') {
            is_link = 1;
            if (color == 0x000000 || color == 0x333333) {
                color = dark ? 0x60a5fa : 0x1a0dab;
            }
            break;
        }
        p = p->parent;
    }

    if (!is_link && dark && (color == 0x000000 || color == 0x333333 || color == 0x222222 || color == 0x44355b)) {
        color = 0xf1f5f9;
    }

    int avail_w = node->box.w > 0 ? node->box.w : 600;
    render_text_selection(node, sx, sy, scale, line_h, avail_w, space_w, cx1, cy1, cx2, cy2);

    const char *ptr = node->text;
    int cur_x = sx;
    int cur_y = sy;
    /* Honor text-align: center/right for a single line (headings, captions). */
    {
        int align = node->style.text_align;
        int cw = (node->parent && node->parent->box.content_w > 0) ? node->parent->box.content_w
               : (node->parent && node->parent->box.w > 0) ? node->parent->box.w : avail_w;
        int nl = 0;
        for (const char *q = node->text; *q; q++) if (*q == '\n') { nl = 1; break; }
        if (align != TEXT_ALIGN_LEFT && !nl) {
            int tw = sys_text_width(node->text, scale);
            int ox = sx;
            if (tw < cw) {
                if (align == TEXT_ALIGN_CENTER) ox = sx + (cw - tw) / 2;
                else if (align == TEXT_ALIGN_RIGHT) ox = sx + (cw - tw);
            }
            render_text_string(ox, sy, node->text, color, scale, cx1, cy1, cx2, cy2);
            if (is_link)
                sys_draw_rect_clipped(ox, sy + get_font_height(scale) + 1, tw, 1, color, cx1, cy1, cx2, cy2);
            return;
        }
    }
    char word[128];
    int w_idx = 0;

    while (*ptr) {
        if (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r') {
            if (w_idx > 0) {
                word[w_idx] = '\0';
                int ww = sys_text_width(word, scale);
                if (cur_x + ww > sx + avail_w && cur_x > sx) {
                    cur_y += line_h;
                    cur_x = sx;
                }
                render_text_string(cur_x, cur_y, word, color, scale, cx1, cy1, cx2, cy2);
                if (is_link) {
                    /* Underline */
                    sys_draw_rect_clipped(cur_x, cur_y + get_font_height(scale) + 1, ww, 1, color, cx1, cy1, cx2, cy2);
                }
                cur_x += ww + space_w;
                w_idx = 0;
            }
            if (*ptr == '\n') {
                cur_y += line_h;
                cur_x = sx;
            }
        } else {
            if (w_idx < (int)sizeof(word) - 1) {
                word[w_idx++] = *ptr;
            }
        }
        ptr++;
    }

    if (w_idx > 0) {
        word[w_idx] = '\0';
        int ww = sys_text_width(word, scale);
        if (cur_x + ww > sx + avail_w && cur_x > sx) {
            cur_y += line_h;
            cur_x = sx;
        }
        render_text_string(cur_x, cur_y, word, color, scale, cx1, cy1, cx2, cy2);
        if (is_link) {
            sys_draw_rect_clipped(cur_x, cur_y + get_font_height(scale) + 1, ww, 1, color, cx1, cy1, cx2, cy2);
        }
    }
}

static void render_node_single(DomNode *node, int origin_x, int origin_y, int scroll_y, int cx1, int cy1, int cx2, int cy2) {
    if (!node || node->style.display == DISPLAY_NONE) return;

    int sx = origin_x + node->box.x;
    int sy = origin_y + node->box.y - scroll_y;
    int sw = node->box.w;
    int sh = node->box.h;

    /* Culling check against viewport */
    if (sy + sh < cy1 || sy > cy2) {
        return;
    }

    /* 1. Background */
    if (node->style.has_bg_color) {
        if (node->style.border_radius > 0) {
            sys_draw_rounded_clipped(sx, sy, sw, sh, node->style.border_radius, node->style.bg_color, cx1, cy1, cx2, cy2);
        } else {
            sys_draw_rect_clipped(sx, sy, sw, sh, node->style.bg_color, cx1, cy1, cx2, cy2);
        }
    }

    /* 2. Borders */
    int bw = node->style.border_width;
    if (bw > 0) {
        u32 bc = node->style.border_color;
        /* Top */
        sys_draw_rect_clipped(sx, sy, sw, bw, bc, cx1, cy1, cx2, cy2);
        /* Bottom */
        sys_draw_rect_clipped(sx, sy + sh - bw, sw, bw, bc, cx1, cy1, cx2, cy2);
        /* Left */
        sys_draw_rect_clipped(sx, sy, bw, sh, bc, cx1, cy1, cx2, cy2);
        /* Right */
        sys_draw_rect_clipped(sx + sw - bw, sy, bw, sh, bc, cx1, cy1, cx2, cy2);
    }

    /* 3. Special HTML elements */
    if (node->tag[0] == 'h' && node->tag[1] == 'r' && node->tag[2] == '\0') {
        sys_draw_rect_clipped(sx, sy + sh / 2, sw, 1, 0xd0d0d8, cx1, cy1, cx2, cy2);
    } else if (node->tag[0] == 'b' && node->tag[1] == 'u' && node->tag[2] == 't' && node->tag[3] == 't') {
        /* Default button styling if no background set */
        if (!node->style.has_bg_color) {
            int dark = ui_is_dark();
            u32 b_bg = dark ? 0x1e2436 : 0xf2f0f6;
            u32 b_brd1 = dark ? 0x334155 : 0xcbc5d8;
            u32 b_brd2 = dark ? 0x141824 : 0x9f97b0;
            sys_draw_rounded_clipped(sx, sy, sw, sh, 4, b_bg, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy, sw, 1, b_brd1, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy + sh - 1, sw, 1, b_brd2, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy, 1, sh, b_brd1, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx + sw - 1, sy, 1, sh, b_brd2, cx1, cy1, cx2, cy2);
        }
    } else if (node->tag[0] == 'i' && node->tag[1] == 'n' && node->tag[2] == 'p' && node->tag[3] == 'u') {
        int is_submit = (node->input_type[0] == 's' && node->input_type[1] == 'u' && node->input_type[2] == 'b' &&
                         node->input_type[3] == 'm' && node->input_type[4] == 'i' && node->input_type[5] == 't') ||
                        (node->input_type[0] == 'b' && node->input_type[1] == 'u' && node->input_type[2] == 't' &&
                         node->input_type[3] == 't' && node->input_type[4] == 'o' && node->input_type[5] == 'n');

        int dark = ui_is_dark();
        if (is_submit) {
            /* Styled submit button */
            u32 s_bg = dark ? 0x1e2436 : 0xf2f0f6;
            u32 s_brd1 = dark ? 0x334155 : 0xcbc5d8;
            u32 s_brd2 = dark ? 0x141824 : 0x9f97b0;
            u32 s_fg = dark ? 0xf1f5f9 : 0x3b334a;
            sys_draw_rounded_clipped(sx, sy, sw, sh, 4, s_bg, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy, sw, 1, s_brd1, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy + sh - 1, sw, 1, s_brd2, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy, 1, sh, s_brd1, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx + sw - 1, sy, 1, sh, s_brd2, cx1, cy1, cx2, cy2);
            const char *label = node->value[0] ? node->value : "Submit";
            int tw = sys_text_width(label, 1);
            int tx = sx + (sw - tw) / 2;
            if (tx < sx + 4) tx = sx + 4;
            render_text_string(tx, sy + (sh - 14) / 2, label, s_fg, 1, cx1, cy1, cx2, cy2);
        } else {
            /* Standard text / search input control */
            int is_focused = (node == g_browser.focused_input);
            u32 border_c = is_focused ? (dark ? 0x60a5fa : 0x6a4fa3) : (dark ? 0x2b344a : 0xb8b0c8);
            int bw = is_focused ? 2 : 1;

            if (!node->style.has_bg_color) {
                sys_draw_rect_clipped(sx, sy, sw, sh, dark ? 0x0e111a : 0xffffff, cx1, cy1, cx2, cy2);
            }
            sys_draw_rect_clipped(sx, sy, sw, bw, border_c, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy, bw, sh, border_c, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx + sw - bw, sy, bw, sh, border_c, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy + sh - bw, sw, bw, border_c, cx1, cy1, cx2, cy2);

            /* Render input value */
            if (node->value[0]) {
                if (is_focused && g_browser.focused_anchor >= 0 && g_browser.focused_anchor != g_browser.focused_cursor) {
                    int a = g_browser.focused_anchor, b = g_browser.focused_cursor;
                    if (a > b) { int t = a; a = b; b = t; }
                    char before[64], selected[64];
                    copy_text_range(before, sizeof(before), node->value, 0, a);
                    copy_text_range(selected, sizeof(selected), node->value, a, b);
                    sys_draw_rect_clipped(sx + 6 + sys_text_width(before, 1), sy + 2,
                                          sys_text_width(selected, 1), sh - 4,
                                          dark ? 0x344866 : 0xbcd8fa, cx1, cy1, cx2, cy2);
                }
                render_text_string(sx + 6, sy + (sh - 14) / 2, node->value, dark ? 0xf1f5f9 : 0x202028, 1, cx1, cy1, cx2, cy2);
            }

            /* Render cursor if focused */
            if (is_focused) {
                char before[64];
                copy_text_range(before, sizeof(before), node->value, 0, g_browser.focused_cursor);
                int tw = sys_text_width(before, 1);
                int cur_x = sx + 6 + tw;
                if (cur_x < sx + sw - 4) {
                    sys_draw_rect_clipped(cur_x, sy + (sh - 14) / 2, 1, 14, dark ? 0x60a5fa : 0x403060, cx1, cy1, cx2, cy2);
                }
            }
        }
    }

    if(node->image && sw>0 && sh>0) {
        int iw=node->image_w, ih=node->image_h;
        const u8 *img=node->image;
        /* Downscaling uses bilinear filtering so real photos stay clean at any
         * display size; upscaling keeps a crisp nearest-neighbour sample. */
        int smooth = (iw>sw || ih>sh);
        for(int y=0;y<sh;y++)for(int x=0;x<sw;x++) {
            if(sx+x<cx1||sx+x>=cx2||sy+y<cy1||sy+y>=cy2)continue;
            u32 r,g,b,a;
            if(smooth) {
                int fx=((2*x+1)*iw*256)/(2*sw)-128, fy=((2*y+1)*ih*256)/(2*sh)-128;
                int x0=fx>>8, y0=fy>>8, tx=fx&255, ty=fy&255;
                if(x0<0){x0=0;tx=0;} if(y0<0){y0=0;ty=0;}
                int x1=x0+1,y1=y0+1;
                if(x0>iw-1)x0=iw-1; if(x1>iw-1)x1=iw-1;
                if(y0>ih-1)y0=ih-1; if(y1>ih-1)y1=ih-1;
                const u8 *p00=img+4*(y0*iw+x0),*p10=img+4*(y0*iw+x1);
                const u8 *p01=img+4*(y1*iw+x0),*p11=img+4*(y1*iw+x1);
                int top,bot;
                top=(p00[0]*(256-tx)+p10[0]*tx)>>8; bot=(p01[0]*(256-tx)+p11[0]*tx)>>8; r=(u32)((top*(256-ty)+bot*ty)>>8);
                top=(p00[1]*(256-tx)+p10[1]*tx)>>8; bot=(p01[1]*(256-tx)+p11[1]*tx)>>8; g=(u32)((top*(256-ty)+bot*ty)>>8);
                top=(p00[2]*(256-tx)+p10[2]*tx)>>8; bot=(p01[2]*(256-tx)+p11[2]*tx)>>8; b=(u32)((top*(256-ty)+bot*ty)>>8);
                top=(p00[3]*(256-tx)+p10[3]*tx)>>8; bot=(p01[3]*(256-tx)+p11[3]*tx)>>8; a=(u32)((top*(256-ty)+bot*ty)>>8);
            } else {
                const u8 *p=img+4*((y*ih/sh)*iw+x*iw/sw);
                r=p[0]; g=p[1]; b=p[2]; a=p[3];
            }
            u32 c=(((r*a+255*(255-a))/255)<<16)|(((g*a+255*(255-a))/255)<<8)|((b*a+255*(255-a))/255);
            sys_draw_rect_clipped(sx+x,sy+y,1,1,c,cx1,cy1,cx2,cy2);
        }
    }
    /* <canvas> backing store drawn through PollikGL. */
    if(node->canvas && sw>0 && sh>0)
        sys_draw_canvas_clipped(sx,sy,sw,sh,node->canvas,node->canvas_w,node->canvas_h,cx1,cy1,cx2,cy2);
    /* 4. Text Node */
    if (node->type == NODE_TEXT) {
        render_node_text(node, sx, sy, cx1, cy1, cx2, cy2);
    }
}

void render_dom(DomNode *root, int origin_x, int origin_y, int viewport_w, int viewport_h, int scroll_y) {
    if (!root) return;

    int cx1 = origin_x;
    int cy1 = origin_y;
    int cx2 = origin_x + viewport_w;
    int cy2 = origin_y + viewport_h;

    /* Pre-order DFS traversal to paint containers before children */
    DomNode *stack[128];
    int top = 0;
    stack[top++] = root;

    while (top > 0) {
        DomNode *curr = stack[--top];
        if(curr->style.display == DISPLAY_NONE)continue;
        render_node_single(curr, origin_x, origin_y, scroll_y, cx1, cy1, cx2, cy2);

        /* Push children in reverse order so first child is rendered first */
        DomNode *ch = curr->last_child;
        while (ch && top < 120) {
            stack[top++] = ch;
            ch = ch->prev_sibling;
        }
    }
}
