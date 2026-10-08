#define POLLIK_BROWSER_STANDALONE 1
#include "../../kernel/browser/browser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pollikos/net.h>
BrowserApp g_browser;
void *kmalloc(u32 size){return malloc(size?size:1);}void kfree(void *pointer){free(pointer);}
void browser_work_checkpoint(void){}void browser_mark_dirty(void){}
int ui_is_dark(void){return 0;}
static int errors;
#define CHECK(x) do {if(!(x)){printf("WEB_ENGINE_FAIL line=%d\n",__LINE__);errors++;}} while(0)
int main(void) {
    static const char html[]="<!doctype html><meta charset='utf-8'><style>#area{width:300px}p{color:blue}#area p:nth-child(2)[data-kind^='live']{color:#112233!important;width:calc(50% - 10px)}@media(min-width:800px){#wide{color:#771111}}</style><div id=area><p>one<p id=chosen data-kind=live-demo style='color:#cc3333'>two</div><p id=wide>wide<table><td id=cell title='&amp;amp;'>A &copy; &#x1f600;</table>";
    g_browser.w=916;g_browser.h=611;
    for(unsigned iteration=0;iteration<30;iteration++) {
        DomNode *root=html_parse(html,(int)strlen(html));CHECK(root!=NULL);if(!root)break;
        CHECK(dom_query_selector(root,"html head")!=NULL);CHECK(dom_query_selector(root,"body table tr td")!=NULL);
        DomNode *chosen=dom_get_element_by_id(root,"chosen"),*cell=dom_get_element_by_id(root,"cell"),*wide=dom_get_element_by_id(root,"wide");
        CHECK(chosen && cell && wide);
        if(cell)CHECK(!strcmp(dom_get_attribute(cell,"title"),"&amp;"));
        css_apply_styles(root,NULL);
        if(chosen){CHECK(chosen->style.color==0x112233);CHECK(chosen->style.width==140);}
        if(wide)CHECK(wide->style.color==0x771111);
        if(chosen){const char *fragment="<span class='new'>UTF-8: ą</span>";dom_set_inner_html(chosen,fragment,(int)strlen(fragment));CHECK(dom_query_selector(chosen,"span.new")!=NULL);}
        dom_free_tree(root);
    }
    char url[256];CHECK(pollikos_url_resolve("https://example.org/a/b?old=1","../c?x=2",url,sizeof(url)) && !strcmp(url,"https://example.org/c?x=2"));
    CHECK(pollikos_url_resolve("https://example.org/a/b","//cdn.example.org/x",url,sizeof(url)) && !strcmp(url,"https://cdn.example.org/x"));
    if(!errors)puts("WEB_ENGINE_PASS real HTML5 repair/entities, libcss cascade/important/nth-child/calc/media and 30 lifecycles");return errors?1:0;
}
