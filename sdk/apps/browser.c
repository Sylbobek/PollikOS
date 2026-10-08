#define POLLIK_BROWSER_STANDALONE 1
#include "../../kernel/browser/browser.h"
#include "../../kernel/browser/script_type.h"
#include <pollikos/window.h>
#include <pollikos/fs.h>
#include <pollikos/net.h>
#include <pollikos/time.h>
#include <pollikos/image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "window_ui.h"
#include "browser_start.h"
#ifndef POLLIK_BROWSER_ELK
#include "browser_js_backend.h"
#endif

#define BROWSER_W 920
#define BROWSER_H 640
#define HTML_LIMIT (2u * 1024u * 1024u)
#define CSS_LIMIT (WEB_MAX_CSS_SIZE - 1)
#define ADDRESS_LIMIT 1023
#define HTTP_READ_CHUNK 4096
#define REMOTE_ASSET_MAX 64
#define IMAGE_LIMIT (4u*1024u*1024u)

BrowserApp g_browser;
static PollikCanvas canvas;
static DomNode *document;
static char address[ADDRESS_LIMIT + 1] = "about:home";
static int address_editing;
static int scroll_y;
static int page_height;
static int view_x, view_y, view_w, view_h;
static char status[96] = "Ready";
static char pending_navigation[ADDRESS_LIMIT+1];
static char bookmarks[32][ADDRESS_LIMIT+1];static unsigned bookmark_count;
static char *page_source;static unsigned page_source_length;
static int document_ready;
static char page_history[16][ADDRESS_LIMIT + 1];
static unsigned history_count,history_index;
static long http_handle=-1;
static char *http_document;
static unsigned http_document_length;
static char http_url[ADDRESS_LIMIT+1];
static int http_loading;
typedef struct { char url[ADDRESS_LIMIT+1]; DomNode *inline_script; } RemoteScript;
static char remote_styles[REMOTE_ASSET_MAX][ADDRESS_LIMIT+1];
static RemoteScript remote_scripts[REMOTE_ASSET_MAX];
static unsigned remote_style_count,remote_script_count,remote_asset_index;
static DomNode *remote_images[REMOTE_ASSET_MAX];
static char remote_image_urls[REMOTE_ASSET_MAX][ADDRESS_LIMIT+1],document_url[ADDRESS_LIMIT+1];
static unsigned remote_image_count,http_limit=HTML_LIMIT;
static char remote_css[CSS_LIMIT+1];
static unsigned remote_css_length;
static int http_request_kind,remote_resource_stage;
extern void browser_js_init(DomNode *document);
extern void browser_js_execute(const char *code);
extern int browser_js_dispatch_click(DomNode *node,int x,int y);
extern const char *browser_js_title(void);
static void render_page(void);
static int resolve_web_reference(const char *base,const char *ref,char *out,unsigned cap);
static int prepare_remote_document(const char *page,const char *html,int n);
static void pump_remote_resources(void);
static int navigate_to(const char *path,int remember);
const char *browser_current_url(void){return address;}
int browser_resolve_url(const char *reference,char *out,unsigned capacity) {
    return pollikos_url_resolve(document_url[0]?document_url:address,reference,out,capacity);
}
void browser_request_navigation(const char *url){(void)browser_resolve_url(url,pending_navigation,sizeof(pending_navigation));}

void *kmalloc(u32 size) { return malloc(size ? size : 1); }
void kfree(void *ptr) { free(ptr); }
void browser_work_checkpoint(void) {}
void browser_mark_dirty(void) { g_browser.layout_dirty = 1; }
int ui_is_dark(void) { return 0; }

static int font_scale(int scale) {
    if (scale < 1) return 1;
    if (scale > 5) return 5;
    return scale;
}
int sys_get_glyph_advance(u8 ch, int scale) {
    if (ch < 32 || ch >= 127) ch = '?';
    return font_glyphs[font_scale(scale)-1][ch-32].advance + 1;
}
int sys_text_width(const char *text, int scale) {
    int width = 0;size_t left=strlen(text);
    while(left){size_t used;uint32_t codepoint=pollik_utf8_next(text,left,&used);width+=sys_get_codepoint_advance(codepoint,scale);text+=used;left-=used;}
    return width;
}
static unsigned glyph_index(uint32_t codepoint) {
    if(codepoint>=32 && codepoint<127)return (unsigned)codepoint-32;
    for(unsigned i=95;i<FONT_GLYPH_COUNT;i++)if(font_codepoints[i]==codepoint)return i;
    return '?'-32;
}
int sys_get_codepoint_advance(uint32_t codepoint,int scale){return font_glyphs[font_scale(scale)-1][glyph_index(codepoint)].advance+1;}

