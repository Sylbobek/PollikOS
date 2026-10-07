/* Native transaction regression: real browser, parser, CSS, layout and wait
 * service; mock HTTP/host/raster/JS. This is NOT an end-to-end transport test. */
#include "../kernel/browser/browser.h"
#include "../kernel/gui/apps.h"
#include "../kernel/net/http.h"
#include "../kernel/net/net_util.h"
extern int printf(const char *, ...);
extern void *malloc(__SIZE_TYPE__);
extern void free(void *);
extern void exit(int);
volatile u32 ticks;
static int services, requests, dom_paints, layouts_during_service;
static int in_service, queue_navigation, close_loading, fail_http;
static int allocations, view_w, view_h, view_scroll, status_strip, url_cursor, thumb_bottom;
static int script_calls;
static const char *override_page;
static int live_allocations,http_cancellations;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
void *kmalloc(u32 n) { ++ticks; ++allocations; void *p=malloc(n);if(p)++live_allocations;return p; }
void kfree(void *p) { if(p)--live_allocations;free(p); }
void http_cancel_current(void){++http_cancellations;}
void serial(const char *s) { (void)s; }
void number(char *s, u32 n) { int k=0; char b[12]; do {b[k++]='0'+n%10;n/=10;}while(n);int i=0;while(k)s[i++]=b[--k];s[i]=0; }
void js_init(void) {}
void js_execute(const char *s, DomNode *d) {(void)s;(void)d;++script_calls;}
void js_dispatch_event(DomNode *n, const char *s) {(void)n;(void)s;CHECK(!in_service);}
void js_set_event_pos(int x, int y) {(void)x;(void)y;}
void js_set_event_key(int k) {(void)k;}
void js_service_tasks(void) {}
/* This transaction harness deliberately mocks raster/media services. Text
 * selection and animated media are covered by the QEMU browser harness. */
