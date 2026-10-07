#include "browser.h"
#include "script_type.h"
#include "../mem.h"
#include "../net/http.h"
#include "../net/net_util.h"
#include "../gui/app_host.h"
#include "../gui/apps.h"

BrowserApp g_browser;
/* is_loading is UI/queued state; load_active protects all mutable DOM/JS work,
 * including home/error paths which historically clear is_loading early. */
static int load_active;
static int close_pending;
int browser_load_cancelled(void){return close_pending;}
static void finish_closed_session(void);
void browser_work_checkpoint(void) {
    if (load_active) net_service_wait();
}

extern void sys_draw_rect_clipped(int x, int y, int w, int h, u32 c, int cx1, int cy1, int cx2, int cy2);
extern void sys_draw_rounded_clipped(int x, int y, int w, int h, int r, u32 c, int cx1, int cy1, int cx2, int cy2);
extern void sys_draw_letter_clipped(int x, int y, u8 c, u32 color, int scale, int cx1, int cy1, int cx2, int cy2);
extern int sys_get_glyph_advance(u8 c, int scale);

static void str_copy(char *d, const char *s, int max_len) {
    int i = 0;
    while (*s && i < max_len - 1) {
        d[i++] = *s++;
    }
    d[i] = '\0';
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int str_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++;
        prefix++;
    }
    return 1;
}

static void render_text(int x, int y, const char *s, u32 color, int scale, int cx1, int cy1, int cx2, int cy2) {
    while (*s) {
        u8 c = (u8)*s++;
        if (c >= 32 && c < 127) {
            sys_draw_letter_clipped(x, y, c, color, scale, cx1, cy1, cx2, cy2);
            x += sys_get_glyph_advance(c, scale);
        }
    }
}

void browser_mark_dirty(void) {
    g_browser.layout_dirty = 1;
}

void browser_init(void) {
    memset(&g_browser, 0, sizeof(BrowserApp));
    g_browser.x = 170;
    g_browser.y = 120;
    g_browser.w = 680;
    g_browser.h = 410;
    str_copy(g_browser.title, "PollikOS Web", sizeof(g_browser.title));
    str_copy(g_browser.status_msg, "Ready", sizeof(g_browser.status_msg));
    str_copy(g_browser.url, "about:home", sizeof(g_browser.url));
    str_copy(g_browser.input_url, "about:home", sizeof(g_browser.input_url));
    g_browser.input_cursor = strlen(g_browser.input_url);
    g_browser.input_anchor = -1;
    g_browser.focused_anchor = -1;
    js_init();
}

void browser_open(void) {
    g_browser.open = 1;
    g_browser.minimized = 0;
    if (!g_browser.document && !g_browser.is_loading) {
        browser_navigate(g_browser.url[0] ? g_browser.url : "about:home");
    }
}

void browser_close(void) {
    g_browser.open = 0;
    g_browser.minimized = 0;
    g_browser.has_pending_navigation = 0;
    if (!load_active) g_browser.is_loading = 0;
    close_pending=1;
    if(load_active)http_cancel_current();
    else finish_closed_session();
}
static void finish_closed_session(void) {
    int x=g_browser.x,y=g_browser.y,w=g_browser.w,h=g_browser.h;
    int reopen=g_browser.open;
    DomNode *old=g_browser.document;g_browser.document=0;
    dom_free_tree(old);
    browser_init();g_browser.x=x;g_browser.y=y;g_browser.w=w;g_browser.h=h;
    close_pending=0;
    if(reopen)browser_open();
}

/* The base URL all relative references resolve against. */
static const char *browser_base_url(void) {
    return g_browser.base_url[0] ? g_browser.base_url : g_browser.url;
}

/* Find <base href> and resolve it against the document URL. */
static void extract_dom_base(DomNode *node) {
    if (!node) return;
    if (node->tag[0] == 'b' && node->tag[1] == 'a' && node->tag[2] == 's' && node->tag[3] == 'e' && node->tag[4] == 0) {
        const char *href = dom_get_attribute(node, "href");
        if (href && href[0]) {
            char resolved[256];
            if (http_resolve_url(g_browser.url, href, resolved, sizeof(resolved)))
                str_copy(g_browser.base_url, resolved, sizeof(g_browser.base_url));
        }
        return;
    }
    for (DomNode *c = node->first_child; c; c = c->next_sibling) extract_dom_base(c);
}

/* Find <title> in DOM */
static void extract_dom_title(DomNode *node, char *out, int max_len) {
    if (!node) return;
    browser_work_checkpoint();
    if (node->tag[0] == 't' && node->tag[1] == 'i' && node->tag[2] == 't' && node->tag[3] == 'l' && node->tag[4] == 'e') {
        if (node->first_child && node->first_child->text) {
            str_copy(out, node->first_child->text, max_len);
            return;
        }
    }
    DomNode *ch = node->first_child;
    while (ch) {
        extract_dom_title(ch, out, max_len);
        ch = ch->next_sibling;
    }
}

/* Fetch and execute classic scripts in document order.  The interpreter is
 * deliberately bounded, while transport still goes through the regular
 * DNS/TCP/TLS/HTTP path. */
static void execute_dom_scripts(DomNode *node) {
    if (!node || close_pending) return;
    browser_work_checkpoint();
    if(close_pending)return;
    if (node->tag[0] == 's' && node->tag[1] == 'c' && node->tag[2] == 'r' && node->tag[3] == 'i' && node->tag[4] == 'p' && node->tag[5] == 't') {
        int kind=browser_script_kind(node);
        if(kind==2){++g_browser.script_errors;serial("JS: module scripts are unsupported\n");}
        if (kind==1 && node->src[0]) {
            char url[256];
            HttpResponse response;
            if (http_resolve_url(browser_base_url(), node->src, url, sizeof(url)) && http_get(url, &response)) {
                if (response.status_code == 200 && response.body && response.body_len <= 32768) {
                    char *code = kmalloc((u32)response.body_len + 1);
                    if (code) {
                        memcpy(code, response.body, response.body_len);
                        code[response.body_len] = 0;
                        js_execute(code, g_browser.document);
                        kfree(code);
                    }
                }
                http_response_free(&response);
            }
        } else if (kind==1 && node->first_child && node->first_child->text) {
            js_execute(node->first_child->text, g_browser.document);
        }
    }
    DomNode *ch = node->first_child;
    while (ch) {
        execute_dom_scripts(ch);
        ch = ch->next_sibling;
    }
}