static void fill_clip(int x,int y,int w,int h,u32 color,int x1,int y1,int x2,int y2) {
    if (x < x1) { w -= x1-x; x=x1; }
    if (y < y1) { h -= y1-y; y=y1; }
    if (x+w > x2) w=x2-x;
    if (y+h > y2) h=y2-y;
    if (w<=0 || h<=0) return;
    for (int row=0; row<h; ++row) {
        u32 *dst=canvas.pixels+(size_t)(y+row)*canvas.width+x;
        for (int col=0; col<w; ++col) dst[col]=color;
    }
}
void sys_draw_rect_clipped(int x,int y,int w,int h,u32 c,int x1,int y1,int x2,int y2) {
    fill_clip(x,y,w,h,c,x1,y1,x2,y2);
}
void sys_draw_rounded_clipped(int x,int y,int w,int h,int r,u32 c,int x1,int y1,int x2,int y2) {
    if(r>w/2)r=w/2;if(r>h/2)r=h/2;if(r<1){fill_clip(x,y,w,h,c,x1,y1,x2,y2);return;}
    for(int row=0;row<h;row++){int inset=0,distance=row<r?r-row-1:row>=h-r?row-(h-r):0;
        if(row<r || row>=h-r)while(inset<r && (r-inset-1)*(r-inset-1)+distance*distance>r*r)++inset;
        fill_clip(x+inset,y+row,w-2*inset,1,c,x1,y1,x2,y2);}
}
void sys_draw_letter_clipped(int x,int y,u8 ch,u32 color,int scale,int x1,int y1,int x2,int y2) {
    sys_draw_codepoint_clipped(x,y,ch,color,scale,x1,y1,x2,y2);
}
void sys_draw_codepoint_clipped(int x,int y,uint32_t ch,u32 color,int scale,int x1,int y1,int x2,int y2) {
    if (ch < 32) return;
    const FontGlyph *glyph=&font_glyphs[font_scale(scale)-1][glyph_index(ch)];
    int origin=x+(glyph->advance-glyph->width)/2;
    for (unsigned row=0; row<glyph->height; ++row) for (unsigned col=0; col<glyph->width; ++col) {
        unsigned i=row*glyph->width+col;
        u8 packed=font_coverage[glyph->offset+i/2];
        if (((i&1)?packed&15:packed>>4)<6) continue;
        int px=origin+(int)col,py=y+(int)row;
        if (px>=x1 && py>=y1 && px<x2 && py<y2 && (unsigned)px<canvas.width && (unsigned)py<canvas.height)
            canvas.pixels[(size_t)py*canvas.width+(unsigned)px]=color;
    }
}
void sys_draw_canvas_clipped(int x,int y,int w,int h,const u32 *src,int sw,int sh,int x1,int y1,int x2,int y2) {
    if (!src || w<=0 || h<=0 || sw<=0 || sh<=0) return;
    int xa=x<x1?x1:x, ya=y<y1?y1:y, xb=x+w>x2?x2:x+w, yb=y+h>y2?y2:y+h;
    for(int py=ya;py<yb;py++) for(int px=xa;px<xb;px++)
        canvas.pixels[(size_t)py*canvas.width+(unsigned)px]=src[(size_t)((py-y)*sh/h)*sw+(px-x)*sw/w];
}

