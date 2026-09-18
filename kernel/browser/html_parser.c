#include "browser.h"

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
    return node;
}

DomNode *dom_create_element(const char *tag) {
    DomNode *node = dom_create_node(NODE_ELEMENT);
    if (!node)
        return 0;
    int i = 0;
    while (tag[i] && i < 15) {
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
                    int num = 0;
                    if (ent[1] == 'x' || ent[1] == 'X') {
                        for (int k = 2; k < elen; k++) {
                            char c = ent[k];
                            int v = (c >= '0' && c <= '9') ? (c - '0') :
                                    (c >= 'a' && c <= 'f') ? (c - 'a' + 10) :
                                    (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : -1;
                            if (v < 0) break;
                            num = num * 16 + v;
                        }
                    } else {
                        for (int k = 1; k < elen; k++) {
                            if (ent[k] >= '0' && ent[k] <= '9') {
                                num = num * 10 + (ent[k] - '0');
                            } else break;
                        }
                    }
                    char rep = 0;
                    if (num >= 32 && num <= 126) rep = (char)num;
                    else if (num == 160) rep = ' ';
                    else if (num == 260 || num == 261) rep = (num == 260 ? 'A' : 'a');
                    else if (num == 262 || num == 263) rep = (num == 262 ? 'C' : 'c');
                    else if (num == 280 || num == 281) rep = (num == 280 ? 'E' : 'e');
                    else if (num == 321 || num == 322) rep = (num == 321 ? 'L' : 'l');
                    else if (num == 323 || num == 324) rep = (num == 323 ? 'N' : 'n');
                    else if (num == 211 || num == 243) rep = (num == 211 ? 'O' : 'o');
                    else if (num == 346 || num == 347) rep = (num == 346 ? 'S' : 's');
                    else if (num == 377 || num == 378 || num == 379 || num == 380) rep = (num % 2 == 1 ? 'Z' : 'z');
                    else if (num == 8211 || num == 8212) rep = '-';
                    else if (num == 8216 || num == 8217) rep = '\'';
                    else if (num == 8220 || num == 8221) rep = '"';
                    else if (num == 8230) rep = '.';
                    if (rep) {
                        dst[d++] = rep;
                        s = semi + 1;
                        continue;
                    }
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
    EventListener *el = node->listeners;
    while (el) {
        EventListener *next_el = el->next;
        kfree(el);
        el = next_el;
    }
    kfree(node);
}

static int is_void_tag(const char *tag) {
    return (tag[0] == 'b' && tag[1] == 'r' && tag[2] == 0) ||
           (tag[0] == 'h' && tag[1] == 'r' && tag[2] == 0) ||
           (tag[0] == 'i' && tag[1] == 'm' && tag[2] == 'g' && tag[3] == 0) ||
           (tag[0] == 'i' && tag[1] == 'n' && tag[2] == 'p' && tag[3] == 'u' && tag[4] == 't' && tag[5] == 0) ||
           (tag[0] == 'm' && tag[1] == 'e' && tag[2] == 't' && tag[3] == 'a' && tag[4] == 0) ||
           (tag[0] == 'l' && tag[1] == 'i' && tag[2] == 'n' && tag[3] == 'k' && tag[4] == 0);
}

static void parse_attribute(DomNode *node, const char *attr_name, const char *attr_val) {
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

DomNode *html_parse(const char *html, int len) {
    if (!html || len <= 0)
        return 0;

    DomNode *root = dom_create_node(NODE_DOCUMENT);
    if (!root)
        return 0;

    DomNode *stack[64];
    int stack_top = 0;
    stack[0] = root;

    int pos = 0;
    while (pos < len) {
        browser_work_checkpoint();
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

            /* Push to stack if not void and not self-closing */
            if (!self_closing && !is_void_tag(tag) && stack_top < 63) {
                stack[++stack_top] = elem;
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
            int has_content = 0;
            for (int k = 0; k < text_len; k++) {
                if (!(k & 255)) browser_work_checkpoint();
                if (html[text_start + k] > 32) {
                    has_content = 1;
                    break;
                }
            }

            if (has_content) {
                DomNode *txt = dom_create_text(html + text_start, text_len);
                if (txt)
                    dom_append_child(stack[stack_top], txt);
            }
        }
    }

    return root;
}
