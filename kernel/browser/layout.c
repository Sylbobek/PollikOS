#include "browser.h"

static int get_font_height(int scale) {
    if (scale < 1) scale = 1;
    if (scale > 5) scale = 5;
    /* Font height roughly 12 to 28 px */
    static const int heights[5] = { 13, 16, 20, 24, 28 };
    return heights[scale - 1];
}

/* Layout text node with word wrapping */
static void layout_text_node(DomNode *node, int start_x, int start_y, int avail_w) {
    if (!node || !node->text) {
        node->box.w = 0;
        node->box.h = 0;
        return;
    }

    const char *visible=node->text;while(*visible && *visible<=32)visible++;
    if(!*visible){node->box.x=start_x;node->box.y=start_y;node->box.w=node->box.h=0;return;}
    int scale = node->style.font_size > 0 ? node->style.font_size : 1;
    int line_h = node->style.line_height > 0 ? node->style.line_height : (get_font_height(scale) + 4);
    int space_w = sys_text_width(" ", scale);
    if (space_w <= 0) space_w = 6 * scale;

    const char *p = node->text;
    int cur_line_w = 0;
    int max_w = 0;
    int total_h = line_h;
    char word[128];
    int w_idx = 0;

    while (*p) {
        if (!((p - node->text) & 255)) browser_work_checkpoint();
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            if (w_idx > 0) {
                word[w_idx] = '\0';
                int ww = sys_text_width(word, scale);
                if (cur_line_w + ww > avail_w && cur_line_w > 0) {
                    total_h += line_h;
                    cur_line_w = ww + space_w;
                } else {
                    cur_line_w += ww + space_w;
                }
                if (cur_line_w > max_w) max_w = cur_line_w;
                w_idx = 0;
            }
            if (*p == '\n') {
                total_h += line_h;
                cur_line_w = 0;
            }
        } else {
            if (w_idx < (int)sizeof(word) - 1) {
                word[w_idx++] = *p;
            }
        }
        p++;
    }

    if (w_idx > 0) {
        word[w_idx] = '\0';
        int ww = sys_text_width(word, scale);
        if (cur_line_w + ww > avail_w && cur_line_w > 0) {
            total_h += line_h;
            cur_line_w = ww;
        } else {
            cur_line_w += ww;
        }
        if (cur_line_w > max_w) max_w = cur_line_w;
    }

    node->box.x = start_x;
    node->box.y = start_y;
    node->box.w = max_w>avail_w?avail_w:max_w;
    node->box.h = total_h;
    node->box.content_w = max_w;
    node->box.content_h = total_h;
}

static void layout_node_recursive(DomNode *node, int cur_x, int cur_y, int avail_w, int *out_h);

/* Flexbox layout handler */
static void layout_flex_container(DomNode *node, int cur_x, int cur_y, int content_w, int *out_h) {
    int is_column = (node->style.flex_dir == FLEX_DIR_COLUMN);
    int gap = node->style.gap >= 0 ? node->style.gap : 0;

    if (is_column) {
        int y = cur_y;
        DomNode *child = node->first_child;
        while (child) {
            if (child->style.display != DISPLAY_NONE) {
                int ch_h = 0;
                layout_node_recursive(child, cur_x, y, content_w, &ch_h);
                y += child->box.h + gap;
            }
            child = child->next_sibling;
        }
        *out_h = (y > cur_y) ? (y - cur_y - gap) : 0;
    } else {
        /* Row flex */
        int total_children_w = 0;
        int max_child_h = 0;
        int child_count = 0;

        DomNode *child = node->first_child;
        while (child) {
            if (child->style.display != DISPLAY_NONE) {
                int ch_h = 0;
                int child_w = child->style.width >= 0 ? child->style.width : (content_w / 3);
                layout_node_recursive(child, 0, 0, child_w, &ch_h);
                total_children_w += child->box.w;
                if (child->box.h > max_child_h) max_child_h = child->box.h;
                child_count++;
            }
            child = child->next_sibling;
        }

        if (child_count > 1) {
            total_children_w += (child_count - 1) * gap;
        }

        /* Justify content offset */
        int start_x = cur_x;
        int extra_space = content_w - total_children_w;
        if (extra_space < 0) extra_space = 0;

        int space_between = 0;
        if (node->style.justify == JUSTIFY_CENTER) {
            start_x += extra_space / 2;
        } else if (node->style.justify == JUSTIFY_END) {
            start_x += extra_space;
        } else if (node->style.justify == JUSTIFY_BETWEEN && child_count > 1) {
            space_between = extra_space / (child_count - 1);
        }

        int x = start_x;
        child = node->first_child;
        while (child) {
            if (child->style.display != DISPLAY_NONE) {
                int y = cur_y;
                if (node->style.align_items == ALIGN_ITEMS_CENTER) {
                    y += (max_child_h - child->box.h) / 2;
                } else if (node->style.align_items == ALIGN_ITEMS_END) {
                    y += (max_child_h - child->box.h);
                } else if (node->style.align_items == ALIGN_ITEMS_STRETCH) {
                    child->box.h = max_child_h;
                }

                int ch_h = 0;
                layout_node_recursive(child, x, y, child->box.w, &ch_h);
                x += child->box.w + gap + space_between;
            }
            child = child->next_sibling;
        }
        *out_h = max_child_h;
    }
}