static int read_file(const char *path, char *out, unsigned cap) {
    int fd=open(path,O_RDONLY);
    if(fd<0) return -1;
    unsigned used=0;
    while(used+1<cap) {
        ssize_t n=read(fd,out+used,cap-used-1);
        if(n<0) { close(fd); return -1; }
        if(!n) break;
        used+=(unsigned)n;
    }
    close(fd);
    out[used]=0;
    return (int)used;
}
static int path_for_reference(const char *page,const char *ref,char *out,unsigned cap) {
    if(!ref || !*ref || strstr(ref,"://") || ref[0]=='#') return 0;
    if(ref[0]=='/') {
        size_t n=strlen(ref);if(n>=cap)return 0;memcpy(out,ref,n+1);return 1;
    }
    const char *slash=strrchr(page,'/');
    unsigned dir=slash?(unsigned)(slash-page+1):0;
    if(dir+strlen(ref)+1>cap) return 0;
    if(dir) memcpy(out,page,dir);
    strcpy(out+dir,ref);
    return 1;
}
static void collect_external_css(DomNode *node,const char *page,char *css,unsigned cap,unsigned *used) {
    if(!node || *used+1>=cap) return;
    if(node->type==NODE_ELEMENT && !strcmp(node->tag,"link")) {
        const char *rel=dom_get_attribute(node,"rel");
        const char *href=dom_get_attribute(node,"href");
        if(rel && href && strstr(rel,"stylesheet")) {
            char path[USER_PATH_MAX];
            char *part=malloc(WEB_MAX_CSS_SIZE);
            if(part && path_for_reference(page,href,path,sizeof(path))) {
                int n=read_file(path,part,WEB_MAX_CSS_SIZE);
                if(n>0 && (unsigned)n<cap-*used-1) {
                    memcpy(css+*used,part,(unsigned)n);*used+=(unsigned)n;css[(*used)++]='\n';
                }
            }
            free(part);
        }
    }
    for(DomNode *c=node->first_child;c;c=c->next_sibling) collect_external_css(c,page,css,cap,used);
}
static void run_page_scripts(DomNode *node,const char *page,int *count,int local_assets) {
    if(!node)return;
    if(node->type==NODE_ELEMENT && !strcmp(node->tag,"script")) {
        int kind=browser_script_kind(node);
        if(kind==2){++g_browser.script_errors;puts("[browser] module scripts are unsupported");}
        if(kind!=1)return;
        const char *src=dom_get_attribute(node,"src");
        if(src && *src && local_assets) {
            char path[USER_PATH_MAX];
            char *code=malloc(WEB_MAX_JS_SIZE);
            if(path_for_reference(page,src,path,sizeof(path))) {
                int n=code?read_file(path,code,WEB_MAX_JS_SIZE):-1;
                if(n>0) {browser_js_execute(code);++*count;}
            }
            free(code);
        } else if((!src || !*src) && node->first_child && node->first_child->text) {
            browser_js_execute(node->first_child->text);++*count;
        }
    }
    for(DomNode *c=node->first_child;c;c=c->next_sibling)run_page_scripts(c,page,count,local_assets);
}
static int decode_image(DomNode *node,const void *bytes,unsigned length){
 int w,h;unsigned char *pixels=pollikos_image_decode(bytes,length,&w,&h);
 if(!pixels)return 0;
 free(node->image);node->image=pixels;node->image_w=w;node->image_h=h;
 printf("[browser] image decoded %dx%d bytes=%u\n",w,h,length);return 1;
}
static void local_images(DomNode *node,const char *page){
 if(!node)return;
 if(node->src[0]&&!strcmp(node->tag,"img")){
  char path[USER_PATH_MAX];
  if(path_for_reference(page,node->src,path,sizeof(path))){
   FILE *f=fopen(path,"rb");if(f){
    if(!fseek(f,0,SEEK_END)){long bytes=ftell(f);
     if(bytes>0&&(unsigned long)bytes<=IMAGE_LIMIT&&!fseek(f,0,SEEK_SET)){
      unsigned char *data=malloc((size_t)bytes);
      if(data&&fread(data,1,(size_t)bytes,f)==(size_t)bytes)(void)decode_image(node,data,(unsigned)bytes);
      free(data);
     }
    }fclose(f);
   }
  }
 }
 for(DomNode *c=node->first_child;c;c=c->next_sibling)local_images(c,page);
}
static int load_html_document(const char *page,const char *html,int n,int remote) {
    char *css=malloc(CSS_LIMIT+1);
    if(!css) {free(css);strcpy(status,"Not enough memory to style the page");return 0;}
    DomNode *next=html_parse(html,n);
    if(!next) { free(css);strcpy(status,"HTML parser could not create the document");return 0; }
    memset(&g_browser,0,sizeof(g_browser));
    g_browser.w=view_w+20;g_browser.h=view_h+110;g_browser.document=next;
    unsigned used=0;css[0]=0;
    if(!remote)collect_external_css(next,page,css,CSS_LIMIT+1,&used);
    css_apply_styles(next,css);
    memcpy(remote_css,css,used+1);remote_css_length=used;
    free(css);
    browser_js_init(next);
    int scripts=0;run_page_scripts(next,page,&scripts,!remote);
    if(!remote)local_images(next,page);
    if(document) dom_free_tree(document);
    document=next;
    char *source=malloc((size_t)n+1);if(source){memcpy(source,html,(size_t)n);source[n]=0;free(page_source);page_source=source;page_source_length=(unsigned)n;}
    document_ready=0;
    snprintf(document_url,sizeof(document_url),"%s",page);
    layout_compute(document,view_w,&page_height);
    scroll_y=0;
    if(!remote)snprintf(address,sizeof(address),"%s",page);
    if(scripts)snprintf(status,sizeof(status),"HTML + CSS + %d JavaScript file(s)",scripts);
    else if(remote)strcpy(status,"Loaded HTTP page with inline HTML, CSS and JavaScript");
    else strcpy(status,"Loaded local HTML with inline and linked CSS");
    return 1;
}
static int load_page(const char *path) {
    char page[USER_PATH_MAX];size_t page_len=strlen(path);
    if(!page_len || page_len>=sizeof(page)){strcpy(status,"Local path is empty or too long");return 0;}
    memcpy(page,path,page_len+1);
    char *html=malloc(HTML_LIMIT+1);
    if(!html) {strcpy(status,"Not enough memory to load the page");return 0;}
    int n=read_file(page,html,HTML_LIMIT+1);
    if(n<0) {free(html);snprintf(status,sizeof(status),"Cannot open %.70s",page);return 0;}
    int result=load_html_document(page,html,n,0);
    free(html);
    return result;
}
static void http_release(void) {
    if(http_handle>=0)pollikos_http_close(http_handle);
    http_handle=-1;http_loading=0;http_document_length=0;
    free(http_document);http_document=0;
}
static int begin_http_request(const char *url,int kind) {
    size_t length=strlen(url);
    if(!length || length>ADDRESS_LIMIT)return 0;
    http_limit=kind==3?IMAGE_LIMIT:HTML_LIMIT;
    http_document=malloc(http_limit+1);
    if(!http_document)return 0;
    long opened=pollikos_http_open(url);
    if(opened<0) {
        free(http_document);http_document=0;http_handle=-1;
        return 0;
    }
    http_handle=opened;
    http_request_kind=kind;
    http_loading=1;
    http_document_length=0;
    return 1;
}
static int start_remote(const char *url) {
    size_t length=strlen(url);
    if(!length || length>ADDRESS_LIMIT) {strcpy(status,"Web address is too long");return 0;}
    http_release();
    remote_resource_stage=0;
    memcpy(http_url,url,length+1);
    if(!begin_http_request(url,0)) {
        strcpy(status,"Cannot start HTTP request");
        return 0;
    }
    snprintf(address,sizeof(address),"%s",url);
    strcpy(status,"Connecting and loading page...");
    puts("[browser] HTTP request started");
    render_page();
    return 1;
}
static void poll_remote(void) {
    if(!http_loading || http_handle<0)return;
    unsigned char incoming[HTTP_READ_CHUNK];
    long n=pollikos_http_read(http_handle,incoming,sizeof(incoming));
    if(n==-USER_EAGAIN)return;
    if(n>0) {
        if(http_document_length+(unsigned long)n>http_limit) {
            int kind=http_request_kind;
            http_release();
            if(kind==0) {remote_resource_stage=0;strcpy(status,"Page exceeds the current 2 MiB document limit");render_page();}
            else {++remote_asset_index;pump_remote_resources();}
            return;
        }
        memcpy(http_document+http_document_length,incoming,(unsigned)n);
        http_document_length+=(unsigned)n;
        return;
    }
    if(n==0) {
        http_document[http_document_length]=0;
        int kind=http_request_kind;
        if(kind==0) {
            char final_url[ADDRESS_LIMIT+1];
            if(pollikos_http_url(http_handle,final_url,sizeof(final_url))>=0){strcpy(http_url,final_url);strcpy(address,final_url);if(history_count)strcpy(page_history[history_index],final_url);}
            printf("[browser] page status=%ld url=%s\n",pollikos_http_status(http_handle),http_url);
            int loaded=prepare_remote_document(http_url,http_document,(int)http_document_length);
            http_release();
            if(!loaded) {remote_resource_stage=0;render_page();return;}
            remote_resource_stage=1;remote_asset_index=0;
            pump_remote_resources();
            return;
        }
        if(kind==1 && http_document_length &&
           remote_css_length+http_document_length<=CSS_LIMIT) {
            memcpy(remote_css+remote_css_length,http_document,http_document_length);
            remote_css_length+=http_document_length;remote_css[remote_css_length]=0;
        } else if(kind==2 && http_document_length<WEB_MAX_JS_SIZE) {
            browser_js_execute(http_document);
        } else if(kind==3 && remote_asset_index<remote_image_count) {
            (void)decode_image(remote_images[remote_asset_index],http_document,http_document_length);
        }
        http_release();
        ++remote_asset_index;
        pump_remote_resources();
        return;
    }
    int kind=http_request_kind;
    http_release();
    printf("[browser] HTTP read failed (%ld)\n",n);
    if(kind==0) {remote_resource_stage=0;strcpy(status,"HTTP request failed while receiving the page");render_page();}
    else {++remote_asset_index;pump_remote_resources();}
}
static void remember_page(void) {
    if(history_count && history_index+1<history_count)history_count=history_index+1;
    if(history_count==16) {
        memmove(page_history,page_history+1,15*sizeof(page_history[0]));
        history_count=15;
        if(history_index)history_index--;
    }
    snprintf(page_history[history_count++],sizeof(page_history[0]),"%s",address);
    history_index=history_count-1;
}
static int resolve_web_reference(const char *base,const char *ref,char *out,unsigned cap) {
    if(strstr(ref,"://"))return pollikos_url_resolve(base,ref,out,cap);
    if(!strstr(base,"://"))return path_for_reference(base,ref,out,cap);
    return pollikos_url_resolve(base,ref,out,cap);
}
static void collect_remote_resources(DomNode *node,const char *page) {
    if(!node)return;
    if(node->type==NODE_ELEMENT && !strcmp(node->tag,"link") && remote_style_count<REMOTE_ASSET_MAX) {
        const char *rel=dom_get_attribute(node,"rel"),*href=dom_get_attribute(node,"href");
        if(rel&&href&&strstr(rel,"stylesheet")&&
           resolve_web_reference(page,href,remote_styles[remote_style_count],sizeof(remote_styles[0])))
            ++remote_style_count;
    } else if(node->type==NODE_ELEMENT && !strcmp(node->tag,"script") && remote_script_count<REMOTE_ASSET_MAX) {
        if(browser_script_kind(node)!=1)return;
        const char *src=dom_get_attribute(node,"src");
        RemoteScript *item=&remote_scripts[remote_script_count];
        item->url[0]=0;item->inline_script=0;
        if(src&&*src) {
            if(resolve_web_reference(page,src,item->url,sizeof(item->url)))++remote_script_count;
        } else if(node->first_child&&node->first_child->text) {
            item->inline_script=node;++remote_script_count;
        }
    }
    for(DomNode *child=node->first_child;child;child=child->next_sibling)
        collect_remote_resources(child,page);
}
static void collect_remote_images(DomNode *node,const char *page){
 if(!node)return;
 if(!strcmp(node->tag,"img")&&node->src[0]&&remote_image_count<REMOTE_ASSET_MAX&&
    resolve_web_reference(page,node->src,remote_image_urls[remote_image_count],sizeof(remote_image_urls[0])))
  remote_images[remote_image_count++]=node;
 for(DomNode *c=node->first_child;c;c=c->next_sibling)collect_remote_images(c,page);
}
static int prepare_remote_document(const char *page,const char *html,int n) {
    DomNode *next=html_parse(html,n);
    if(!next) {strcpy(status,"HTML parser could not create the remote document");return 0;}
    memset(&g_browser,0,sizeof(g_browser));
    g_browser.w=view_w+20;g_browser.h=view_h+110;g_browser.document=next;
    if(document)dom_free_tree(document);
    document=next;
    char *source=malloc((size_t)n+1);if(source){memcpy(source,html,(size_t)n);source[n]=0;free(page_source);page_source=source;page_source_length=(unsigned)n;}
    document_ready=0;
    browser_js_init(next);
    remote_style_count=remote_script_count=remote_asset_index=0;
    remote_image_count=0;snprintf(document_url,sizeof(document_url),"%s",page);
    remote_css_length=0;remote_css[0]=0;
    collect_remote_resources(next,page);
    strcpy(status,"Page loaded; fetching styles and scripts...");
    return 1;
}
static void pump_remote_resources(void) {
    while(remote_resource_stage==1) {
        if(remote_asset_index<remote_style_count) {
            if(begin_http_request(remote_styles[remote_asset_index],1))return;
            ++remote_asset_index;
            continue;
        }
        css_apply_styles(document,remote_css);
        remote_resource_stage=2;remote_asset_index=0;
    }
    while(remote_resource_stage==2) {
        if(remote_asset_index>=remote_script_count) {
            remote_resource_stage=3;remote_asset_index=0;
            collect_remote_images(document,document_url);
            break;
        }
        RemoteScript *item=&remote_scripts[remote_asset_index];
        if(item->url[0]) {
            if(begin_http_request(item->url,2))return;
        } else if(item->inline_script && item->inline_script->first_child &&
                  item->inline_script->first_child->text) {
            browser_js_execute(item->inline_script->first_child->text);
        }
        ++remote_asset_index;
    }
    while(remote_resource_stage==3){
        if(remote_asset_index>=remote_image_count){
            remote_resource_stage=0;
            layout_compute(document,view_w,&page_height);scroll_y=0;
            snprintf(status,sizeof(status),"HTTP page: %u CSS, %u JavaScript resources",
                     remote_style_count,remote_script_count);
            printf("[browser] HTTP document loaded: %s\n",browser_js_title());
            render_page();
            return;
        }
        if(begin_http_request(remote_image_urls[remote_asset_index],3))return;
        ++remote_asset_index;
    }
}
static void bookmarks_load(void) {
    FILE *file=fopen("/home/.pollik-web-bookmarks","rb");if(!file)return;
    char line[ADDRESS_LIMIT+2];while(bookmark_count<32 && fgets(line,sizeof(line),file)){
        size_t n=strlen(line);while(n && (line[n-1]=='\r'||line[n-1]=='\n'))line[--n]=0;
        if(n && n<=ADDRESS_LIMIT)strcpy(bookmarks[bookmark_count++],line);
    }fclose(file);
}
static void bookmark_page(void) {
    unsigned existing=bookmark_count;for(unsigned i=0;i<bookmark_count;i++)if(!strcmp(bookmarks[i],address)){existing=i;break;}
    if(existing==bookmark_count && bookmark_count==32){strcpy(status,"Bookmark list is full");return;}
    char temporary[]="/home/.pollik-web-bookmarks-XXXXXX";int fd=mkstemp(temporary);if(fd<0){strcpy(status,"Could not save bookmarks");return;}
    int failed=0;
    for(unsigned i=0;i<bookmark_count;i++)if(i!=existing){size_t n=strlen(bookmarks[i]);if(write(fd,bookmarks[i],n)!=(ssize_t)n || write(fd,"\n",1)!=1)failed=1;}
    if(existing==bookmark_count){size_t n=strlen(address);if(write(fd,address,n)!=(ssize_t)n || write(fd,"\n",1)!=1)failed=1;}
    if(close(fd)<0)failed=1;
    if(failed || rename_replace(temporary,"/home/.pollik-web-bookmarks")<0){strcpy(status,"Bookmark save failed");return;}
    if(existing<bookmark_count){memmove(bookmarks+existing,bookmarks+existing+1,(bookmark_count-existing-1)*sizeof(bookmarks[0]));--bookmark_count;strcpy(status,"Bookmark removed");}
    else{strcpy(bookmarks[bookmark_count++],address);strcpy(status,"Page bookmarked");}
    puts("[browser] bookmarks saved");
}
static void save_page(void) {
    if(!page_source){strcpy(status,"Load a page before saving");return;}
    if(mkdir("/home/Downloads",0)<0 && errno!=EEXIST){strcpy(status,"Cannot create Downloads folder");return;}
    char temporary[]="/home/Downloads/page-XXXXXX";int fd=mkstemp(temporary);if(fd<0){strcpy(status,"Cannot create saved page");return;}
    unsigned done=0;while(done<page_source_length){ssize_t n=write(fd,page_source+done,page_source_length-done);if(n<=0)break;done+=(unsigned)n;}
    int failed=done!=page_source_length;if(close(fd)<0)failed=1;
    if(failed){unlink(temporary);strcpy(status,"Save failed; page remains open");return;}
    char filename[USER_PATH_MAX];snprintf(filename,sizeof(filename),"%s.html",temporary);
    if(rename(temporary,filename)<0){strcpy(status,"Could not finish saving the page");return;}
    snprintf(status,sizeof(status),"Saved HTML to %s",filename);printf("[browser] saved page %s\n",filename);
}
static void show_bookmarks(void) {
    char *html=malloc(65536);if(!html){strcpy(status,"Not enough memory");return;}
    size_t used=(size_t)snprintf(html,65536,"<html><head><title>Bookmarks</title></head><body><h1>Bookmarks</h1><p><a href='about:home'>Home</a></p>");
    for(unsigned i=0;i<bookmark_count;i++) {
        char escaped[ADDRESS_LIMIT*6+1];size_t at=0;
        for(const char *p=bookmarks[i];*p;p++){const char *entity=*p=='&'?"&amp;":*p=='<'?"&lt;":*p=='>'?"&gt;":*p=='\"'?"&quot;":*p=='\''?"&#39;":NULL;
            if(entity){size_t n=strlen(entity);memcpy(escaped+at,entity,n);at+=n;}else escaped[at++]=*p;}escaped[at]=0;
        if(at*2+sizeof("<p><a href=\"\"></a></p></body></html>")>65536-used)break;
        used+=(size_t)snprintf(html+used,65536-used,"<p><a href=\"%s\">%s</a></p>",escaped,escaped);
    }
    strcpy(html+used,"</body></html>");
    http_release();remote_resource_stage=0;remote_css[0]=0;
    (void)load_html_document("about:bookmarks",html,(int)strlen(html),0);free(html);strcpy(address,"about:bookmarks");strcpy(document_url,address);render_page();
}

