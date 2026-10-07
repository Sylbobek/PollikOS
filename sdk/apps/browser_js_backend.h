#ifndef POLLIK_BROWSER_JS_BACKEND_H
#define POLLIK_BROWSER_JS_BACKEND_H
void browser_js_init(DomNode *document);
void browser_js_destroy(void);
void browser_js_execute(const char *code);
void browser_js_execute_source(const char *code,const char *name,int module);
void browser_js_poll(void);
int browser_js_dispatch_click(DomNode *node,int x,int y);
const char *browser_js_title(void);
#endif