/* Table layout handler */
static void layout_table(DomNode *node, int cur_x, int cur_y, int content_w, int *out_h) {
    /* Count columns and rows */
    int num_cols = 0;
    DomNode *row = node->first_child;
    while (row) {
        if (row->tag[0] == 't' && row->tag[1] == 'r') {
            int cols_in_row = 0;
            DomNode *cell = row->first_child;
            while (cell) {
                if (cell->tag[0] == 't' && (cell->tag[1] == 'd' || cell->tag[1] == 'h')) {
                    cols_in_row++;
                }
                cell = cell->next_sibling;
            }
            if (cols_in_row > num_cols) num_cols = cols_in_row;
        }
        row = row->next_sibling;
    }

    if (num_cols <= 0) num_cols = 1;
    int col_w = content_w / num_cols;

    int y = cur_y;
    row = node->first_child;
    while (row) {
        if (row->tag[0] == 't' && row->tag[1] == 'r') {
            int max_row_h = 0;
            int x = cur_x;
            DomNode *cell = row->first_child;
            while (cell) {
                if (cell->tag[0] == 't' && (cell->tag[1] == 'd' || cell->tag[1] == 'h')) {
                    int ch_h = 0;
                    layout_node_recursive(cell, x, y, col_w, &ch_h);
                    if (cell->box.h > max_row_h) max_row_h = cell->box.h;
                    x += col_w;
                }
                cell = cell->next_sibling;
            }
            row->box.x = cur_x;
            row->box.y = y;
            row->box.w = content_w;
            row->box.h = max_row_h > 0 ? max_row_h : 20;

            /* Align all cells in row to max_row_h */
            cell = row->first_child;
            while (cell) {
                cell->box.h = row->box.h;
                cell = cell->next_sibling;
            }

            y += row->box.h;
        }
        row = row->next_sibling;
    }
    *out_h = y - cur_y;
}