static void load_stylesheets(DomNode *node,char *css,int *used,int *count) {
    if(!node || close_pending)return;
    browser_work_checkpoint();
    if(close_pending)return;
    /* Remote pages often include dozens of tracking stylesheets. Keeping only
     * the first two small files preserves common site styling and bounds the
     * synchronous legacy browser's network wait. */
    if(!memcmp(node->tag,"link",5)&&node->href[0]&&node->rel[0]=='s'&&node->rel[1]=='t'&&node->rel[2]=='y'&&node->rel[3]=='l'&&node->rel[4]=='e'&&*count<2&&*used<8192) {
        char url[256];HttpResponse response;(*count)++;
        if(http_resolve_url(browser_base_url(),node->href,url,sizeof(url)) && http_get_timeout(url,&response,500)) {
            if(response.status_code==200 && response.body && response.body_len<8192-*used){memcpy(css+*used,response.body,response.body_len);*used+=response.body_len;css[(*used)++]='\n';}
            http_response_free(&response);
        }
    }
    for(DomNode *c=node->first_child;c;c=c->next_sibling)load_stylesheets(c,css,used,count);
}
/* Synchronous transaction with opt-in cooperative wait/CPU safe points.
 * The host may present loading chrome and manipulate WM geometry only; it must
 * never dispatch client events or poll applications on this stack. */
