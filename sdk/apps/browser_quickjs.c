#define POLLIK_BROWSER_STANDALONE 1
#define POLLIK_JS_LIMB32 1
#include "../../kernel/browser/browser.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "../../third_party/quickjs/quickjs.h"
#pragma clang diagnostic pop
#include <pollikos/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "browser_js_backend.h"
#define BINDINGS 1024
#define LISTENERS 512
typedef struct {DomNode *node;JSValue object,style,last_style[11];} Binding;
typedef struct {DomNode *node;char type[24];JSValue fn;} Listener;
static Binding bindings[BINDINGS];static unsigned binding_count;
static Listener listeners[LISTENERS];static unsigned listener_count;
static JSRuntime *runtime;static JSContext *context;static JSClassID node_class;
static DomNode *document;static char title[64];static int prevented,stopped;
static uint64_t deadline;static unsigned interrupts;
static JSValue wrap(DomNode *node);
static Binding *binding(JSValueConst value){return JS_GetOpaque(value,node_class);}
static int interrupt(JSRuntime *rt,void *opaque){
    (void)rt;(void)opaque;
    return ++interrupts>1000 || (uint64_t)pollikos_monotonic_ms()>deadline;
}
static void begin_budget(void){interrupts=0;deadline=(uint64_t)pollikos_monotonic_ms()+50;}
static void error(const char *operation){
    JSValue exception=JS_GetException(context);const char *text=JS_ToCString(context,exception);
    printf("[browser:js] %s: %s\n",operation,text?text:"exception");
    JS_FreeCString(context,text);JS_FreeValue(context,exception);++g_browser.script_errors;
}
static JSValue text(DomNode *node){
    size_t n=0,cap=256;char *out=malloc(cap);if(!out)return JS_ThrowOutOfMemory(context);
    DomNode *stack[DOM_MAX_DEPTH*4];unsigned top=0;stack[top++]=node;
    while(top){DomNode *p=stack[--top];if(!p)continue;
        if(p->type==NODE_TEXT&&p->text){size_t len=strlen(p->text);
            if(n+len+1>1024u*1024u){free(out);return JS_ThrowRangeError(context,"textContent limit");}
            if(n+len+1>cap){cap=n+len+1;char *grown=realloc(out,cap);if(!grown){free(out);return JS_ThrowOutOfMemory(context);}out=grown;}
            memcpy(out+n,p->text,len);n+=len;
        }else for(DomNode *c=p->last_child;c;c=c->prev_sibling){
            if(top==sizeof(stack)/sizeof(stack[0])){free(out);return JS_ThrowRangeError(context,"DOM traversal limit");}stack[top++]=c;
        }
    }
    JSValue value=JS_NewStringLen(context,out,n);free(out);return value;
}
static void replace(DomNode *node,const char *value){
    while(node->first_child){DomNode *child=node->first_child;dom_remove_child(node,child);dom_free_tree(child);}
    if(*value){DomNode *child=dom_create_text(value,(int)strlen(value));if(child)dom_append_child(node,child);}
    g_browser.layout_dirty=1;
}
enum {P_TEXT,P_ID,P_CLASS,P_HTML,P_TAG,P_PARENT,P_FIRST,P_LAST,P_NEXT,P_PREV,P_CHILDREN,P_STYLE};
static JSValue node_get(JSContext *ctx,JSValueConst self,int magic){
    Binding *b=binding(self);if(!b||!b->node)return JS_ThrowTypeError(ctx,"detached DOM node");DomNode *n=b->node;
    switch(magic){
    case P_TEXT:return text(n);case P_ID:return JS_NewString(ctx,n->id);case P_CLASS:return JS_NewString(ctx,n->class_name);
    case P_TAG:return JS_NewString(ctx,n->tag);case P_STYLE:return JS_DupValue(ctx,b->style);
    case P_HTML:{char out[8192];dom_serialize_inner(n,out,sizeof(out));return JS_NewString(ctx,out);}
    case P_PARENT:return wrap(n->parent);case P_FIRST:return wrap(n->first_child);case P_LAST:return wrap(n->last_child);
    case P_NEXT:return wrap(n->next_sibling);case P_PREV:return wrap(n->prev_sibling);
    case P_CHILDREN:{JSValue array=JS_NewArray(ctx);uint32_t i=0;for(DomNode *p=n->first_child;p;p=p->next_sibling){
        if(p->type==NODE_ELEMENT)JS_SetPropertyUint32(ctx,array,i++,wrap(p));}return array;}
    }return JS_UNDEFINED;
}
static JSValue node_set(JSContext *ctx,JSValueConst self,JSValueConst value,int magic){
    Binding *b=binding(self);if(!b||!b->node)return JS_ThrowTypeError(ctx,"detached DOM node");
    const char *s=JS_ToCString(ctx,value);if(!s)return JS_EXCEPTION;
    if(magic==P_TEXT)replace(b->node,s);else if(magic==P_HTML)dom_set_inner_html(b->node,s,(int)strlen(s));
    else dom_set_attribute(b->node,magic==P_ID?"id":"class",s);
    JS_FreeCString(ctx,s);g_browser.layout_dirty=1;return JS_UNDEFINED;
}
enum {M_LISTEN,M_REMOVE_LISTENER,M_GET_ATTR,M_SET_ATTR,M_REMOVE_ATTR,M_APPEND,M_REMOVE,M_QUERY,M_QUERY_ALL};
static JSValue query(DomNode *root,const char *selector,int all){
    if(!all)return wrap(dom_query_selector(root,selector));
    DomNode *nodes[512];int count=0;dom_query_all(root,selector,nodes,512,&count);
    if(count==512)return JS_ThrowRangeError(context,"querySelectorAll limit");
    JSValue result=JS_NewArray(context);for(int i=0;i<count;++i)JS_SetPropertyUint32(context,result,(uint32_t)i,wrap(nodes[i]));return result;
}
static JSValue method(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic){
    Binding *b=binding(self);if(!b||!b->node)return JS_ThrowTypeError(ctx,"detached DOM node");
    if(magic==M_REMOVE){if(b->node->parent)dom_remove_child(b->node->parent,b->node);g_browser.layout_dirty=1;return JS_UNDEFINED;}
    if(magic==M_APPEND){Binding *child=argc?binding(argv[0]):NULL;
        if(!child||!child->node)return JS_ThrowTypeError(ctx,"appendChild requires a node");
        for(DomNode *p=b->node;p;p=p->parent)if(p==child->node)return JS_ThrowTypeError(ctx,"DOM hierarchy cycle");
        if(child->node->parent)dom_remove_child(child->node->parent,child->node);
        dom_append_child(b->node,child->node);g_browser.layout_dirty=1;return JS_DupValue(ctx,argv[0]);}
    if(!argc)return JS_UNDEFINED;const char *name=JS_ToCString(ctx,argv[0]);if(!name)return JS_EXCEPTION;
    JSValue result=JS_UNDEFINED;
    if(magic==M_LISTEN&&argc>1&&JS_IsFunction(ctx,argv[1])){
        if(listener_count==LISTENERS)result=JS_ThrowRangeError(ctx,"listener limit");else{
            Listener *l=&listeners[listener_count++];l->node=b->node;snprintf(l->type,sizeof(l->type),"%s",name);l->fn=JS_DupValue(ctx,argv[1]);
            puts("[browser] JS click listener registered");}
    }else if(magic==M_REMOVE_LISTENER&&argc>1){for(unsigned i=0;i<listener_count;i++)
        if(listeners[i].node==b->node&&!strcmp(listeners[i].type,name)&&JS_VALUE_GET_PTR(listeners[i].fn)==JS_VALUE_GET_PTR(argv[1])){
            JS_FreeValue(ctx,listeners[i].fn);listeners[i].fn=JS_UNDEFINED;listeners[i].node=NULL;}}
    else if(magic==M_GET_ATTR){const char *s=dom_get_attribute(b->node,name);result=s?JS_NewString(ctx,s):JS_NULL;}
    else if(magic==M_SET_ATTR&&argc>1){const char *s=JS_ToCString(ctx,argv[1]);if(s){dom_set_attribute(b->node,name,s);JS_FreeCString(ctx,s);g_browser.layout_dirty=1;}else result=JS_EXCEPTION;}
    else if(magic==M_REMOVE_ATTR){dom_remove_attribute(b->node,name);g_browser.layout_dirty=1;}
    else if(magic==M_QUERY||magic==M_QUERY_ALL)result=query(b->node,name,magic==M_QUERY_ALL);
    JS_FreeCString(ctx,name);return result;
}
#define PROP(n,id) JS_CGETSET_MAGIC_DEF(n,node_get,node_set,id)
static const JSCFunctionListEntry node_functions[]={
    PROP("textContent",P_TEXT),PROP("id",P_ID),PROP("className",P_CLASS),PROP("innerHTML",P_HTML),
    JS_CGETSET_MAGIC_DEF("tagName",node_get,NULL,P_TAG),JS_CGETSET_MAGIC_DEF("style",node_get,NULL,P_STYLE),
    JS_CGETSET_MAGIC_DEF("parentNode",node_get,NULL,P_PARENT),JS_CGETSET_MAGIC_DEF("firstChild",node_get,NULL,P_FIRST),
    JS_CGETSET_MAGIC_DEF("lastChild",node_get,NULL,P_LAST),JS_CGETSET_MAGIC_DEF("nextSibling",node_get,NULL,P_NEXT),
    JS_CGETSET_MAGIC_DEF("previousSibling",node_get,NULL,P_PREV),JS_CGETSET_MAGIC_DEF("children",node_get,NULL,P_CHILDREN),
    JS_CFUNC_MAGIC_DEF("addEventListener",2,method,M_LISTEN),JS_CFUNC_MAGIC_DEF("removeEventListener",2,method,M_REMOVE_LISTENER),
    JS_CFUNC_MAGIC_DEF("getAttribute",1,method,M_GET_ATTR),JS_CFUNC_MAGIC_DEF("setAttribute",2,method,M_SET_ATTR),
    JS_CFUNC_MAGIC_DEF("removeAttribute",1,method,M_REMOVE_ATTR),JS_CFUNC_MAGIC_DEF("appendChild",1,method,M_APPEND),
    JS_CFUNC_MAGIC_DEF("remove",0,method,M_REMOVE),JS_CFUNC_MAGIC_DEF("querySelector",1,method,M_QUERY),
    JS_CFUNC_MAGIC_DEF("querySelectorAll",1,method,M_QUERY_ALL)};