static void draw_chrome(void) {
    pollik_ui_fill(&canvas,0,0,(int)canvas.width,(int)canvas.height,0xf4f6fa);
    pollik_ui_fill(&canvas,0,0,(int)canvas.width,38,0x202939);
    pollik_ui_text(&canvas,18,11,"Pollik Web",0xf3f5fa);
    pollik_ui_text(&canvas,(int)canvas.width-325,11,"Bookmarks   Save page   New window",0xc7d1e7);
    pollik_ui_fill(&canvas,12,49,42,34,0xe3e8f0);
    pollik_ui_text(&canvas,26,59,"<",0x354052);
    pollik_ui_fill(&canvas,62,49,42,34,0xe3e8f0);
    pollik_ui_text(&canvas,76,59,">",0x354052);
    pollik_ui_fill(&canvas,112,49,42,34,0xe3e8f0);pollik_ui_text(&canvas,126,59,http_loading?"X":"R",0x354052);
    pollik_ui_fill(&canvas,162,49,42,34,0xe3e8f0);pollik_ui_text(&canvas,176,59,"H",0x354052);
    pollik_ui_fill(&canvas,212,48,(int)canvas.width-268,36,0xffffff);
    pollik_ui_fill(&canvas,212,48,(int)canvas.width-268,1,0xc8d0dd);
    pollik_ui_fill(&canvas,212,83,(int)canvas.width-268,1,0xc8d0dd);
    const char *shown=address;while(*shown && pollik_ui_text_width(shown)>(int)canvas.width-294)shown++;
    pollik_ui_text(&canvas,224,59,*address?shown:"Search or enter a web address",0x263246);
    if(address_editing) pollik_ui_fill(&canvas,224+pollik_ui_text_width(shown),58,1,17,0x405e9a);
    pollik_ui_fill(&canvas,(int)canvas.width-46,49,34,34,0xe3e8f0);pollik_ui_text(&canvas,(int)canvas.width-36,59,"*",0x354052);
    pollik_ui_fill(&canvas,0,91,(int)canvas.width,1,0xd8dee8);
    pollik_ui_fill(&canvas,view_x,view_y,view_w,view_h,0xffffff);
    pollik_ui_fill(&canvas,0,(int)canvas.height-27,(int)canvas.width,27,0xe9edf3);
    pollik_ui_text(&canvas,14,(int)canvas.height-19,status,0x596579);
    const char *title=browser_js_title();
    if(title && *title) {
        int start=(int)canvas.width-sys_text_width(title,1)-14;
        if(start>520)pollik_ui_text(&canvas,start,(int)canvas.height-19,title,0x596579);
    }
}
static void render_page(void) {
    if(document && g_browser.layout_dirty){css_apply_styles(document,remote_css);layout_compute(document,view_w,&page_height);g_browser.layout_dirty=0;}
    draw_chrome();
    if(document) {
        int max_scroll=page_height-view_h;if(max_scroll<0)max_scroll=0;
        if(scroll_y>max_scroll)scroll_y=max_scroll;
        render_dom(document,view_x,view_y,view_w,view_h,scroll_y);
    }
    (void)pollikos_window_present();
}
static int navigate_to(const char *path,int remember) {
    if(!strcmp(path,"about:home") || !strcmp(path,"about:blank")) {
        http_release();remote_resource_stage=0;
        if(!load_html_document("about:home",browser_start_page,(int)strlen(browser_start_page),0)){render_page();return 0;}
        strcpy(address,"about:home");strcpy(document_url,address);remote_css[0]=0;document_ready=0;
        if(remember)remember_page();strcpy(status,"Ready");render_page();return 1;
    }
    if(strstr(path,"://")) {
        if(!start_remote(path)) {render_page();return 0;}
        if(remember)remember_page();
        return 1;
    }
    http_release();remote_resource_stage=0;
    if(!load_page(path)) {render_page();return 0;}
    if(remember)remember_page();
    render_page();return 1;
}
static void history_go(int backward) {
    if(backward) {
        if(history_index==0)return;
        --history_index;
    } else {
        if(history_index+1>=history_count)return;
        ++history_index;
    }
    (void)navigate_to(page_history[history_index],0);
}
static void submit_address(void) {
    char path[ADDRESS_LIMIT+1];
    if(strstr(address,"://") || !strncmp(address,"about:",6))snprintf(path,sizeof(path),"%s",address);
    else if(address[0]=='/')snprintf(path,sizeof(path),"%s",address);
    else if(strchr(address,' ') || !strchr(address,'.')) {
        char encoded[ADDRESS_LIMIT+1];unsigned at=0;
        for(const unsigned char *p=(const unsigned char *)address;*p && at+4<sizeof(encoded);p++){
            if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_'||*p=='.')encoded[at++]=(char)*p;
            else{static const char hex[]="0123456789ABCDEF";encoded[at++]='%';encoded[at++]=hex[*p>>4];encoded[at++]=hex[*p&15];}}
        encoded[at]=0;snprintf(path,sizeof(path),"https://lite.duckduckgo.com/lite/?q=%s",encoded);
    } else snprintf(path,sizeof(path),"https://%s",address);
    (void)navigate_to(path,1);
}
static DomNode *hit_test(DomNode *node,int x,int y,int root) {
    if(!node || node->style.display==DISPLAY_NONE)return 0;
    int inside=root || (x>=node->box.x && y>=node->box.y &&
                       x<node->box.x+node->box.w && y<node->box.y+node->box.h);
    if(!inside)return 0;
    DomNode *found=0;
    for(DomNode *child=node->first_child;child;child=child->next_sibling) {
        DomNode *candidate=hit_test(child,x,y,0);if(candidate)found=candidate;
    }
    if(found)return found;
    return node->type==NODE_DOCUMENT?0:node;
}
static DomNode *nearest_link(DomNode *node) {
    for(DomNode *n=node;n;n=n->parent)
        if(n->type==NODE_ELEMENT && !strcmp(n->tag,"a"))return n;
    return 0;
}
static void open_link(const char *href) {
    if(!href || !*href)return;
    if(!strncmp(href,"about:",6)){(void)navigate_to(href,1);return;}
    if(!strncmp(href,"javascript:",11)){browser_js_execute(href+11);return;}
    if(href[0]=='#') {
        DomNode *target=dom_get_element_by_id(document,href+1);
        if(target){scroll_y=target->box.y;strcpy(status,"Jumped to page section");render_page();}
        return;
    }
    char path[ADDRESS_LIMIT+1];
    if(!resolve_web_reference(address,href,path,sizeof(path))) {
        strcpy(status,"This link address is too long or not supported");render_page();return;
    }
    (void)navigate_to(path,1);
}
static int address_character(uint32_t key,uint32_t modifiers) {
    if(key<32 || key>=127)return 0;
    int shifted=(modifiers&POLLIKOS_INPUT_MOD_SHIFT)!=0;
    if(!shifted)return (int)key;
    if(key>='a'&&key<='z')return (int)(key-'a'+'A');
    switch(key) {
    case '1':return '!';case '2':return '@';case '3':return '#';case '4':return '$';
    case '5':return '%';case '6':return '^';case '7':return '&';case '8':return '*';
    case '9':return '(';case '0':return ')';case '-':return '_';case '=':return '+';
    case '[':return '{';case ']':return '}';case '\\':return '|';case ';':return ':';
    case '\'':return '"';case ',':return '<';case '.':return '>';case '/':return '?';
    case '`':return '~';default:return (int)key;
    }
}
static DomNode *ancestor_tag(DomNode *node,const char *tag){for(;node;node=node->parent)if(!strcmp(node->tag,tag))return node;return NULL;}
static int query_append(char *out,unsigned capacity,unsigned *used,const char *value) {
    static const char hex[]="0123456789ABCDEF";
    for(const unsigned char *p=(const unsigned char *)value;*p;p++){
        if(*used+4>=capacity)return 0;
        if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_'||*p=='.'||*p=='~')out[(*used)++]=(char)*p;
        else if(*p==' ')out[(*used)++]='+';
        else{out[(*used)++]='%';out[(*used)++]=hex[*p>>4];out[(*used)++]=hex[*p&15];}}
    out[*used]=0;return 1;
}
static int collect_form(DomNode *node,char *url,unsigned capacity,unsigned *used,int *first) {
    if((!strcmp(node->tag,"input") || !strcmp(node->tag,"textarea")) && node->name[0] && !dom_get_attribute(node,"disabled") &&
       strcmp(node->input_type,"submit") && strcmp(node->input_type,"button") &&
       (strcmp(node->input_type,"checkbox") || dom_get_attribute(node,"checked"))) {
        if(*used+2>=capacity)return 0;if(!*first)url[(*used)++]='&';*first=0;
        if(!query_append(url,capacity,used,node->name))return 0;url[(*used)++]='=';
        if(!query_append(url,capacity,used,node->value))return 0;
    }
    for(DomNode *child=node->first_child;child;child=child->next_sibling)if(!collect_form(child,url,capacity,used,first))return 0;return 1;
}
static void submit_form(DomNode *control) {
    DomNode *form=ancestor_tag(control,"form");if(!form)return;
    if(!browser_js_dispatch_event(form,"submit",0,0,0))return;
    const char *method=dom_get_attribute(form,"method");if(method && strcmp(method,"get") && strcmp(method,"GET")){strcpy(status,"POST forms are not supported by this transport yet");return;}
    const char *action=dom_get_attribute(form,"action");char url[ADDRESS_LIMIT+1];
    if(!resolve_web_reference(address,action && *action?action:address,url,sizeof(url))){strcpy(status,"Form address is too long");return;}
    unsigned used=(unsigned)strlen(url);if(used+2>=sizeof(url))return;url[used++]=strchr(url,'?')?'&':'?';url[used]=0;int first=1;
    if(!collect_form(form,url,sizeof(url),&used,&first)){strcpy(status,"Form data is too long");return;}(void)navigate_to(url,1);
}
static void input_key(uint32_t key,uint32_t modifiers) {
    DomNode *node=g_browser.focused_input;if(!node)return;
    if(!browser_js_dispatch_event(node,"keydown",0,0,(int)key))return;
    unsigned length=(unsigned)strlen(node->value);int changed=0;
    if((modifiers&POLLIKOS_INPUT_MOD_CONTROL) && (key=='a'||key=='A')){g_browser.focused_anchor=0;g_browser.focused_cursor=(int)length;return;}
    if(key==13){submit_form(node);return;}
    if(key==POLLIKOS_KEY_LEFT && g_browser.focused_cursor>0)--g_browser.focused_cursor;
    else if(key==POLLIKOS_KEY_RIGHT && (unsigned)g_browser.focused_cursor<length)++g_browser.focused_cursor;
    else if(key==POLLIKOS_KEY_HOME)g_browser.focused_cursor=0;
    else if(key==POLLIKOS_KEY_END)g_browser.focused_cursor=(int)length;
    else if(key==8 || key==POLLIKOS_KEY_DELETE || address_character(key,modifiers)) {
        unsigned cursor=(unsigned)g_browser.focused_cursor;if(cursor>length)cursor=length;int had_selection=0;
        if(g_browser.focused_anchor>=0 && g_browser.focused_anchor!=(int)cursor){unsigned a=(unsigned)g_browser.focused_anchor,b=cursor;if(a>b){unsigned t=a;a=b;b=t;}
            memmove(node->value+a,node->value+b,length-b+1);length-=b-a;cursor=a;changed=had_selection=1;}
        if(key==8 && !had_selection && cursor){memmove(node->value+cursor-1,node->value+cursor,length-cursor+1);--cursor;changed=1;}
        else if(key==POLLIKOS_KEY_DELETE && !had_selection && cursor<length){memmove(node->value+cursor,node->value+cursor+1,length-cursor);changed=1;}
        else if(address_character(key,modifiers) && length+1<sizeof(node->value)){memmove(node->value+cursor+1,node->value+cursor,length-cursor+1);node->value[cursor++]=(char)address_character(key,modifiers);changed=1;}
        g_browser.focused_cursor=(int)cursor;
    }
    g_browser.focused_anchor=-1;
    if(changed){dom_set_attribute(node,"value",node->value);g_browser.layout_dirty=1;(void)browser_js_dispatch_event(node,"input",0,0,(int)key);}
}