static void browser_load_now(const char *raw_url) {
    if (!raw_url || !raw_url[0]) return;

    if (str_eq(raw_url, "about:home") || str_eq(raw_url, "pollik://home")) {
        str_copy(g_browser.url, "about:home", sizeof(g_browser.url));
        str_copy(g_browser.input_url, "about:home", sizeof(g_browser.input_url));
        g_browser.input_cursor = strlen(g_browser.input_url);
        g_browser.is_typing_url = 0;
        g_browser.scroll_y = 0;
        g_browser.is_loading = 0;
        g_browser.focused_input = 0;
        g_browser.base_url[0] = 0;
        js_init();
        str_copy(g_browser.title, "PollikOS Web", sizeof(g_browser.title));
        str_copy(g_browser.status_msg, "Ready", sizeof(g_browser.status_msg));

        if (g_browser.document) {
            dom_free_tree(g_browser.document);
            g_browser.document = 0;
        }
        g_browser.selection_node = 0;
        g_browser.selection_all = 0;

        static const char home_html[] =
            "<div style=\"padding: 16px 20px; font-family: sans-serif;\">"
            "<h1 style=\"color: #6a4fa3; font-size: 18px; margin-bottom: 2px;\">PollikOS Web</h1>"
            "<p style=\"color: #777; font-size: 12px; margin-bottom: 10px;\">Live Web Explorer &amp; Search</p>"
            "<form action=\"https://lite.duckduckgo.com/lite/\" method=\"get\">"
            "<input name=\"q\" type=\"text\" style=\"width:360px;\">"
            "<input type=\"submit\" value=\"Search\"></form>"
            "<p>Web results from DuckDuckGo. Ctrl+L: address or search.</p>"
            "<div style=\"background-color: #f3f0f9; padding: 10px 14px; border-radius: 8px; margin-bottom: 8px;\">"
            "<p style=\"font-weight: bold; color: #44355b; margin-bottom: 4px;\">Search Engines:</p>"
            "<p><a href=\"http://frogfind.com\">FrogFind (Google Search for Retro &amp; Hobby OS)</a></p>"
            "<p><a href=\"https://lite.duckduckgo.com/lite/\">DuckDuckGo Lite (Live Text Search)</a></p>"
            "<p><a href=\"http://wiby.me\">Wiby Search (Classic Web)</a></p>"
            "</div>"
            "<div style=\"background-color: #f7f6fa; padding: 10px 14px; border-radius: 8px;\">"
            "<p style=\"font-weight: bold; color: #44355b; margin-bottom: 4px;\">Live Websites:</p>"
            "<p><a href=\"https://google.com\">Google (Search Engine)</a></p>"
            "<p><a href=\"https://en.m.wikipedia.org\">Wikipedia Mobile (Encyclopedia)</a></p>"
            "<p><a href=\"http://neverssl.com\">NeverSSL (Fast Plain HTTP)</a></p>"
            "<p><a href=\"https://example.com\">Example Domain</a></p>"
            "</div>"
            "</div>";

        static const char home_html_dark[] =
            "<div style=\"padding: 16px 20px; font-family: sans-serif; color: #f1f5f9;\">"
            "<h1 style=\"color: #93c5fd; font-size: 18px; margin-bottom: 2px;\">PollikOS Web</h1>"
            "<p style=\"color: #94a3b8; font-size: 12px; margin-bottom: 10px;\">Live Web Explorer &amp; Search</p>"
            "<form action=\"https://lite.duckduckgo.com/lite/\" method=\"get\">"
            "<input name=\"q\" type=\"text\" style=\"width:360px;\">"
            "<input type=\"submit\" value=\"Search\"></form>"
            "<p style=\"color: #94a3b8;\">Web results from DuckDuckGo. Ctrl+L: address or search.</p>"
            "<div style=\"background-color: #141824; padding: 10px 14px; border-radius: 8px; margin-bottom: 8px;\">"
            "<p style=\"font-weight: bold; color: #e2e8f0; margin-bottom: 4px;\">Search Engines:</p>"
            "<p><a href=\"http://frogfind.com\" style=\"color: #60a5fa;\">FrogFind (Google Search for Retro &amp; Hobby OS)</a></p>"
            "<p><a href=\"https://lite.duckduckgo.com/lite/\" style=\"color: #60a5fa;\">DuckDuckGo Lite (Live Text Search)</a></p>"
            "<p><a href=\"http://wiby.me\" style=\"color: #60a5fa;\">Wiby Search (Classic Web)</a></p>"
            "</div>"
            "<div style=\"background-color: #181c2b; padding: 10px 14px; border-radius: 8px;\">"
            "<p style=\"font-weight: bold; color: #e2e8f0; margin-bottom: 4px;\">Live Websites:</p>"
            "<p><a href=\"https://google.com\" style=\"color: #60a5fa;\">Google (Search Engine)</a></p>"
            "<p><a href=\"https://en.m.wikipedia.org\" style=\"color: #60a5fa;\">Wikipedia Mobile (Encyclopedia)</a></p>"
            "<p><a href=\"http://neverssl.com\" style=\"color: #60a5fa;\">NeverSSL (Fast Plain HTTP)</a></p>"
            "<p><a href=\"https://example.com\" style=\"color: #60a5fa;\">Example Domain</a></p>"
            "</div>"
            "</div>";

        const char *chosen = ui_is_dark() ? home_html_dark : home_html;
        int chosen_len = ui_is_dark() ? (sizeof(home_html_dark) - 1) : (sizeof(home_html) - 1);
        g_browser.document = html_parse(chosen, chosen_len);
        css_apply_styles(g_browser.document, 0);
        layout_compute(g_browser.document, g_browser.w - 20, &g_browser.content_height);
        return;
    }

    char target_url[256];
    if (str_starts_with(raw_url, "http://") || str_starts_with(raw_url, "https://")) {
        str_copy(target_url, raw_url, sizeof(target_url));
    } else {
        char host_part[64];
        int hi = 0;
        while (raw_url[hi] && raw_url[hi] != '/' && raw_url[hi] != ':' && hi < 63) {
            host_part[hi] = raw_url[hi];
            hi++;
        }
        host_part[hi] = '\0';
        u8 dummy_ip[4];
        int is_ip = net_parse_ip(host_part, dummy_ip);

        if (is_ip) {
            target_url[0] = 'h'; target_url[1] = 't'; target_url[2] = 't'; target_url[3] = 'p';
            target_url[4] = ':'; target_url[5] = '/'; target_url[6] = '/';
            int i = 7, j = 0;
            while (raw_url[j] && i < 255) target_url[i++] = raw_url[j++];
            target_url[i] = '\0';
        } else {
            int has_space = 0;
            int has_dot = 0;
            for (int k = 0; raw_url[k]; k++) {
                if (raw_url[k] == ' ') has_space = 1;
                if (raw_url[k] == '.') has_dot = 1;
            }

            if (has_space || !has_dot) {
                const char *search_pfx = "https://lite.duckduckgo.com/lite/?q=";
                int ti = 0;
                while (search_pfx[ti]) { target_url[ti] = search_pfx[ti]; ti++; }
                for (int k = 0; raw_url[k] && ti < 250; k++) {
                    char ch = raw_url[k];
                    if (ch == ' ') target_url[ti++] = '+';
                    else if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                             (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')
                        target_url[ti++] = ch;
                    else {
                        static const char hex[] = "0123456789ABCDEF";
                        target_url[ti++] = '%';
                        target_url[ti++] = hex[(u8)ch >> 4];
                        target_url[ti++] = hex[(u8)ch & 15];
                    }
                }
                target_url[ti] = '\0';
            } else {
                target_url[0] = 'h'; target_url[1] = 't'; target_url[2] = 't'; target_url[3] = 'p';
                target_url[4] = 's'; target_url[5] = ':'; target_url[6] = '/'; target_url[7] = '/';
                int i = 8, j = 0;
                while (raw_url[j] && i < 255) {
                    target_url[i++] = raw_url[j++];
                }
                target_url[i] = '\0';
            }
        }
    }

    str_copy(g_browser.url, target_url, sizeof(g_browser.url));
    str_copy(g_browser.input_url, target_url, sizeof(g_browser.input_url));
    g_browser.input_cursor = strlen(g_browser.input_url);
    g_browser.is_typing_url = 0;
    g_browser.focused_input = 0;
    g_browser.focused_cursor = 0;
    g_browser.scroll_y = 0;

    /* Push history if distinct */
    if (g_browser.history_count == 0 || !str_eq(g_browser.history[g_browser.history_index], target_url)) {
        g_browser.history_count = g_browser.history_index + (g_browser.history_count ? 1 : 0);
        if (g_browser.history_count < 16) {
            g_browser.history_index = g_browser.history_count++;
        } else {
            for (int k = 0; k < 15; k++) {
                memcpy(g_browser.history[k], g_browser.history[k + 1], 256);
            }
            g_browser.history_index = 15;
        }
        str_copy(g_browser.history[g_browser.history_index], target_url, 256);
    }

    g_browser.is_loading = 1;
    str_copy(g_browser.status_msg, "Connecting...", sizeof(g_browser.status_msg));
    serial("BROWSER: Navigating to ");
    serial(target_url);
    serial("\n");

    HttpResponse resp;
    int err = http_get(target_url, &resp);
    if(close_pending){http_response_free(&resp);return;}

    if (err && !resp.error && resp.body && resp.body_len > 0) {
        if (resp.final_url[0]) {
            str_copy(g_browser.url, resp.final_url, sizeof(g_browser.url));
            str_copy(g_browser.input_url, resp.final_url, sizeof(g_browser.input_url));
            g_browser.input_cursor = strlen(g_browser.input_url);
        }
        /* Free old document */
        if (g_browser.document) {
            dom_free_tree(g_browser.document);
            g_browser.document = 0;
        }
        g_browser.selection_node = 0;
        g_browser.selection_all = 0;

        js_init();
        g_browser.script_errors = 0;
        /* Parse HTML */
        str_copy(g_browser.status_msg, "Parsing HTML...", sizeof(g_browser.status_msg));
        g_browser.document = html_parse((const char *)resp.body, resp.body_len);

        /* Resolve <base href> before loading any subresource. */
        g_browser.base_url[0] = 0;
        extract_dom_base(g_browser.document);

        /* Extract title */
        char page_title[64] = {0};
        extract_dom_title(g_browser.document, page_title, sizeof(page_title));
        if (page_title[0]) {
            str_copy(g_browser.title, page_title, sizeof(g_browser.title));
        } else {
            str_copy(g_browser.title, target_url, sizeof(g_browser.title));
        }

        /* Apply CSS styles */
        str_copy(g_browser.status_msg, "Applying CSS...", sizeof(g_browser.status_msg));
        char *external=kmalloc(16384);int used=0,count=0;
        if(external){load_stylesheets(g_browser.document,external,&used,&count);external[used]=0;}
        css_apply_styles(g_browser.document, external);kfree(external);

        browser_load_images(g_browser.document,browser_base_url());
        /* Compute Layout */
        str_copy(g_browser.status_msg, "Computing Layout...", sizeof(g_browser.status_msg));
        layout_compute(g_browser.document, g_browser.w - 20, &g_browser.content_height);

        /* Execute JavaScript */
        execute_dom_scripts(g_browser.document);

        /* Fire DOMContentLoaded and load on the document/body. */
        js_dispatch_event(g_browser.document, "DOMContentLoaded");
        js_dispatch_event(g_browser.document, "load");

        /* Free HTTP response body */
        http_response_free(&resp);

        str_copy(g_browser.status_msg, g_browser.script_errors ? "Loaded; this site's JavaScript is not fully supported" : "Done", sizeof(g_browser.status_msg));
        g_browser.is_loading = 0;
        serial("BROWSER: Page loaded successfully\n");
        serial("BROWSER title: ");serial(g_browser.title);serial("\n");
    } else {
        int failure = resp.error;
        http_response_free(&resp);
        str_copy(g_browser.status_msg, failure == 2 ? "Network / DNS unavailable" :
                 failure == 3 ? "Server connection failed" :
                 failure == 4 ? "TLS or certificate verification failed" :
                 "Incomplete or unsupported HTTP response", sizeof(g_browser.status_msg));
        str_copy(g_browser.title, "Connection Error", sizeof(g_browser.title));
        g_browser.is_loading = 0;

        /* Create error page DOM */
        if (g_browser.document) {
            dom_free_tree(g_browser.document);
            g_browser.document = 0;
        }
        g_browser.selection_node = 0;
        g_browser.selection_all = 0;
        char err_html[256];
        int l = 0;
        const char *prefix = "<div style=\"padding: 30px; font-family: sans-serif;\">"
                             "<h1 style=\"color: #d32f2f;\">Unable to load page</h1>"
                             "<p>PollikOS Browser could not connect to:</p>"
                             "<p style=\"color: #666;\">";
        while (*prefix) err_html[l++] = *prefix++;
        const char *u = target_url;
        while (*u && l < 210) err_html[l++] = *u++;
        const char *suffix = "</p></div>";
        while (*suffix) err_html[l++] = *suffix++;
        err_html[l] = '\0';

        g_browser.document = html_parse(err_html, l);
        css_apply_styles(g_browser.document, 0);
        layout_compute(g_browser.document, g_browser.w - 20, &g_browser.content_height);
    }
}

void browser_navigate(const char *url) {
    if (!url || !url[0]) return;
    str_copy(g_browser.pending_url, url, sizeof(g_browser.pending_url));
    g_browser.has_pending_navigation = 1;
    g_browser.is_loading = 1;
    str_copy(g_browser.status_msg, "Loading...", sizeof(g_browser.status_msg));
    browser_mark_dirty();
}

void browser_poll(void) {
    if (load_active) return;
    if(!g_browser.open)return;
    if (!g_browser.has_pending_navigation) {
        /* Event loop: drain due timers / rAF only when the document is stable. */
        if (!g_browser.is_loading && g_browser.document && js_has_pending_tasks()) {
            js_service_tasks();
            browser_mark_dirty();
            app_host_invalidate(APP_BROWSER);
        }
        return;
    }
    char url[sizeof(g_browser.pending_url)];
    str_copy(url, g_browser.pending_url, sizeof(url));
    g_browser.has_pending_navigation = 0;
    load_active = 1;
    NetWaitService previous = net_set_wait_service(app_host_service_loading);
    app_host_invalidate(APP_BROWSER);
    browser_work_checkpoint();
    if(!close_pending)browser_load_now(url);
    net_set_wait_service(previous);
    load_active = 0;
    if(close_pending){finish_closed_session();return;}
    /* location changes from JS stay queued; do not lose their loading state. */
    g_browser.is_loading = g_browser.has_pending_navigation;
    browser_mark_dirty();
    app_host_invalidate(APP_BROWSER);
}

/* The document has 8px padding inside the white viewport on all sides. */
static int browser_view_height(void) {
    int h = g_browser.h - 110;
    return h > 0 ? h : 1;
}
static void browser_clamp_scroll(void) {
    int max = g_browser.content_height - browser_view_height();
    if (max < 0) max = 0;
    if (g_browser.scroll_y > max) g_browser.scroll_y = max;
    if (g_browser.scroll_y < 0) g_browser.scroll_y = 0;
}
void browser_render(int is_active) {
    int wx = g_browser.x;
    int wy = g_browser.y;
    int ww = g_browser.w;
    int wh = g_browser.h;

    int clip_x1 = wx;
    int clip_y1 = wy;
    int clip_x2 = wx + ww;
    int clip_y2 = wy + wh;

    int dark = ui_is_dark();
    u32 tb_bg = dark ? 0x141824 : 0xf3f1f7;
    u32 tb_sep = dark ? 0x242b3c : 0xe2dfea;
    u32 btn_bg = dark ? 0x1c2234 : 0xe7e3ef;
    u32 btn_fg = dark ? 0x94a3b8 : 0x554c68;
    u32 bar_bg = dark ? 0x0e111a : 0xffffff;
    u32 bar_border = dark ? 0x2b344a : 0xcbc5d8;
    u32 url_fg = dark ? 0xf1f5f9 : 0x222228;
    u32 vp_bg = dark ? 0x10131d : 0xffffff;
    u32 sb_track = dark ? 0x141824 : 0xf0edf5;
    u32 sb_thumb = dark ? 0x334155 : 0xa69cb8;
    u32 status_bg = dark ? 0x10131d : 0xf4f2f8;
    u32 status_border = dark ? 0x202638 : 0xe5e2ec;
    u32 status_fg = dark ? 0x94a3b8 : 0x766c88;

    /* Recompute layout if marked dirty by JS */
    if (!load_active && !g_browser.is_loading && g_browser.layout_dirty && g_browser.document) {
        layout_compute(g_browser.document, g_browser.w - 20, &g_browser.content_height);
        browser_clamp_scroll();
        g_browser.layout_dirty = 0;
    }

    /* 1. Navigation toolbar: wy + 42 to wy + 76 */
    sys_draw_rect_clipped(wx + 1, wy + 42, ww - 2, 34, tb_bg, clip_x1, clip_y1, clip_x2, clip_y2);
    sys_draw_rect_clipped(wx + 1, wy + 75, ww - 2, 1, tb_sep, clip_x1, clip_y1, clip_x2, clip_y2);

    /* Back button [ < ] */
    sys_draw_rounded_clipped(wx + 10, wy + 47, 24, 22, 5, btn_bg, clip_x1, clip_y1, clip_x2, clip_y2);
    render_text(wx + 18, wy + 51, "<", btn_fg, 1, clip_x1, clip_y1, clip_x2, clip_y2);

    /* Forward button [ > ] */
    sys_draw_rounded_clipped(wx + 38, wy + 47, 24, 22, 5, btn_bg, clip_x1, clip_y1, clip_x2, clip_y2);
    render_text(wx + 46, wy + 51, ">", btn_fg, 1, clip_x1, clip_y1, clip_x2, clip_y2);

    /* Refresh button [ R ] */
    sys_draw_rounded_clipped(wx + 66, wy + 47, 24, 22, 5, btn_bg, clip_x1, clip_y1, clip_x2, clip_y2);
    render_text(wx + 74, wy + 51, "R", btn_fg, 1, clip_x1, clip_y1, clip_x2, clip_y2);

    /* Home button [ H ] */
    sys_draw_rounded_clipped(wx + 94, wy + 47, 24, 22, 5, btn_bg, clip_x1, clip_y1, clip_x2, clip_y2);
    render_text(wx + 101, wy + 51, "H", btn_fg, 1, clip_x1, clip_y1, clip_x2, clip_y2);

    /* Address Bar: wx + 124 to wx + 640 */
    int bar_x = wx + 124;
    int bar_w = ww - 162;
    /* Full rounded stroke (border + fill) so the corners never fade. */
    sys_draw_roundrect_stroke_clipped(bar_x, wy + 46, bar_w, 24, 6, 1, bar_border, bar_bg, clip_x1, clip_y1, clip_x2, clip_y2);

    /* Security badge icon */
    if (str_starts_with(g_browser.url, "https://") && str_eq(g_browser.status_msg, "Done")) {
        sys_draw_rounded_clipped(bar_x + 6, wy + 50, 14, 15, 3, 0x388e3c, clip_x1, clip_y1, clip_x2, clip_y2);
        render_text(bar_x + 10, wy + 51, "S", 0xffffff, 1, clip_x1, clip_y1, clip_x2, clip_y2);
    } else {
        sys_draw_rounded_clipped(bar_x + 6, wy + 50, 14, 15, 3, dark ? 0x4a3e5c : 0x757575, clip_x1, clip_y1, clip_x2, clip_y2);
        render_text(bar_x + 10, wy + 51, "i", dark ? 0xd0c4eb : 0xffffff, 1, clip_x1, clip_y1, clip_x2, clip_y2);
    }

    /* URL text */
    const char *display_url = g_browser.is_typing_url ? g_browser.input_url : g_browser.url;
    int url_shift = g_browser.is_typing_url ? sys_text_width(display_url, 1) - (bar_w - 40) : 0;
    if (url_shift < 0) url_shift = 0;
    if (g_browser.is_typing_url && g_browser.input_anchor >= 0 && g_browser.input_anchor != g_browser.input_cursor) {
        int a = g_browser.input_anchor, b = g_browser.input_cursor;
        if (a > b) { int t = a; a = b; b = t; }
        char before[256], selected[256]; int n = 0;
        while (n < a && n < (int)sizeof(before) - 1 && display_url[n]) { before[n] = display_url[n]; n++; }
        before[n] = 0;
        n = 0;
        while (a + n < b && n < (int)sizeof(selected) - 1 && display_url[a + n]) { selected[n] = display_url[a + n]; n++; }
        selected[n] = 0;
        sys_draw_rect_clipped(bar_x + 26 + sys_text_width(before, 1) - url_shift, wy + 49,
                              sys_text_width(selected, 1), 18, dark ? 0x344866 : 0xbcd8fa,
                              bar_x + 24, wy + 46, bar_x + bar_w - 10, wy + 70);
    }
    render_text(bar_x + 26 - url_shift, wy + 51, display_url, url_fg, 1, bar_x + 24, wy + 46, bar_x + bar_w - 10, wy + 70);

    /* Cursor if typing */
    if (g_browser.is_typing_url) {
        char before[256]; int prefix_len = g_browser.input_cursor;
        if (prefix_len < 0) prefix_len = 0;
        if (prefix_len > (int)sizeof(before) - 1) prefix_len = sizeof(before) - 1;
        memcpy(before, g_browser.input_url, (u32)prefix_len); before[prefix_len] = 0;
        int tw = sys_text_width(before, 1);
        int cur_x = bar_x + 26 + tw - url_shift;
        if (cur_x < bar_x + bar_w - 6) {
            sys_draw_rect_clipped(cur_x, wy + 50, 1, 16, dark ? 0xb89bed : 0x403060, clip_x1, clip_y1, clip_x2, clip_y2);
        }
    }

    /* Menu button [ : ] */
    sys_draw_rounded_clipped(wx + ww - 32, wy + 47, 22, 22, 5, btn_bg, clip_x1, clip_y1, clip_x2, clip_y2);
    render_text(wx + ww - 24, wy + 51, ":", btn_fg, 1, clip_x1, clip_y1, clip_x2, clip_y2);

    /* 2. Web Viewport area: wy + 76 to wy + wh - 22 */
    int vp_x = wx + 2;
    int vp_y = wy + 76;
    int vp_w = ww - 4;
    int vp_h = wh - 98;

    /* Viewport background */
    sys_draw_rect_clipped(vp_x, vp_y, vp_w, vp_h, vp_bg, clip_x1, clip_y1, clip_x2, clip_y2);

    /* Never traverse/layout a partially freed, parsed or script-mutated DOM.
     * The clip height matches browser_view_height() so scroll clamping and
     * painting agree on the visible document area. */
    if (!load_active && !g_browser.is_loading && g_browser.document) {
        render_dom(g_browser.document, vp_x + 8, vp_y + 8, vp_w - 16, browser_view_height(), g_browser.scroll_y);
    }

    /* Scrollbar if content exceeds viewport */
    if (!load_active && !g_browser.is_loading && g_browser.content_height > browser_view_height()) {
        int sb_x = wx + ww - 8;
        int sb_w = 4;
        int sb_h = vp_h;
        sys_draw_rect_clipped(sb_x, vp_y, sb_w, sb_h, sb_track, clip_x1, clip_y1, clip_x2, clip_y2);

        int thumb_h = (vp_h * browser_view_height()) / g_browser.content_height;
        if (thumb_h < 20) thumb_h = 20;
        int max_scroll = g_browser.content_height - browser_view_height();
        int thumb_y = vp_y + (g_browser.scroll_y * (vp_h - thumb_h)) / (max_scroll > 0 ? max_scroll : 1);
        sys_draw_rounded_clipped(sb_x, thumb_y, sb_w, thumb_h, 2, sb_thumb, clip_x1, clip_y1, clip_x2, clip_y2);
    }

    /* 3. Opaque bottom strip; the host applies common radius-12 coverage. */
    if (load_active || g_browser.is_loading) {
        sys_draw_rect_clipped(wx + 2, wy + 76, ww - 4, 3, 0x8062ba, clip_x1, clip_y1, clip_x2, clip_y2);
        sys_draw_rounded_clipped(wx + (ww - 260) / 2, wy + 105, 260, 34, 8, dark ? 0x241c34 : 0xf3f0f9, clip_x1, clip_y1, clip_x2, clip_y2);
        render_text(wx + (ww - 260) / 2 + 20, wy + 113, "Loading page...", dark ? 0xd0c4eb : 0x554c68, 1, clip_x1, clip_y1, clip_x2, clip_y2);
    }
    sys_draw_rect_clipped(wx, wy + wh - 22, ww, 22, status_bg, clip_x1, clip_y1, clip_x2, clip_y2);
    sys_draw_rect_clipped(wx + 2, wy + wh - 22, ww - 4, 1, status_border, clip_x1, clip_y1, clip_x2, clip_y2);
    const char *status = g_browser.hover_url[0] ? g_browser.hover_url : g_browser.status_msg;
    render_text(wx + 14, wy + wh - 18, status, status_fg, 1, wx + 14, wy + wh - 20, wx + ww - 14, wy + wh - 3);

    (void)is_active;
}

static DomNode *find_node_at(DomNode *node, int click_x, int click_y) {
    if (!node || node->style.display == DISPLAY_NONE) return 0;

    /* Check children from last to first (topmost) */
    DomNode *ch = node->last_child;
    while (ch) {
        DomNode *found = find_node_at(ch, click_x, click_y);
        if (found) return found;
        ch = ch->prev_sibling;
    }

    int x1 = node->box.x;
    int y1 = node->box.y;
    int x2 = x1 + node->box.w;
    int y2 = y1 + node->box.h;

    if (click_x >= x1 && click_x <= x2 && click_y >= y1 && click_y <= y2) {
        return node;
    }
    return 0;
}

int browser_cursor(int x,int y) {
    if (load_active || g_browser.is_loading) return 0;
    int wx=g_browser.x,wy=g_browser.y;
    if(x<wx || x>=wx+g_browser.w || y<wy || y>=wy+g_browser.h)return 0;
    if(y>=wy+46 && y<wy+70)return x>=wx+124 && x<wx+g_browser.w-38?1:2;
    if(x<wx+10 || x>=wx+g_browser.w-10 || y<wy+84 || y>=wy+g_browser.h-26)return 0;
    DomNode *n=find_node_at(g_browser.document,x-wx-10,y-wy-84+g_browser.scroll_y);
    for(;n;n=n->parent) {
        if(n->href[0] || str_eq(n->tag,"button"))return 2;
        if(str_eq(n->tag,"input"))return str_eq(n->input_type,"submit")?2:1;
    }
    return 0;
}

void browser_submit_form(DomNode *node) {
    if (load_active || g_browser.is_loading || !node || !g_browser.document) return;

    /* Find enclosing <form> */
    DomNode *form = node;
    while (form) {
        if (form->tag[0] == 'f' && form->tag[1] == 'o' && form->tag[2] == 'r' && form->tag[3] == 'm') break;
        form = form->parent;
    }
    if (!form) form = g_browser.document;

    const char *action = form->action[0] ? form->action : "";

    /* Collect input values */
    char query[512];
    int qlen = 0;

    DomNode *stack[64];
    int top = 0;
    stack[top++] = form;

    while (top > 0) {
        DomNode *curr = stack[--top];
        if (curr->tag[0] == 'i' && curr->tag[1] == 'n' && curr->tag[2] == 'p' && curr->name[0]) {
            int is_sub = (curr->input_type[0] == 's' && curr->input_type[1] == 'u' && curr->input_type[2] == 'b');
            if (!is_sub || curr == node) {
                if (qlen > 0 && qlen < 500) query[qlen++] = '&';
                for (int k = 0; curr->name[k] && qlen < 500; k++) query[qlen++] = curr->name[k];
                if (qlen < 500) query[qlen++] = '=';
                for (int k = 0; curr->value[k] && qlen < 500; k++) {
                    char c = curr->value[k];
                    if (c == ' ') query[qlen++] = '+';
                    else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.') {
                        query[qlen++] = c;
                    } else {
                        static const char hex[] = "0123456789ABCDEF";
                        if (qlen < 498) {
                            query[qlen++] = '%';
                            query[qlen++] = hex[((u8)c >> 4) & 15];
                            query[qlen++] = hex[(u8)c & 15];
                        }
                    }
                }
            }
        }
        DomNode *ch = curr->first_child;
        while (ch && top < 60) {
            stack[top++] = ch;
            ch = ch->next_sibling;
        }
    }
    query[qlen] = '\0';

    char target[256];
    if (http_resolve_url(browser_base_url(), action, target, sizeof(target))) {
        char full_url[300];
        int tlen = strlen(target);
        if (tlen + qlen + 2 < (int)sizeof(full_url)) {
            memcpy(full_url, target, tlen);
            int has_q = 0;
            for (int k = 0; k < tlen; k++) {
                if (target[k] == '?') { has_q = 1; break; }
            }
            full_url[tlen++] = has_q ? '&' : '?';
            memcpy(full_url + tlen, query, qlen);
            full_url[tlen + qlen] = '\0';
            serial("BROWSER: Submitting form to ");
            serial(full_url);
            serial("\n");
            browser_navigate(full_url);
        }
    }
}