static JSValue wrap(DomNode *node){
    if(!node)return JS_NULL;for(unsigned i=0;i<binding_count;i++)if(bindings[i].node==node)return JS_DupValue(context,bindings[i].object);
    if(binding_count==BINDINGS)return JS_ThrowRangeError(context,"DOM binding limit");
    Binding *b=&bindings[binding_count++];b->node=node;b->object=JS_NewObjectClass(context,node_class);b->style=JS_NewObject(context);
    for(unsigned k=0;k<11;k++)b->last_style[k]=JS_UNDEFINED;
    JS_SetOpaque(b->object,b);JS_SetPropertyFunctionList(context,b->object,node_functions,sizeof(node_functions)/sizeof(node_functions[0]));
    return JS_DupValue(context,b->object);
}
enum {D_ID,D_QUERY,D_ALL,D_CREATE,D_TITLE,D_BODY};
static JSValue doc_method(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic){
    (void)self;if(!argc)return JS_NULL;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;JSValue value;
    if(magic==D_ID)value=wrap(dom_get_element_by_id(document,s));
    else if(magic==D_CREATE)value=wrap(dom_create_element(s));else value=query(document,s,magic==D_ALL);
    JS_FreeCString(ctx,s);return value;
}
static JSValue doc_get(JSContext *ctx,JSValueConst self,int magic){
    (void)self;return magic==D_TITLE?JS_NewString(ctx,title):wrap(dom_query_selector(document,"body"));
}
static JSValue doc_set(JSContext *ctx,JSValueConst self,JSValueConst value,int magic){
    (void)self;(void)magic;const char *s=JS_ToCString(ctx,value);if(!s)return JS_EXCEPTION;
    snprintf(title,sizeof(title),"%s",s);JS_FreeCString(ctx,s);return JS_UNDEFINED;
}
static JSValue log_message(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;for(int i=0;i<argc;i++){const char *s=JS_ToCString(ctx,argv[i]);if(!s)return JS_EXCEPTION;
        printf("[browser:js] %s\n",s);JS_FreeCString(ctx,s);}return JS_UNDEFINED;
}
static JSValue event_action(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic){
    (void)ctx;(void)self;(void)argc;(void)argv;if(magic)stopped=1;else prevented=1;return JS_UNDEFINED;
}
static void sync_styles(void){
    static const char *keys[]={"color","backgroundColor","background","fontSize","width","height","display","opacity","borderRadius","padding","margin"};
    static const char *css[]={"color","background-color","background","font-size","width","height","display","opacity","border-radius","padding","margin"};
    for(unsigned i=0;i<binding_count;i++)if(bindings[i].node)for(unsigned k=0;k<sizeof(keys)/sizeof(keys[0]);k++){
        JSValue value=JS_GetPropertyStr(context,bindings[i].style,keys[k]);if(JS_IsString(value)&&!JS_SameValue(context,value,bindings[i].last_style[k])){
            const char *s=JS_ToCString(context,value);if(s){css_apply_property(&bindings[i].node->style,css[k],s);JS_FreeCString(context,s);g_browser.layout_dirty=1;
                JS_FreeValue(context,bindings[i].last_style[k]);bindings[i].last_style[k]=JS_DupValue(context,value);}}
        JS_FreeValue(context,value);
    }
}
void js_dom_node_destroyed(DomNode *node){
    for(unsigned i=0;i<binding_count;i++)if(bindings[i].node==node)bindings[i].node=NULL;
    for(unsigned i=0;i<listener_count;i++)if(listeners[i].node==node){listeners[i].node=NULL;JS_FreeValue(context,listeners[i].fn);listeners[i].fn=JS_UNDEFINED;}
}
void browser_js_destroy(void){
    if(!runtime)return;
    for(unsigned i=0;i<listener_count;i++){JS_FreeValue(context,listeners[i].fn);listeners[i].fn=JS_UNDEFINED;listeners[i].node=NULL;}
    /* Created nodes which never entered the document still need reclamation. */
    for(unsigned i=0;i<binding_count;i++)if(bindings[i].node&&!bindings[i].node->parent&&bindings[i].node!=document)dom_free_tree(bindings[i].node);
    for(unsigned i=0;i<binding_count;i++){for(unsigned k=0;k<11;k++)JS_FreeValue(context,bindings[i].last_style[k]);JS_FreeValue(context,bindings[i].style);JS_FreeValue(context,bindings[i].object);}
    JS_FreeContext(context);JS_RunGC(runtime);JS_FreeRuntime(runtime);runtime=NULL;context=NULL;document=NULL;binding_count=listener_count=0;
}
void browser_js_init(DomNode *root){
    browser_js_destroy();document=root;title[0]=0;
    runtime=JS_NewRuntime();if(!runtime)return;JS_SetMemoryLimit(runtime,32u*1024u*1024u);JS_SetMaxStackSize(runtime,128u*1024u);
    JS_SetInterruptHandler(runtime,interrupt,NULL);context=JS_NewContext(runtime);if(!context){JS_FreeRuntime(runtime);runtime=NULL;return;}
    if(!node_class)JS_NewClassID(&node_class);JSClassDef definition={"PollikDOMNode",NULL,NULL,NULL,NULL};JS_NewClass(runtime,node_class,&definition);
    JSValue global=JS_GetGlobalObject(context),doc=JS_NewObject(context),console=JS_NewObject(context);
    const JSCFunctionListEntry functions[]={JS_CFUNC_MAGIC_DEF("getElementById",1,doc_method,D_ID),JS_CFUNC_MAGIC_DEF("querySelector",1,doc_method,D_QUERY),
        JS_CFUNC_MAGIC_DEF("querySelectorAll",1,doc_method,D_ALL),JS_CFUNC_MAGIC_DEF("createElement",1,doc_method,D_CREATE),
        JS_CGETSET_MAGIC_DEF("title",doc_get,doc_set,D_TITLE),JS_CGETSET_MAGIC_DEF("body",doc_get,NULL,D_BODY)};
    JS_SetPropertyFunctionList(context,doc,functions,sizeof(functions)/sizeof(functions[0]));JS_SetPropertyStr(context,global,"document",doc);
    JS_SetPropertyStr(context,global,"window",JS_DupValue(context,global));JS_SetPropertyStr(context,console,"log",JS_NewCFunction(context,log_message,"log",1));
    JS_SetPropertyStr(context,console,"warn",JS_NewCFunction(context,log_message,"warn",1));JS_SetPropertyStr(context,console,"error",JS_NewCFunction(context,log_message,"error",1));
    JS_SetPropertyStr(context,global,"console",console);JS_FreeValue(context,global);
    DomNode *name=dom_query_selector(root,"title");if(name){JSValue value=text(name);const char *s=JS_ToCString(context,value);if(s){snprintf(title,sizeof(title),"%s",s);JS_FreeCString(context,s);}JS_FreeValue(context,value);}
    puts("[browser] QuickJS 2026-06-04 runtime ready");
}
void browser_js_execute_source(const char *code,const char *name,int module){
    if(!context||!code)return;begin_budget();JSValue result=JS_Eval(context,code,strlen(code),name?name:"page.js",module?JS_EVAL_TYPE_MODULE:JS_EVAL_TYPE_GLOBAL);
    if(JS_IsException(result))error("script");JS_FreeValue(context,result);sync_styles();g_browser.layout_dirty=1;
    puts("[browser] JavaScript executed");
}
void browser_js_execute(const char *code){browser_js_execute_source(code,"page.js",0);}
void browser_js_poll(void){
    if(!context)return;begin_budget();JSContext *job_context;for(int i=0;i<32;i++){int n=JS_ExecutePendingJob(runtime,&job_context);if(n<=0){if(n<0)error("job");break;}}
    sync_styles();
}
int browser_js_dispatch_click(DomNode *node,int x,int y){
    if(!context)return 1;prevented=stopped=0;JSValue event=JS_NewObject(context);
    JS_SetPropertyStr(context,event,"target",wrap(node));JS_SetPropertyStr(context,event,"type",JS_NewString(context,"click"));
    JS_SetPropertyStr(context,event,"clientX",JS_NewInt32(context,x));JS_SetPropertyStr(context,event,"clientY",JS_NewInt32(context,y));
    JS_SetPropertyStr(context,event,"preventDefault",JS_NewCFunctionMagic(context,event_action,"preventDefault",0,JS_CFUNC_generic_magic,0));
    JS_SetPropertyStr(context,event,"stopPropagation",JS_NewCFunctionMagic(context,event_action,"stopPropagation",0,JS_CFUNC_generic_magic,1));
    for(DomNode *p=node;p&&!stopped;p=p->parent)for(unsigned i=0;i<listener_count;i++)if(listeners[i].node==p&&!strcmp(listeners[i].type,"click")){
        JS_SetPropertyStr(context,event,"currentTarget",wrap(p));begin_budget();JSValue self=wrap(p);JSValue result=JS_Call(context,listeners[i].fn,self,1,&event);
        if(JS_IsException(result))error("event");JS_FreeValue(context,result);JS_FreeValue(context,self);puts("[browser] JS click listener matched");}
    JS_FreeValue(context,event);sync_styles();g_browser.layout_dirty=1;return !prevented;
}
const char *browser_js_title(void){return title;}