int main(int argc,char **argv) {
    int64_t pixels=pollikos_window_create(BROWSER_W,BROWSER_H,"Pollik Web");
    if(pixels<0) { puts("[browser] cannot create window");return 1; }
    canvas=(PollikCanvas){(u32 *)(uintptr_t)pixels,BROWSER_W,BROWSER_H};
    view_x=12;view_y=100;view_w=BROWSER_W-24;view_h=BROWSER_H-139;
    bookmarks_load();
    memset(page_history,0,sizeof(page_history));history_count=history_index=0;
    if(argc>1 && argv[1] && argv[1][0]) snprintf(address,sizeof(address),"%s",argv[1]);
    (void)navigate_to(address,1);
    puts("[browser] ready: native HTML parser and CSS renderer");
    int running=1;
    while(running) {
        poll_remote();
#ifndef POLLIK_BROWSER_ELK
        browser_js_poll();
        if(!document_ready && document && !http_loading && !remote_resource_stage){document_ready=1;browser_js_document_ready();}
        if(pending_navigation[0]){char next[ADDRESS_LIMIT+1];strcpy(next,pending_navigation);pending_navigation[0]=0;(void)navigate_to(next,1);}
        if(g_browser.layout_dirty)render_page();
#endif
        pollikos_input_event_t e;
        if(pollikos_input_read(&e)==(int64_t)sizeof(e)) {
            if(e.kind&POLLIKOS_INPUT_WINDOW_CLOSE) running=0;
            if((e.kind&POLLIKOS_INPUT_KEY_DOWN) && (e.modifiers&POLLIKOS_INPUT_MOD_CONTROL) &&
               (e.key=='l'||e.key=='L'||e.key=='d'||e.key=='D'||e.key=='s'||e.key=='S'||e.key=='b'||e.key=='B'||e.key=='n'||e.key=='N'||e.key=='r'||e.key=='R')) {
                uint32_t key=e.key;if(key>='A' && key<='Z')key+=32;
                if(key=='l'){address_editing=1;address[0]=0;g_browser.focused_input=NULL;}
                else if(key=='d')bookmark_page();else if(key=='s')save_page();else if(key=='b')show_bookmarks();
                else if(key=='r'){char url[ADDRESS_LIMIT+1];strcpy(url,document_url[0]?document_url:address);(void)navigate_to(url,0);}
                else if(key=='n'){const char *arguments[]={"browser","about:home",NULL};if(pollikos_spawn("/bin/browser.pol",arguments,NULL)<0)strcpy(status,"Could not open another window");}
                render_page();
            } else if((e.kind&POLLIKOS_INPUT_KEY_DOWN) && address_editing) {
                if(e.key==13) { address_editing=0;submit_address(); }
                else if(e.key==27) { address_editing=0;if(document_url[0])strcpy(address,document_url); }
                else if(e.key==8) { size_t len=strlen(address);if(len)address[len-1]=0; }
                else if(address_character(e.key,e.modifiers) && strlen(address)<ADDRESS_LIMIT) {
                    size_t len=strlen(address);address[len]=(char)address_character(e.key,e.modifiers);address[len+1]=0;
                }
                render_page();
            } else if((e.kind&POLLIKOS_INPUT_KEY_DOWN) && g_browser.focused_input) {
                input_key(e.key,e.modifiers);render_page();
            } else if(e.kind&POLLIKOS_INPUT_KEY_DOWN) {
                if((e.modifiers&POLLIKOS_INPUT_MOD_ALT) && e.key==POLLIKOS_KEY_LEFT)history_go(1);
                else if((e.modifiers&POLLIKOS_INPUT_MOD_ALT) && e.key==POLLIKOS_KEY_RIGHT)history_go(0);
                else if(e.key=='f'||e.key=='F') { address_editing=1;address[0]=0; }
                else if(e.key==POLLIKOS_KEY_UP) scroll_y-=48;
                else if(e.key==POLLIKOS_KEY_DOWN) scroll_y+=48;
                else if(e.key==POLLIKOS_KEY_HOME) scroll_y=0;
                else if(e.key==POLLIKOS_KEY_END) scroll_y=page_height;
                else if(e.key==13) { address_editing=1; }
                render_page();
            } else if((e.kind&POLLIKOS_INPUT_MOUSE_WHEEL) && e.wheel) {
                scroll_y-=e.wheel*42;render_page();
            } else if((e.kind&POLLIKOS_INPUT_MOUSE_BUTTON) && (e.changed&POLLIKOS_MOUSE_LEFT) &&
                      (e.buttons&POLLIKOS_MOUSE_LEFT)) {
                pollikos_window_info_t info;
                if(pollikos_window_info(&info)==0) {
                    int x=e.x-info.content_x,y=e.y-info.content_y;
                    if(y>=48 && y<85) {
                        if(x>=12 && x<54)history_go(1);
                        else if(x>=62 && x<104)history_go(0);
                        else if(x>=112 && x<154){if(http_loading){http_release();remote_resource_stage=0;strcpy(status,"Stopped");}else{char url[ADDRESS_LIMIT+1];strcpy(url,document_url[0]?document_url:address);(void)navigate_to(url,0);}}
                        else if(x>=162 && x<204)(void)navigate_to("about:home",1);
                        else if(x>=(int)canvas.width-46)bookmark_page();
                        else if(x>=212 && x<(int)canvas.width-56){address_editing=1;g_browser.focused_input=NULL;}
                        render_page();
                    } else if(y<38 && x>(int)canvas.width-325) {
                        if(x<(int)canvas.width-225)show_bookmarks();else if(x<(int)canvas.width-115)save_page();else {const char *arguments[]={"browser","about:home",NULL};(void)pollikos_spawn("/bin/browser.pol",arguments,NULL);}render_page();
                    } else if(y>=view_y && y<view_y+view_h && x>=view_x && x<view_x+view_w && document) {
                        int doc_x=x-view_x,doc_y=y-view_y+scroll_y;
                        DomNode *target=hit_test(document,doc_x,doc_y,1);
                        DomNode *link=nearest_link(target);char href[ADDRESS_LIMIT+1]={0};
                        if(link)snprintf(href,sizeof(href),"%s",link->href);
                        DomNode *input=ancestor_tag(target,"input");if(!input)input=ancestor_tag(target,"textarea");
                        if(input && !dom_get_attribute(input,"disabled")){g_browser.focused_input=input;g_browser.focused_cursor=(int)strlen(input->value);g_browser.focused_anchor=-1;address_editing=0;
                            if(!strcmp(input->input_type,"checkbox")){if(dom_get_attribute(input,"checked"))dom_remove_attribute(input,"checked");else dom_set_attribute(input,"checked","");strcpy(input->value,dom_get_attribute(input,"checked")?"on":"");}}
                        else g_browser.focused_input=NULL;
                        int allow_default=browser_js_dispatch_click(target,doc_x,doc_y);
                        layout_compute(document,view_w,&page_height);
                        if(allow_default && href[0])open_link(href);
                        else if(allow_default && (ancestor_tag(target,"button") || (input && !strcmp(input->input_type,"submit")))){submit_form(target);render_page();}
                        else render_page();
                    }
                }
                else render_page();
            }
        }
        (void)pollikos_sleep_ms(12);
    }
    puts("[browser] closing");
    http_release();
#ifndef POLLIK_BROWSER_ELK
    browser_js_destroy();
#endif
    if(document) dom_free_tree(document);
    free(page_source);
    return pollikos_window_destroy()<0?2:0;
}