void browser_handle_click(int mx, int my) {
    if (load_active || g_browser.is_loading) return;
    int wx = g_browser.x;
    int wy = g_browser.y;
    int ww = g_browser.w;

    /* Toolbar clicks: wy + 42 to wy + 76 */
    if (my >= wy + 42 && my <= wy + 76) {
        /* Back button */
        if (mx >= wx + 10 && mx <= wx + 34) {
            if (g_browser.history_index > 0) {
                g_browser.history_index--;
                browser_navigate(g_browser.history[g_browser.history_index]);
            }
            return;
        }
        /* Forward button */
        if (mx >= wx + 38 && mx <= wx + 62) {
            if (g_browser.history_index < g_browser.history_count - 1) {
                g_browser.history_index++;
                browser_navigate(g_browser.history[g_browser.history_index]);
            }
            return;
        }
        /* Refresh button */
        if (mx >= wx + 66 && mx <= wx + 90) {
            browser_navigate(g_browser.url);
            return;
        }
        /* Home button */
        if (mx >= wx + 94 && mx <= wx + 118) {
            browser_navigate("about:home");
            return;
        }
        /* Address bar */
        int bar_x = wx + 124;
        int bar_w = ww - 162;
        if (mx >= bar_x && mx <= bar_x + bar_w) {
            g_browser.is_typing_url = 1;
            g_browser.focused_input = 0;
            g_browser.input_anchor = -1;
            str_copy(g_browser.input_url, g_browser.url, sizeof(g_browser.input_url));
            g_browser.input_cursor = strlen(g_browser.input_url);
            return;
        }
    }

    /* Viewport clicks: wy + 76 to wy + 392 */
    if (my >= wy + 84 && my < wy + g_browser.h - 26 && mx >= wx + 10 && mx < wx + ww - 10) {
        g_browser.is_typing_url = 0;

        int doc_x = mx - (wx + 2 + 8);
        int doc_y = my - (wy + 76 + 8) + g_browser.scroll_y;

        DomNode *hit = find_node_at(g_browser.document, doc_x, doc_y);
        if (hit) {
            /* Check if clicked node or ancestor has an <a> link */
            DomNode *p = hit;
            while (p) {
                if (p->tag[0] == 'a' && p->tag[1] == '\0' && p->href[0]) {
                    serial("BROWSER: Clicked link ");
                    serial(p->href);
                    serial("\n");
                    char target[256];
                    if(http_resolve_url(browser_base_url(),p->href,target,sizeof(target)))browser_navigate(target);
                    g_browser.selection_node = 0; g_browser.selection_all = 0; g_browser.selection_drag = 0;
                    return;
                }
                p = p->parent;
            }

            /* Check if clicked an input or button */
            if (hit->tag[0] == 'i' && hit->tag[1] == 'n' && hit->tag[2] == 'p' && hit->tag[3] == 'u') {
                int is_submit = (hit->input_type[0] == 's' && hit->input_type[1] == 'u' && hit->input_type[2] == 'b') ||
                                (hit->input_type[0] == 'b' && hit->input_type[1] == 'u' && hit->input_type[2] == 't');
                if (is_submit) {
                    browser_submit_form(hit);
                    g_browser.selection_node = 0; g_browser.selection_all = 0; g_browser.selection_drag = 0;
                    return;
                } else {
                    g_browser.focused_input = hit;
                    g_browser.focused_cursor = strlen(hit->value);
                    g_browser.focused_anchor = -1;
                    g_browser.selection_node = 0; g_browser.selection_all = 0; g_browser.selection_drag = 0;
                    return;
                }
            } else if (hit->tag[0] == 'b' && hit->tag[1] == 'u' && hit->tag[2] == 't' && hit->tag[3] == 't') {
                browser_submit_form(hit);
                g_browser.selection_node = 0; g_browser.selection_all = 0; g_browser.selection_drag = 0;
                return;
            }

            g_browser.focused_input = 0;
            g_browser.focused_anchor = -1;
            if (hit->type == NODE_TEXT) {
                g_browser.selection_node = hit;
                g_browser.selection_anchor = g_browser.selection_focus = browser_text_offset_at(hit, doc_x, doc_y);
                g_browser.selection_drag = 1;
                g_browser.selection_all = 0;
            } else {
                g_browser.selection_node = 0;
                g_browser.selection_drag = 0;
                g_browser.selection_all = 0;
            }

            /* Dispatch JS mouse events with real viewport coordinates. */
            js_set_event_pos(mx - (wx + 2 + 8), my - (wy + 76 + 8) + g_browser.scroll_y);
            js_dispatch_event(hit, "mousedown");
            js_dispatch_event(hit, "mouseup");
            js_dispatch_event(hit, "click");
            js_dispatch_event(hit, "dblclick");
        } else {
            g_browser.focused_input = 0;
            g_browser.focused_anchor = -1;
            g_browser.selection_node = 0;
            g_browser.selection_drag = 0;
            g_browser.selection_all = 0;
        }
    }
}

