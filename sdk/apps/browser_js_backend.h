#ifndef POLLIK_BROWSER_JS_BACKEND_H
#define POLLIK_BROWSER_JS_BACKEND_H
void browser_js_init(DomNode *document);
void browser_js_destroy(void);
void browser_js_execute(const char *code);
void browser_js_execute_source(const char *code,const char *name,int module);
void browser_js_poll(void);
int browser_js_dispatch_click(DomNode *node,int x,int y);
int browser_js_dispatch_event(DomNode *node,const char *type,int x,int y,int key);
void browser_js_document_ready(void);
int browser_resolve_url(const char *reference,char *out,unsigned capacity);
void browser_request_navigation(const char *url);
const char *browser_current_url(void);
const char *browser_js_title(void);
#endif
