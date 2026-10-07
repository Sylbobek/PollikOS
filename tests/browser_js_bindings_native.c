#define POLLIK_BROWSER_STANDALONE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../third_party/elk/elk.c"
#ifndef BROWSER_JS_SOURCE
#define BROWSER_JS_SOURCE "../sdk/apps/browser_js.c"
#endif
#include BROWSER_JS_SOURCE
BrowserApp g_browser;
void *kmalloc(u32 n){return malloc(n);}
void kfree(void *p){free(p);}
void browser_work_checkpoint(void){}
void browser_mark_dirty(void){g_browser.layout_dirty=1;}
int ui_is_dark(void){return 0;}
static int intact(void){
    for(jsoff_t off=0;off<runtime->brk;){
        jsoff_t n=esize(loadoff(runtime,off));
        if(!n || n>runtime->brk-off)return 0;
        off+=n;
    }
    return 1;
}
int main(void){
    const char html[]="<html><head><title>GC proof</title></head><body id='root'><p id='old'>text</p></body></html>";
    DomNode *doc=html_parse(html,sizeof(html)-1);
    browser_js_init(doc);
    js_setgct(runtime,0); /* deterministic collection at every nested statement */
    char garbage[4096];memset(garbage,'g',sizeof(garbage));
    js_mkstr(runtime,garbage,sizeof(garbage)); /* force actual compaction */
    DomNode *button=dom_create_element("button");
    dom_set_attribute(button,"id","late");
    dom_append_child(dom_query_selector(doc,"body"),button);
    browser_js_dispatch_click(button,20,20);
    if(!intact()){puts("FAIL nested DOM callback damaged GC entity chain");return 1;}
    jsval_t event=pollik_js_get(runtime,js_glob(runtime),"event");
    jsval_t target=pollik_js_get(runtime,event,"target");
    char *id=js_getstr(runtime,pollik_js_get(runtime,target,"id"),0);
    if(!id||strcmp(id,"late")){puts("FAIL event target lost after nested GC");return 1;}
    browser_js_execute("document.getElementById('root').textContent='replaced';");
    if(!intact()){puts("FAIL DOM replacement damaged GC entity chain");return 1;}
    for(unsigned i=0;i<binding_count;++i)
        if(bindings[i].node==button){puts("FAIL destroyed node retained in binding");return 1;}
    dom_free_tree(doc);
    for(unsigned i=0;i<binding_count;++i)
        if(object_roots[i].owner||style_roots[i].owner){puts("FAIL destroyed DOM retained C roots");return 1;}
    puts("PASS DOM bindings: forced GC, live event target, subtree invalidation, exact root release");
    return 0;
}
