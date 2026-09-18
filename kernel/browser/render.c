#include "browser.h"

/* Extern system rendering primitives declared in system.h */
extern void sys_draw_rect_clipped(int x, int y, int w, int h, u32 c, int cx1, int cy1, int cx2, int cy2);
extern void sys_draw_rounded_clipped(int x, int y, int w, int h, int r, u32 c, int cx1, int cy1, int cx2, int cy2);
extern void sys_draw_letter_clipped(int x, int y, u8 c, u32 color, int scale, int cx1, int cy1, int cx2, int cy2);
extern int sys_get_glyph_advance(u8 c, int scale);

static int get_font_height(int scale) {
    if (scale < 1) scale = 1;
    if (scale > 5) scale = 5;
    static const int heights[5] = { 13, 16, 20, 24, 28 };
    return heights[scale - 1];
}

static void render_text_string(int x, int y, const char *s, u32 color, int scale, int cx1, int cy1, int cx2, int cy2) {
    while (*s) {
        u8 c = (u8)*s++;
        if (c >= 32 && c < 127) {
            sys_draw_letter_clipped(x, y, c, color, scale, cx1, cy1, cx2, cy2);
            x += sys_get_glyph_advance(c, scale);
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
    /* If inside an <a> tag, use link color */
    DomNode *p = node->parent;
    int is_link = 0;
    while (p) {
        if (p->tag[0] == 'a' && p->tag[1] == '\0') {
            is_link = 1;
            if (color == 0x000000 || color == 0x333333) {
                color = 0x1a0dab; /* Google/web link blue */
            }
            break;
        }
        p = p->parent;
    }

    const char *ptr = node->text;
    int cur_x = sx;
    int cur_y = sy;
    int avail_w = node->box.w > 0 ? node->box.w : 600;
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
            sys_draw_rounded_clipped(sx, sy, sw, sh, 4, 0xf2f0f6, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy, sw, 1, 0xcbc5d8, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy + sh - 1, sw, 1, 0x9f97b0, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy, 1, sh, 0xcbc5d8, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx + sw - 1, sy, 1, sh, 0x9f97b0, cx1, cy1, cx2, cy2);
        }
    } else if (node->tag[0] == 'i' && node->tag[1] == 'n' && node->tag[2] == 'p' && node->tag[3] == 'u') {
        int is_submit = (node->input_type[0] == 's' && node->input_type[1] == 'u' && node->input_type[2] == 'b' &&
                         node->input_type[3] == 'm' && node->input_type[4] == 'i' && node->input_type[5] == 't') ||
                        (node->input_type[0] == 'b' && node->input_type[1] == 'u' && node->input_type[2] == 't' &&
                         node->input_type[3] == 't' && node->input_type[4] == 'o' && node->input_type[5] == 'n');

        if (is_submit) {
            /* Styled submit button */
            sys_draw_rounded_clipped(sx, sy, sw, sh, 4, 0xf2f0f6, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy, sw, 1, 0xcbc5d8, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy + sh - 1, sw, 1, 0x9f97b0, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy, 1, sh, 0xcbc5d8, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx + sw - 1, sy, 1, sh, 0x9f97b0, cx1, cy1, cx2, cy2);
            const char *label = node->value[0] ? node->value : "Submit";
            int tw = sys_text_width(label, 1);
            int tx = sx + (sw - tw) / 2;
            if (tx < sx + 4) tx = sx + 4;
            render_text_string(tx, sy + (sh - 14) / 2, label, 0x3b334a, 1, cx1, cy1, cx2, cy2);
        } else {
            /* Standard text / search input control */
            int is_focused = (node == g_browser.focused_input);
            u32 border_c = is_focused ? 0x6a4fa3 : 0xb8b0c8;
            int bw = is_focused ? 2 : 1;

            if (!node->style.has_bg_color) {
                sys_draw_rect_clipped(sx, sy, sw, sh, 0xffffff, cx1, cy1, cx2, cy2);
            }
            sys_draw_rect_clipped(sx, sy, sw, bw, border_c, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy, bw, sh, border_c, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx + sw - bw, sy, bw, sh, border_c, cx1, cy1, cx2, cy2);
            sys_draw_rect_clipped(sx, sy + sh - bw, sw, bw, border_c, cx1, cy1, cx2, cy2);

            /* Render input value */
            if (node->value[0]) {
                render_text_string(sx + 6, sy + (sh - 14) / 2, node->value, 0x202028, 1, cx1, cy1, cx2, cy2);
            }

            /* Render cursor if focused */
            if (is_focused) {
                int tw = sys_text_width(node->value, 1);
                int cur_x = sx + 6 + tw;
                if (cur_x < sx + sw - 4) {
                    sys_draw_rect_clipped(cur_x, sy + (sh - 14) / 2, 1, 14, 0x403060, cx1, cy1, cx2, cy2);
                }
            }
        }
    }

    if(node->image && sw>0 && sh>0) {
        for(int y=0;y<sh;y++)for(int x=0;x<sw;x++) {
            if(sx+x<cx1||sx+x>=cx2||sy+y<cy1||sy+y>=cy2)continue;
            const u8 *p=node->image+4*((y*node->image_h/sh)*node->image_w+x*node->image_w/sw);
            u32 c=(((p[0]*p[3]+255*(255-p[3]))/255)<<16)|(((p[1]*p[3]+255*(255-p[3]))/255)<<8)|((p[2]*p[3]+255*(255-p[3]))/255);
            sys_draw_rect_clipped(sx+x,sy+y,1,1,c,cx1,cy1,cx2,cy2);
        }
    }
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