void browser_focus_address(void) {
    if (load_active || g_browser.is_loading) return;
    g_browser.is_typing_url = 1;
    g_browser.focused_input = 0;
    g_browser.input_cursor = 0;
    g_browser.input_anchor = -1;
    g_browser.input_url[0] = 0;
}
static int browser_selection_exists(void) {
    return g_browser.selection_node && g_browser.selection_anchor != g_browser.selection_focus;
}
static void browser_collect_text(DomNode *node, char *out, int cap, int *used) {
    if (!node || *used >= cap - 1 || node->style.display == DISPLAY_NONE) return;
    if (node->type == NODE_TEXT && node->text) {
        const char *p = node->text;
        while (*p && *used < cap - 2) out[(*used)++] = *p++;
        if (*used < cap - 2) out[(*used)++] = '\n';
    }
    for (DomNode *child = node->first_child; child && *used < cap - 1; child = child->next_sibling)
        browser_collect_text(child, out, cap, used);
}
static void browser_copy_page(void) {
    char text[1024]; int used = 0;
    browser_collect_text(g_browser.document, text, sizeof(text), &used);
    app_clipboard_copy(text, used);
}
static void browser_copy_selected(void) {
    if (g_browser.selection_all) { browser_copy_page(); return; }
    if (!browser_selection_exists()) return;
    int a = g_browser.selection_anchor, b = g_browser.selection_focus;
    if (a > b) { int t = a; a = b; b = t; }
    int length = (int)strlen(g_browser.selection_node->text);
    if (a < 0) a = 0;
    if (b > length) b = length;
    app_clipboard_copy(g_browser.selection_node->text + a, b - a);
}
static void browser_edit_delete(char *buf, int *length, int *cursor, int *anchor) {
    if (*anchor >= 0 && *anchor != *cursor) {
        int a = *anchor, b = *cursor;
        if (a > b) { int t = a; a = b; b = t; }
        memmove(buf + a, buf + b, (u32)(*length - b + 1));
        *length -= b - a; *cursor = a; *anchor = -1;
    } else *anchor = -1;
}
static int browser_edit_insert(char *buf, int *length, int capacity, int *cursor, int *anchor,
                               const char *source, int source_length) {
    browser_edit_delete(buf, length, cursor, anchor);
    if (source_length > capacity - 1 - *length) source_length = capacity - 1 - *length;
    if (source_length <= 0) return 0;
    memmove(buf + *cursor + source_length, buf + *cursor, (u32)(*length - *cursor + 1));
    memcpy(buf + *cursor, source, (u32)source_length);
    *cursor += source_length; *length += source_length;
    return 1;
}
static void browser_fire_input_events(DomNode *input, int key) {
    if (!input) return;
    js_set_event_key(key);
    js_dispatch_event(input, "keydown");
    js_dispatch_event(input, "input");
    js_dispatch_event(input, "keyup");
}
void browser_handle_key_ex(u8 scancode, char ch, int shift, int control) {
    if (load_active || g_browser.is_loading) return;
    char *buf = 0;
    int *cursor = 0, *anchor = 0, capacity = 0, length = 0;
    DomNode *field = 0;
    if (g_browser.is_typing_url) {
        buf = g_browser.input_url; cursor = &g_browser.input_cursor; anchor = &g_browser.input_anchor;
        capacity = sizeof(g_browser.input_url); length = (int)strlen(buf);
    } else if (g_browser.focused_input) {
        field = g_browser.focused_input; buf = field->value; cursor = &g_browser.focused_cursor;
        anchor = &g_browser.focused_anchor; capacity = sizeof(field->value); length = (int)strlen(buf);
    }

    if (control && scancode == 30) {
        if (buf) { *anchor = 0; *cursor = length; }
        else { g_browser.selection_all = 1; g_browser.selection_node = 0; }
        return;
    }
    if (control && scancode == 46) {
        if (buf) {
            if (*anchor >= 0 && *anchor != *cursor) {
                int a = *anchor, b = *cursor; if (a > b) { int t = a; a = b; b = t; }
                app_clipboard_copy(buf + a, b - a);
            } else app_clipboard_copy(buf, length);
        } else browser_copy_selected();
        return;
    }
    if (control && scancode == 45 && buf) {
        if (*anchor >= 0 && *anchor != *cursor) {
            int a = *anchor, b = *cursor; if (a > b) { int t = a; a = b; b = t; }
            app_clipboard_copy(buf + a, b - a);
            browser_edit_delete(buf, &length, cursor, anchor);
            if (field) browser_fire_input_events(field, 8);
        }
        return;
    }
    if (control && scancode == 47 && buf) {
        char pasted[1024]; int n = app_clipboard_paste(pasted, sizeof(pasted)), kept = 0;
        for (int i = 0; i < n; i++) if (pasted[i] >= 32 && pasted[i] < 127) pasted[kept++] = pasted[i];
        if (browser_edit_insert(buf, &length, capacity, cursor, anchor, pasted, kept) && field)
            browser_fire_input_events(field, 'v');
        return;
    }

    if (buf) {
        if (scancode == 0x1C || ch == '\n' || ch == '\r') {
            if (field) browser_submit_form(field);
            else { g_browser.is_typing_url = 0; *anchor = -1; browser_navigate(buf); }
        } else if (scancode == 0x01) {
            if (g_browser.is_typing_url) {
                g_browser.is_typing_url = 0;
                str_copy(g_browser.input_url, g_browser.url, sizeof(g_browser.input_url));
                g_browser.input_cursor = (int)strlen(g_browser.input_url); g_browser.input_anchor = -1;
            } else { g_browser.focused_input = 0; g_browser.focused_anchor = -1; }
        } else if (scancode == 75 || scancode == 77 || scancode == 71 || scancode == 79) {
            int next = scancode == 75 ? *cursor - 1 : scancode == 77 ? *cursor + 1 : scancode == 71 ? 0 : length;
            if (next < 0) next = 0; if (next > length) next = length;
            if (shift) { if (*anchor < 0) *anchor = *cursor; } else *anchor = -1;
            *cursor = next;
        } else if (scancode == 0x0E || scancode == 83 || ch == '\b') {
            int old_length = length;
            if (*anchor >= 0 && *anchor != *cursor) browser_edit_delete(buf, &length, cursor, anchor);
            else if (scancode == 0x0E && *cursor > 0) {
                memmove(buf + *cursor - 1, buf + *cursor, (u32)(length - *cursor + 1));
                --*cursor; --length; *anchor = -1;
            } else if (*cursor < length) {
                memmove(buf + *cursor, buf + *cursor + 1, (u32)(length - *cursor));
                --length; *anchor = -1;
            }
            if (field && length != old_length) browser_fire_input_events(field, 8);
        } else if (ch >= 32 && ch < 127 && !control) {
            if (browser_edit_insert(buf, &length, capacity, cursor, anchor, &ch, 1) && field)
                browser_fire_input_events(field, (int)(u8)ch);
        }
        return;
    }
    if (control && scancode == 46) return;
}
void browser_handle_key(u8 scancode, char ch) { browser_handle_key_ex(scancode, ch, 0, 0); }

int browser_drag(int x, int y, int active) {
    if (!active) { g_browser.selection_drag = 0; return 0; }
    if (!g_browser.selection_drag || !g_browser.selection_node) return -1;
    int offset = browser_text_offset_at(g_browser.selection_node, x - 10,
                                        y - 84 + g_browser.scroll_y);
    if (offset == g_browser.selection_focus) return 0;
    g_browser.selection_focus = offset;
    g_browser.selection_all = 0;
    return 1;
}

void browser_handle_scroll(int delta) {
    if (load_active || g_browser.is_loading) return;
    browser_clamp_scroll();
    int max = g_browser.content_height - browser_view_height();
    if (max < 0) max = 0;
    if (delta > max - g_browser.scroll_y) g_browser.scroll_y = max;
    else if (delta < -g_browser.scroll_y) g_browser.scroll_y = 0;
    else g_browser.scroll_y += delta;
}
