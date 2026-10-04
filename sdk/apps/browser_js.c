#define POLLIK_BROWSER_STANDALONE 1
#include "../../kernel/browser/browser.h"
#include "../../third_party/elk/elk.h"
#include <stdio.h>
#include <string.h>

extern jsval_t pollik_js_get(struct js *,jsval_t,const char *);
extern unsigned pollik_js_budget;

typedef struct { DomNode *node; jsval_t object,style; char original_text[512]; } JsNodeBinding;
typedef struct { DomNode *node; char event[16]; unsigned global_slot; } JsEventListener;
#define JS_BINDINGS 128
#define JS_LISTENERS 64
static struct js *runtime;
static unsigned char arena[131072] __attribute__((aligned(8)));
static JsNodeBinding bindings[JS_BINDINGS];
static unsigned binding_count;
static JsEventListener listeners[JS_LISTENERS];
static unsigned listener_count;
static int event_prevented;
static int event_stopped;
static DomNode *active_document;
static char page_title[64];

static void copy_js_string(jsval_t value,char *out,size_t cap) {
    size_t len=0;char *text=runtime?js_getstr(runtime,value,&len):0;
    if(!cap)return;
    if(!text){out[0]=0;return;}
    if(len>=cap)len=cap-1;
    memcpy(out,text,len);out[len]=0;
}
static void node_text(DomNode *node,char *out,size_t cap,size_t *used) {
    if(!node || *used+1>=cap)return;
    if(node->type==NODE_TEXT && node->text) {
        size_t n=strlen(node->text);if(n>cap-*used-1)n=cap-*used-1;
        memcpy(out+*used,node->text,n);*used+=n;out[*used]=0;return;
    }
    for(DomNode *child=node->first_child;child;child=child->next_sibling)node_text(child,out,cap,used);
}
static DomNode *node_for_id(const char *id) {
    return active_document?dom_get_element_by_id(active_document,id):0;
}
static jsval_t wrap_node(DomNode *node) {
    if(!node)return js_mknull();
    unsigned i=0;while(i<binding_count && bindings[i].node!=node)++i;
    if(i<binding_count)return bindings[i].object;
    if(binding_count>=JS_BINDINGS)return js_mknull();
    i=binding_count++;
    JsNodeBinding *b=&bindings[i];b->node=node;
    b->object=js_mkobj(runtime);b->style=js_mkobj(runtime);
    js_set(runtime,b->object,"style",b->style);
    js_set(runtime,b->object,"addEventListener",js_mkundef());
    char text[512];size_t used=0;text[0]=0;node_text(node,text,sizeof(text),&used);
    snprintf(b->original_text,sizeof(b->original_text),"%s",text);
    js_set(runtime,b->object,"textContent",js_mkstr(runtime,text,strlen(text)));
    js_set(runtime,b->object,"id",js_mkstr(runtime,node->id,strlen(node->id)));
    js_set(runtime,b->object,"className",js_mkstr(runtime,node->class_name,strlen(node->class_name)));
    js_set(runtime,b->style,"color",js_mkundef());
    js_set(runtime,b->style,"backgroundColor",js_mkundef());
    js_set(runtime,b->style,"fontSize",js_mkundef());
    js_set(runtime,b->style,"width",js_mkundef());
    js_set(runtime,b->style,"height",js_mkundef());
    char name[24],setup[128];snprintf(name,sizeof(name),"_pollik_node_%u",i);
    js_set(runtime,js_glob(runtime),name,b->object);
    snprintf(setup,sizeof(setup),"%s.addEventListener=function(t,f){_pollik_listen(%u,t,f);};",name,i);
    jsval_t setup_result=js_eval(runtime,setup,strlen(setup));
    if(js_type(setup_result)==JS_ERR)printf("[browser:js] listener setup: %s\n",js_str(runtime,setup_result));
    return b->object;
}
static void prewrap_interactive(DomNode *node) {
    if(!node || binding_count>=JS_BINDINGS)return;
    if(node->type==NODE_ELEMENT &&
       (node->id[0] || node->class_name[0] || !strcmp(node->tag,"a") ||
        !strcmp(node->tag,"button") || !strcmp(node->tag,"input") ||
        !strcmp(node->tag,"select") || !strcmp(node->tag,"textarea") ||
        !strcmp(node->tag,"form")))
        (void)wrap_node(node);
    for(DomNode *child=node->first_child;child && binding_count<JS_BINDINGS;child=child->next_sibling)
        prewrap_interactive(child);
}
static jsval_t get_by_id(struct js *js,jsval_t *args,int count) {
    (void)js;if(count<1)return js_mknull();
    char id[64];copy_js_string(args[0],id,sizeof(id));return wrap_node(node_for_id(id));
}
static jsval_t query_selector(struct js *js,jsval_t *args,int count) {
    (void)js;if(count<1 || !active_document)return js_mknull();
    char selector[96];copy_js_string(args[0],selector,sizeof(selector));
    return wrap_node(dom_query_selector(active_document,selector));
}
static jsval_t listen_event(struct js *js,jsval_t *args,int count) {
    if(count<3 || js_type(args[0])!=JS_NUM || listener_count>=JS_LISTENERS)return js_mkundef();
    unsigned binding=(unsigned)js_getnum(args[0]);
    if(binding>=binding_count || !bindings[binding].node)return js_mkundef();
    char type[16];copy_js_string(args[1],type,sizeof(type));if(!*type)return js_mkundef();
    unsigned slot=listener_count++;
    JsEventListener *entry=&listeners[slot];entry->node=bindings[binding].node;
    snprintf(entry->event,sizeof(entry->event),"%s",type);entry->global_slot=slot;
    char key[24];snprintf(key,sizeof(key),"_pollik_handler_%u",slot);
    js_set(js,js_glob(js),key,args[2]);
    puts("[browser] JS click listener registered");
    return js_mkundef();
}
static jsval_t prevent_default(struct js *js,jsval_t *args,int count) {
    (void)js;(void)args;(void)count;event_prevented=1;return js_mkundef();
}
static jsval_t stop_propagation(struct js *js,jsval_t *args,int count) {
    (void)js;(void)args;(void)count;event_stopped=1;return js_mkundef();
}
static jsval_t log_message(struct js *js,jsval_t *args,int count) {
    (void)js;if(count>0){char text[160];copy_js_string(args[0],text,sizeof(text));printf("[browser:js] %s\n",text);}
    return js_mkundef();
}
static void replace_text(DomNode *node,const char *text) {
    DomNode *child=node->first_child;
    if(child && child==node->last_child && child->type==NODE_TEXT) {
        size_t n=strlen(text);char *replacement=kmalloc((u32)n+1);
        if(!replacement)return;memcpy(replacement,text,n+1);kfree(child->text);child->text=replacement;
        return;
    }
    while(node->first_child) {
        DomNode *old=node->first_child;dom_remove_child(node,old);dom_free_tree(old);
    }
    if(*text) {DomNode *new_text=dom_create_text(text,(int)strlen(text));if(new_text)dom_append_child(node,new_text);}
}
static int is_descendant(DomNode *parent,DomNode *node) {
    for(DomNode *p=node?node->parent:0;p;p=p->parent)if(p==parent)return 1;
    return 0;
}
static void propagate_inherited_color(DomNode *node,u32 old_color,u32 new_color) {
    for(DomNode *child=node?node->first_child:0;child;child=child->next_sibling) {
        if(child->style.color!=old_color)continue;
        child->style.color=new_color;
        propagate_inherited_color(child,old_color,new_color);
    }
}
static int binding_text_changed(JsNodeBinding *binding) {
    jsval_t value=pollik_js_get(runtime,binding->object,"textContent");
    if(js_type(value)!=JS_STR)return 0;
    char text[512];copy_js_string(value,text,sizeof(text));return strcmp(text,binding->original_text)!=0;
}
static void sync_dom(void) {
    static const char *style_keys[]={"color","backgroundColor","fontSize","width","height"};
    static const char *css_keys[]={"color","background-color","font-size","width","height"};
    unsigned char stale[JS_BINDINGS]={0};
    for(unsigned i=0;i<binding_count;i++)if(bindings[i].node && binding_text_changed(&bindings[i]))
        for(unsigned j=0;j<binding_count;j++)if(j!=i && bindings[j].node && is_descendant(bindings[i].node,bindings[j].node))stale[j]=1;
    for(unsigned i=0;i<binding_count;i++)if(stale[i])bindings[i].node=0;
    for(unsigned i=0;i<binding_count;i++) {
        JsNodeBinding *b=&bindings[i];if(!b->node)continue;
        jsval_t value=pollik_js_get(runtime,b->object,"textContent");
        if(js_type(value)==JS_STR){char text[512];copy_js_string(value,text,sizeof(text));
            if(strcmp(text,b->original_text)){replace_text(b->node,text);snprintf(b->original_text,sizeof(b->original_text),"%s",text);}}
        for(unsigned p=0;p<sizeof(style_keys)/sizeof(style_keys[0]);p++) {
            value=pollik_js_get(runtime,b->style,style_keys[p]);
            if(js_type(value)==JS_STR){
                char css_value[64];copy_js_string(value,css_value,sizeof(css_value));
                u32 old_color=b->node->style.color;
                css_apply_property(&b->node->style,css_keys[p],css_value);
                if(p==0 && b->node->style.color!=old_color)
                    propagate_inherited_color(b->node,old_color,b->node->style.color);
            }
        }
    }
    jsval_t doc=pollik_js_get(runtime,js_glob(runtime),"document");
    jsval_t title=pollik_js_get(runtime,doc,"title");
    if(js_type(title)==JS_STR)copy_js_string(title,page_title,sizeof(page_title));
}