int browser_text_offset_at(DomNode *node,int x,int y) {
    (void)x;(void)y;CHECK(!node);return 0;
}
int browser_advance_media(DomNode *node) {
    for (;node;node=node->next_sibling) {
        CHECK(!node->gif);
        browser_advance_media(node->first_child);
    }
    return 0; /* HTTP fixture has no images; do not invent media progress. */
}
int js_has_pending_tasks(void) {return 0;}
void browser_load_images(DomNode *n,const char *s) {(void)n;(void)s;browser_work_checkpoint();}
void render_dom(DomNode *n,int x,int y,int w,int h,int scroll) {
    (void)n;
    CHECK(x==g_browser.x+10 && y==g_browser.y+84);
    CHECK(w==g_browser.w-20 && h==g_browser.h-110);
    view_w=w;view_h=h;view_scroll=scroll;
    CHECK(!in_service); ++dom_paints;
}
int sys_text_width(const char *s,int scale) {
    if(in_service) {
        /* Layout measures single spaces; loading chrome never does. */
        if (s[0]==' ' && !s[1]) ++layouts_during_service;
    }
    return strlen(s)*6*scale;
}
int sys_get_glyph_advance(u8 c,int scale) {(void)c;return 6*scale;}
void sys_draw_rect_clipped(int x,int y,int w,int h,u32 c,int a,int b,int d,int e) {
    (void)a;(void)b;(void)d;(void)e;
    if(c==0xf4f2f8) {
        CHECK(x==g_browser.x && y==g_browser.y+g_browser.h-22);
        CHECK(w==g_browser.w && h==22); ++status_strip;
    }
    if(c==0x403060) {
        CHECK(x>=g_browser.x+150 && x<g_browser.x+g_browser.w-48);
        CHECK(y==g_browser.y+50 && w==1 && h==16); ++url_cursor;
    }
    if(c==0xa69cb8) thumb_bottom=y+h;
}
void sys_draw_rounded_clipped(int x,int y,int w,int h,int r,u32 c,int a,int b,int d,int e) {
    (void)r;sys_draw_rect_clipped(x,y,w,h,c,a,b,d,e);
}
void sys_draw_roundrect_stroke_clipped(int x,int y,int w,int h,int r,int bw,u32 border,u32 fill,int a,int b,int d,int e) {
    (void)r;(void)bw;(void)border;
    sys_draw_rect_clipped(x,y,w,h,fill,a,b,d,e);
}
int ui_is_dark(void) {return 0;}
void sys_draw_letter_clipped(int x,int y,u8 c,u32 color,int scale,int a,int b,int d,int e) {
    (void)c;(void)scale;sys_draw_rect_clipped(x,y,1,1,color,a,b,d,e);
}
void sys_draw_window_bottom(int x,int y,int w,int h,int r,u32 c,int start) {
    (void)x;(void)y;(void)w;(void)h;(void)r;(void)c;(void)start;
}
void app_host_invalidate(int id) {(void)id;}
void app_host_service_loading(void) {
    CHECK(!in_service);in_service=1;++services;
    int before=requests, paints=dom_paints;
    browser_poll(); /* Must not enter a second load, even if one is queued. */
    browser_client_resized(services%2 ? 480 : 1200,services%2 ? 280 : 800);
    int allocated=allocations;
    browser_render(1);
    CHECK(allocations==allocated);
    CHECK(dom_paints==paints && requests==before);
    int typing=g_browser.is_typing_url, input_cursor=g_browser.input_cursor;
    DomNode *focused=g_browser.focused_input;
    browser_client_key(38,'l',0,1);
    CHECK(g_browser.is_typing_url==typing && g_browser.input_cursor==input_cursor && g_browser.focused_input==focused);
    CHECK(browser_cursor(g_browser.x+30,g_browser.y+100)==0);
    int scroll=g_browser.scroll_y;
    browser_handle_scroll(40); browser_handle_click(g_browser.x+100,g_browser.y+52);
    browser_handle_key(0x1c,'\n');
    CHECK(g_browser.scroll_y==scroll);
    ++ticks;net_service_wait(); /* Callback recursion must be rejected. */
    if(queue_navigation) {queue_navigation=0;browser_navigate("about:home");browser_poll();CHECK(requests==before);}
    if(close_loading) {close_loading=0;browser_close();}
    in_service=0;
}
void http_response_free(HttpResponse *r) {kfree(r->body);r->body=0;}
int http_resolve_url(const char *base,const char *ref,char *out,int cap) {
    (void)base;if((int)strlen(ref)>=cap)return 0;memcpy(out,ref,strlen(ref)+1);return 1;
}
int http_get(const char *url,HttpResponse *r) {
    (void)url;++requests;memset(r,0,sizeof(*r));
    for(int i=0;i<25;i++){++ticks;net_service_wait();}
    if(fail_http){r->error=3;return 0;}
    const char page[]="<html><head><title>Native PASS</title><style>p {color:red;}</style></head><body><p>Safe cooperative load</p></body></html>";
    const char *body=override_page?override_page:page;
    r->body_len=strlen(body);r->status_code=200;r->body=kmalloc(r->body_len+1);memcpy(r->body,body,r->body_len+1);return 1;
}
int http_get_timeout(const char *url,HttpResponse *r,u32 timeout) {
    CHECK(timeout>0);return http_get(url,r);
}
static void check_loaded(void) {
    CHECK(!g_browser.is_loading && !g_browser.has_pending_navigation);
    CHECK(g_browser.document && services>0 && !layouts_during_service);
    int before=dom_paints;browser_render(1);CHECK(dom_paints==before+1);
    before=services;++ticks;net_service_wait();CHECK(services==before);
}
static void check_viewports(void) {
    static const int sizes[][2]={{480,280},{680,410},{1200,800},{480,800}};
    for(unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++) {
        int allocated=allocations, serviced=services;
        browser_client_resized(sizes[i][0],sizes[i][1]);
        g_browser.scroll_y=1000000;
        status_strip=0;
        browser_render(1);
        CHECK(!g_browser.layout_dirty && view_w==sizes[i][0]-20 && view_h==sizes[i][1]-110);
        int max=g_browser.content_height-view_h;if(max<0)max=0;
        CHECK(view_scroll==max && g_browser.scroll_y==max && status_strip==1);
        browser_handle_scroll(-2147483647-1);CHECK(g_browser.scroll_y==0);
        browser_handle_scroll(2147483647);CHECK(g_browser.scroll_y==max);
        browser_render(1);
        if(max>0) CHECK(thumb_bottom==g_browser.y+g_browser.h-22);
        browser_focus_address();
        for(int j=0;j<250;j++) browser_handle_key(30,'w');
        CHECK(g_browser.input_cursor==250);
        url_cursor=0;browser_render(1);CHECK(url_cursor==1);
        CHECK(allocations==allocated && services==serviced);
        g_browser.is_typing_url=0;
    }
}
int main(void) {
    browser_client_init();browser_open();browser_poll();check_loaded();CHECK(requests==0);
    check_viewports();
    browser_navigate("http://127.0.0.1/");queue_navigation=1;browser_poll();
    CHECK(requests==1 && g_browser.has_pending_navigation && g_browser.is_loading);
    CHECK(!memcmp(g_browser.title,"Native PASS",12));
    browser_poll();check_loaded();CHECK(requests==1); /* Queued home, not nested HTTP. */
    browser_navigate("http://127.0.0.1/");fail_http=1;browser_poll();check_loaded();
    fail_http=0;browser_navigate("http://127.0.0.1/");close_loading=1;browser_poll();
    /* Closing now ends the browser session, per the requested lifecycle:
     * replace the old retained-document assumption with exact reclamation. */
    CHECK(!g_browser.open && !g_browser.document && !g_browser.is_loading);
    CHECK(!g_browser.has_pending_navigation && !live_allocations && http_cancellations==1);
    browser_open();browser_poll();check_loaded();
    CHECK(!memcmp(g_browser.url,"about:home",11));
    override_page="<html><body><script type='application/ld+json'>{\"name\":\"metadata\"}</script>"
                  "<script type='application/json'>{\"name\":\"config\"}</script>"
                  "<script type='text/plain'>not JavaScript</script>"
                  "<script type='module'>import x from 'x';</script>"
                  "<script>let classic=1;</script>"
                  "<script type='TeXt/JaVaScRiPt'>let classic=2;</script></body></html>";
    script_calls=0;browser_open();browser_navigate("http://127.0.0.1/");browser_poll();
    CHECK(script_calls==2);
    CHECK(g_browser.script_errors==1); /* module remains explicitly unsupported */
    printf("PASS: script types: two classic scripts, JSON/data untouched, module reported unsupported\n");
    printf("PASS: %d cooperative services; home/success/error/close, no nested load or mutable DOM painting/layout/input\n",services);
    dom_free_tree(g_browser.document);return 0;
}