static void layout_node_recursive(DomNode *node, int cur_x, int cur_y, int avail_w, int *out_h) {
    browser_work_checkpoint();
    if (!node) {
        if (out_h) *out_h = 0;
        return;
    }

    if (node->style.display == DISPLAY_NONE) {
        node->box.x = cur_x;
        node->box.y = cur_y;
        node->box.w = 0;
        node->box.h = 0;
        node->box.content_w = 0;
        node->box.content_h = 0;
        if (out_h) *out_h = 0;
        return;
    }

    if(node->image && node->image_w>0 && node->image_h>0) {
        int w=node->style.width>=0?node->style.width:node->image_w;if(w>avail_w)w=avail_w;
        int h=node->style.height>=0?node->style.height:node->image_h*w/node->image_w;
        node->box=(LayoutBox){cur_x,cur_y,w,h,w,h};if(out_h)*out_h=h;return;
    }
    if (node->type == NODE_TEXT) {
        layout_text_node(node, cur_x, cur_y, avail_w);
        if (out_h) *out_h = node->box.h;
        return;
    }

    /* Box model margins, borders, paddings */
    int ml = node->style.margin_left;
    int mr = node->style.margin_right;
    int mt = node->style.margin_top;
    int mb = node->style.margin_bottom;

    int pl = node->style.padding_left;
    int pr = node->style.padding_right;
    int pt = node->style.padding_top;
    int pb = node->style.padding_bottom;

    int bw = node->style.border_width;

    if(ml<0 || mr<0) {
        int spare=avail_w-(node->style.width>=0?node->style.width:avail_w);
        if(spare<0)spare=0;
        if(ml<0 && mr<0)ml=mr=spare/2;
        else if(ml<0)ml=spare-mr;else mr=spare-ml;
    }
    int box_x = cur_x + ml;
    int box_y = cur_y + mt;

    int box_w = node->style.width;
    if (box_w < 0) {
        if (node->style.display == DISPLAY_INLINE || node->style.display == DISPLAY_INLINE_BLOCK) {
            box_w = avail_w; /* May shrink after child layout */
        } else {
            box_w = avail_w - ml - mr;
        }
    }
    if (box_w < 0) box_w = 0;

    int inner_w = box_w - pl - pr - 2 * bw;
    if (inner_w < 0) inner_w = 0;

    int content_x = box_x + bw + pl;
    int content_y = box_y + bw + pt;

    int children_h = 0;

    if (node->style.display == DISPLAY_FLEX) {
        layout_flex_container(node, content_x, content_y, inner_w, &children_h);
    } else if (node->tag[0] == 't' && node->tag[1] == 'a' && node->tag[2] == 'b' && node->tag[3] == 'l' && node->tag[4] == 'e') {
        layout_table(node, content_x, content_y, inner_w, &children_h);
    } else {
        /* Standard block / inline layout */
        int flow_y = content_y;
        int flow_x = content_x;
        int cur_row_h = 0;
        int max_inline_w = 0;

        DomNode *child = node->first_child;
        while (child) {
            if (child->style.display != DISPLAY_NONE) {
                int is_inline = (child->style.display == DISPLAY_INLINE || child->style.display == DISPLAY_INLINE_BLOCK || child->type == NODE_TEXT);

                if (is_inline) {
                    int ch_h = 0;
                    int remaining_w = content_x + inner_w - flow_x;
                    if (remaining_w <= 0) {
                        flow_y += cur_row_h;
                        flow_x = content_x;
                        cur_row_h = 0;
                        remaining_w = inner_w;
                    }

                    layout_node_recursive(child, flow_x, flow_y, remaining_w, &ch_h);
                    flow_x += child->box.w;
                    if (child->box.h > cur_row_h) cur_row_h = child->box.h;
                    if (flow_x - content_x > max_inline_w) max_inline_w = flow_x - content_x;
                } else {
                    /* Block element: finish previous inline line */
                    if (cur_row_h > 0 || flow_x > content_x) {
                        flow_y += cur_row_h;
                        flow_x = content_x;
                        cur_row_h = 0;
                    }

                    int ch_h = 0;
                    layout_node_recursive(child, content_x, flow_y, inner_w, &ch_h);
                    flow_y += ch_h;
                }
            }
            child = child->next_sibling;
        }

        if (cur_row_h > 0) {
            flow_y += cur_row_h;
        }

        children_h = flow_y - content_y;

        if ((node->style.display == DISPLAY_INLINE || node->style.display == DISPLAY_INLINE_BLOCK) && node->style.width < 0) {
            box_w = max_inline_w + pl + pr + 2 * bw;
        }
    }

    int box_h = node->style.height;
    if (box_h < 0) {
        box_h = children_h + pt + pb + 2 * bw;
    }
    if (box_h < 0) box_h = 0;

    /* Inputs/buttons default minimum sizing */
    if (node->tag[0] == 'b' && node->tag[1] == 'u' && node->tag[2] == 't' && node->tag[3] == 't') {
        if (box_h < 26) box_h = 26;
        if (box_w < 60) box_w = 60;
    } else if (node->tag[0] == 'i' && node->tag[1] == 'n' && node->tag[2] == 'p' && node->tag[3] == 'u') {
        if (box_h < 26) box_h = 26;
        int is_submit = (node->input_type[0] == 's' && node->input_type[1] == 'u' && node->input_type[2] == 'b' &&
                         node->input_type[3] == 'm' && node->input_type[4] == 'i' && node->input_type[5] == 't') ||
                        (node->input_type[0] == 'b' && node->input_type[1] == 'u' && node->input_type[2] == 't' &&
                         node->input_type[3] == 't' && node->input_type[4] == 'o' && node->input_type[5] == 'n');
        if (is_submit) {
            int val_w = sys_text_width(node->value[0] ? node->value : "Submit", 1) + 16;
            if (box_w < val_w) box_w = val_w;
        } else {
            if (box_w < 120 && node->style.width < 0) box_w = 180;
        }
    }

    node->box.x = box_x;
    node->box.y = box_y;
    node->box.w = box_w;
    node->box.h = box_h;
    node->box.content_w = inner_w;
    node->box.content_h = children_h;

    if (out_h) {
        *out_h = box_h + mt + mb;
    }
}

void layout_compute(DomNode *root, int viewport_w, int *out_total_h) {
    if (!root) {
        if (out_total_h) *out_total_h = 0;
        return;
    }

    int total_h = 0;
    layout_node_recursive(root, 0, 0, viewport_w, &total_h);

    /* Find max bottom of any box for scrolling */
    int max_y = total_h;
    DomNode *stack[128];
    int top = 0;
    stack[top++] = root;

    while (top > 0) {
        DomNode *curr = stack[--top];
        int bottom = curr->box.y + curr->box.h;
        if (bottom > max_y) max_y = bottom;

        DomNode *ch = curr->first_child;
        while (ch && top < 120) {
            stack[top++] = ch;
            ch = ch->next_sibling;
        }
    }

    if (out_total_h) {
        *out_total_h = max_y;
    }
}