void browser_js_init(DomNode *document) {
    runtime=0;active_document=document;binding_count=0;listener_count=0;event_prevented=0;event_stopped=0;page_title[0]=0;
    memset(bindings,0,sizeof(bindings));
    memset(listeners,0,sizeof(listeners));
    runtime=js_create(arena,sizeof(arena));if(!runtime)return;
    js_setmaxcss(runtime,8192);
    jsval_t global=js_glob(runtime),doc=js_mkobj(runtime),console=js_mkobj(runtime);
    js_set(runtime,global,"document",doc);js_set(runtime,global,"window",global);
    js_set(runtime,doc,"getElementById",js_mkfun(get_by_id));
    js_set(runtime,doc,"querySelector",js_mkfun(query_selector));
    js_set(runtime,global,"_pollik_listen",js_mkfun(listen_event));
    prewrap_interactive(document);
    char title[64];size_t title_len=0;title[0]=0;
    node_text(dom_query_selector(document,"title"),title,sizeof(title),&title_len);
    js_set(runtime,doc,"title",js_mkstr(runtime,title,strlen(title)));
    js_set(runtime,doc,"body",wrap_node(dom_query_selector(document,"body")));
    js_set(runtime,global,"console",console);js_set(runtime,console,"log",js_mkfun(log_message));
    js_set(runtime,console,"warn",js_mkfun(log_message));js_set(runtime,console,"error",js_mkfun(log_message));
}
int browser_js_dispatch_click(DomNode *node,int x,int y) {
    if(!runtime || !node)return 1;
    event_prevented=event_stopped=0;
    jsval_t global=js_glob(runtime),event=js_mkobj(runtime);
    js_set(runtime,event,"type",js_mkstr(runtime,"click",5));
    js_set(runtime,event,"clientX",js_mknum(x));js_set(runtime,event,"clientY",js_mknum(y));
    js_set(runtime,event,"target",wrap_node(node));
    js_set(runtime,event,"currentTarget",wrap_node(node));
    js_set(runtime,event,"preventDefault",js_mkfun(prevent_default));
    js_set(runtime,event,"stopPropagation",js_mkfun(stop_propagation));
    js_set(runtime,global,"event",event);
    DomNode *chain[DOM_MAX_DEPTH];unsigned depth=0;
    for(DomNode *n=node;n && depth<DOM_MAX_DEPTH;n=n->parent)chain[depth++]=n;
    for(unsigned d=0;d<depth && !event_stopped;d++) {
        DomNode *current=chain[d];
        for(unsigned i=0;i<listener_count;i++) {
            JsEventListener *listener=&listeners[i];
            if(listener->node!=current || strcmp(listener->event,"click"))continue;
            puts("[browser] JS click listener matched");
            js_set(runtime,event,"currentTarget",wrap_node(current));
            char code[48];snprintf(code,sizeof(code),"_pollik_handler_%u(event);",listener->global_slot);
            jsval_t result=js_eval(runtime,code,strlen(code));
            if(js_type(result)==JS_ERR)puts("[browser] click handler failed");
        }
    }
    sync_dom();
    puts("[browser] click dispatched");
    return !event_prevented;
}
void browser_js_execute(const char *code) {
    if(!runtime || !code || !*code)return;
    pollik_js_budget=100000;
    jsval_t result=js_eval(runtime,code,strlen(code));
    if(js_type(result)==JS_ERR){
        const char *error=js_str(runtime,result);
        printf("[browser:js] error: %s\n",error?error:"unknown");return;
    }
    sync_dom();
    puts("[browser] JavaScript executed");
}
const char *browser_js_title(void) { return page_title; }
