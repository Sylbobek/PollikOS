#include "browser.h"

#include "../../third_party/elk/elk.h"
#include "../../third_party/elk/pollik_compat.h"
extern jsval_t pollik_js_get(struct js *,jsval_t,const char *);
extern unsigned pollik_js_budget;
static struct js *runtime;
static unsigned char arena[131072] __attribute__((aligned(8)));
static DomNode *g_doc;
static DomNode *bindings[128];
static int binding_count;
void js_init(void) {runtime=0;g_doc=0;binding_count=0;memset(bindings,0,sizeof(bindings));}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* DOM query helpers */
DomNode *dom_get_element_by_id(DomNode *root, const char *id) {
    if (!root) return 0;
    if (root->id[0] && str_eq(root->id, id)) return root;

    DomNode *child = root->first_child;
    while (child) {
        DomNode *found = dom_get_element_by_id(child, id);
        if (found) return found;
        child = child->next_sibling;
    }
    return 0;
}

DomNode *dom_query_selector(DomNode *root, const char *selector) {
    if (!root || !selector || !selector[0]) return 0;

    if (selector[0] == '#') {
        return dom_get_element_by_id(root, selector + 1);
    }

    if (selector[0] == '.') {
        const char *cls = selector + 1;
        if (root->class_name[0] && str_eq(root->class_name, cls)) return root;
        DomNode *child = root->first_child;
        while (child) {
            DomNode *found = dom_query_selector(child, selector);
            if (found) return found;
            child = child->next_sibling;
        }
        return 0;
    }

    /* Tag name */
    if (root->tag[0] && str_eq(root->tag, selector)) return root;
    DomNode *child = root->first_child;
    while (child) {
        DomNode *found = dom_query_selector(child, selector);
        if (found) return found;
        child = child->next_sibling;
    }
    return 0;
}

extern void dom_append_child(DomNode *, DomNode *);
void dom_remove_node(DomNode *node) {
    if (!node || !node->parent) return;
    DomNode *p = node->parent;
    if (node->prev_sibling) {
        node->prev_sibling->next_sibling = node->next_sibling;
    } else {
        p->first_child = node->next_sibling;
    }
    if (node->next_sibling) {
        node->next_sibling->prev_sibling = node->prev_sibling;
    } else {
        p->last_child = node->prev_sibling;
    }
    node->parent = 0;
    node->prev_sibling = 0;
    node->next_sibling = 0;
    browser_mark_dirty();
}

static u32 parse_color_str(const char *val) {
    if (str_eq(val, "red")) return 0xd32f2f;
    if (str_eq(val, "green")) return 0x388e3c;
    if (str_eq(val, "blue")) return 0x1976d2;
    if (str_eq(val, "white")) return 0xffffff;
    if (str_eq(val, "black")) return 0x000000;
    if (str_eq(val, "yellow")) return 0xfbc02d;
    if (str_eq(val, "purple")) return 0x7b1fa2;
    if (str_eq(val, "gray") || str_eq(val, "grey")) return 0x757575;
    if (val[0] == '#') {
        u32 c = 0;
        int i = 1;
        while (val[i]) {
            char ch = val[i++];
            u32 v = 0;
            if (ch >= '0' && ch <= '9') v = ch - '0';
            else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
            c = (c << 4) | v;
        }
        return c;
    }
    return 0x202020;
}

static int parse_int_str(const char *val) {
    int res = 0;
    while (*val >= '0' && *val <= '9') {
        res = res * 10 + (*val - '0');
        val++;
    }
    return res;
}

void dom_set_style_property(DomNode *node, const char *prop, const char *val) {
    if (!node || !prop || !val) return;

    if (str_eq(prop, "color")) {
        node->style.color = parse_color_str(val);
        for(DomNode *c=node->first_child;c;c=c->next_sibling)if(c->type==NODE_TEXT)c->style.color=node->style.color;
    } else if (str_eq(prop, "backgroundColor") || str_eq(prop, "background-color") || str_eq(prop, "background")) {
        node->style.bg_color = parse_color_str(val);
        node->style.has_bg_color = 1;
    } else if (str_eq(prop, "display")) {
        if (str_eq(val, "none")) node->style.display = DISPLAY_NONE;
        else if (str_eq(val, "block")) node->style.display = DISPLAY_BLOCK;
        else if (str_eq(val, "flex")) node->style.display = DISPLAY_FLEX;
        else if (str_eq(val, "inline")) node->style.display = DISPLAY_INLINE;
    } else if (str_eq(prop, "fontSize") || str_eq(prop, "font-size")) {
        int px=parse_int_str(val);node->style.font_size=px>=40?4:px>=26?3:px>=18?2:1;
    } else if (str_eq(prop, "borderRadius") || str_eq(prop, "border-radius")) {
        node->style.border_radius = parse_int_str(val);
    } else if (str_eq(prop, "width")) {
        node->style.width = parse_int_str(val);
    } else if (str_eq(prop, "height")) {
        node->style.height = parse_int_str(val);
    }
    browser_mark_dirty();
}

void dom_set_text_content(DomNode *node, const char *text) {
    if (!node || !text) return;
    if (node->type == NODE_TEXT) {
        if (node->text) kfree(node->text);
        int l = strlen(text);
        node->text = (char *)kmalloc(l + 1);
        if (node->text) {
            memcpy(node->text, text, l + 1);
        }
    } else {
        /* Find first text child or create one */
        DomNode *child = node->first_child;
        while (child && child->type != NODE_TEXT) {
            child = child->next_sibling;
        }
        if (child) {
            dom_set_text_content(child, text);
        } else {
            DomNode *tn = (DomNode *)kcalloc(1, sizeof(DomNode));
            if (tn) {
                tn->type = NODE_TEXT;
                int l = strlen(text);
                tn->text = (char *)kmalloc(l + 1);
                if (tn->text) memcpy(tn->text, text, l + 1);
                tn->style = node->style;
                dom_append_child(node, tn);
            }
        }
    }
    browser_mark_dirty();
}

void dom_add_event_listener(DomNode *node, const char *event_type, const char *js_code) {
    if (!node || !event_type || !js_code) return;
    EventListener *el = (EventListener *)kcalloc(1, sizeof(EventListener));
    if (!el) return;

    int i = 0;
    while (event_type[i] && i < (int)sizeof(el->event_type) - 1) {
        el->event_type[i] = event_type[i];
        i++;
    }
    el->event_type[i] = '\0';

    i = 0;
    while (js_code[i] && i < (int)sizeof(el->js_code) - 1) {
        el->js_code[i] = js_code[i];
        i++;
    }
    el->js_code[i] = '\0';

    el->next = node->listeners;
    node->listeners = el;
}


extern DomNode *dom_create_element(const char *);
static jsval_t get(jsval_t obj,const char *key){return pollik_js_get(runtime,obj,key);}
static void string(jsval_t v,char *out,int cap) {
 size_t n=0;char *p=js_getstr(runtime,v,&n);if(!p){out[0]=0;return;}
 if(n>=(size_t)cap)n=cap-1;memcpy(out,p,n);out[n]=0;
}
static DomNode *by_id(jsval_t v) {if(js_type(v)!=JS_NUM)return 0;int i=(int)js_getnum(v);return i>=0&&i<binding_count?bindings[i]:0;}
static jsval_t wrap(DomNode *n);
static jsval_t query(struct js *j,jsval_t *a,int count) {
 (void)j;if(count!=1)return js_mknull();char q[128];string(a[0],q,sizeof(q));return wrap(dom_query_selector(g_doc,q));
}
static jsval_t query_id(struct js *j,jsval_t *a,int count) {
 (void)j;if(count!=1)return js_mknull();char q[128];string(a[0],q,sizeof(q));return wrap(dom_get_element_by_id(g_doc,q));
}
static jsval_t create_node(struct js *j,jsval_t *a,int count) {
 (void)j;if(count!=1)return js_mknull();char q[16];string(a[0],q,sizeof(q));return wrap(dom_create_element(q));
}
static jsval_t append_node(struct js *j,jsval_t *a,int count) {
 (void)j;if(count!=2)return js_mkundef();DomNode *p=by_id(a[0]),*c=by_id(a[1]);if(!p||!c)return js_mkundef();
 for(DomNode *n=p;n;n=n->parent)if(n==c)return js_mkundef();
 dom_remove_node(c);dom_append_child(p,c);browser_mark_dirty();return wrap(c);
}
static jsval_t remove_node(struct js *j,jsval_t *a,int count){(void)j;if(count==1)dom_remove_node(by_id(a[0]));return js_mkundef();}
static jsval_t listen(struct js *j,jsval_t *a,int count) {
 if(count!=3 || !by_id(a[0]))return js_mkundef();char type[16],key[48];string(a[1],type,sizeof(type));
 if(!str_eq(type,"click")&&!str_eq(type,"load")&&!str_eq(type,"input")&&!str_eq(type,"change")&&!str_eq(type,"submit"))return js_mkundef();
 snprintf(key,sizeof(key),"_e%d_%s",(int)js_getnum(a[0]),type);js_set(j,js_glob(j),key,a[2]);return js_mkundef();
}
static jsval_t wrap(DomNode *n) {
 if(!n)return js_mknull();int i=0;while(i<binding_count&&bindings[i]!=n)i++;
 char name[16];snprintf(name,sizeof(name),"_n%d",i);
 if(i<binding_count)return get(js_glob(runtime),name);
 if(binding_count==128)return js_mknull();bindings[binding_count++]=n;
 jsval_t obj=js_mkobj(runtime);js_set(runtime,js_glob(runtime),name,obj);
 js_set(runtime,obj,"__node",js_mknum(i));
 jsval_t style=js_mkobj(runtime);js_set(runtime,obj,"style",style);
 const char *properties[]={"color","backgroundColor","display","fontSize","width","height","borderRadius"};
 for(unsigned k=0;k<sizeof(properties)/sizeof(properties[0]);k++)js_set(runtime,style,properties[k],js_mkundef());
 js_set(runtime,obj,"appendChild",js_mkundef());js_set(runtime,obj,"remove",js_mkundef());js_set(runtime,obj,"addEventListener",js_mkundef());
 const char *text=n->text?n->text:(n->first_child&&n->first_child->text?n->first_child->text:"");
 js_set(runtime,obj,"textContent",js_mkstr(runtime,text,strlen(text)));
 js_set(runtime,obj,"className",js_mkstr(runtime,n->class_name,strlen(n->class_name)));
 js_set(runtime,obj,"id",js_mkstr(runtime,n->id,strlen(n->id)));
 char code[512];snprintf(code,sizeof(code),"%s.appendChild=function(c){return _append(%d,c.__node);};%s.remove=function(){_remove(%d);};%s.addEventListener=function(t,f){_listen(%d,t,f);};",name,i,name,i,name,i);
 js_eval(runtime,code,strlen(code));return get(js_glob(runtime),name);
}
static void sync_dom(void) {
 static const char *props[]={"color","backgroundColor","display","fontSize","width","height","borderRadius"};
 for(int i=0;i<binding_count;i++) {
  DomNode *n=bindings[i];if(!n)continue;char name[16];snprintf(name,sizeof(name),"_n%d",i);
  jsval_t obj=get(js_glob(runtime),name),style=get(obj,"style");
  for(unsigned k=0;k<sizeof(props)/sizeof(props[0]);k++){jsval_t v=get(style,props[k]);if(js_type(v)==JS_STR){char val[64];string(v,val,sizeof(val));dom_set_style_property(n,props[k],val);}}
  jsval_t tv=get(obj,"textContent");if(js_type(tv)==JS_STR){char val[256];string(tv,val,sizeof(val));const char *old=n->text?n->text:(n->first_child&&n->first_child->text?n->first_child->text:"");if(!str_eq(val,old))dom_set_text_content(n,val);}
 }
 jsval_t title=get(get(js_glob(runtime),"document"),"title");
 if(js_type(title)==JS_STR) {
  char previous[sizeof(g_browser.title)];memcpy(previous,g_browser.title,sizeof(previous));
  string(title,g_browser.title,sizeof(g_browser.title));
  if(!str_eq(previous,g_browser.title)) {serial("BROWSER title: ");serial(g_browser.title);serial("\n");}
 }
}
void js_execute(const char *code,DomNode *document) {
 if(!code||!document)return;g_doc=document;pollik_js_budget=100000;
 if(!runtime) {
  runtime=js_create(arena,sizeof(arena));if(!runtime)return;js_setmaxcss(runtime,8192);
  jsval_t global=js_glob(runtime),doc=js_mkobj(runtime);js_set(runtime,global,"document",doc);
  js_set(runtime,doc,"querySelector",js_mkfun(query));js_set(runtime,doc,"getElementById",js_mkfun(query_id));js_set(runtime,doc,"createElement",js_mkfun(create_node));
  js_set(runtime,doc,"title",js_mkstr(runtime,g_browser.title,strlen(g_browser.title)));
  js_set(runtime,global,"_append",js_mkfun(append_node));js_set(runtime,global,"_remove",js_mkfun(remove_node));js_set(runtime,global,"_listen",js_mkfun(listen));
  jsval_t body=wrap(dom_query_selector(document,"body"));doc=get(js_glob(runtime),"document");js_set(runtime,doc,"body",body);
 }
 jsval_t result=js_eval(runtime,code,strlen(code));
 if(js_type(result)==JS_ERR){g_browser.script_errors++;serial("JS: ");serial(js_str(runtime,result));serial("\n");}
 sync_dom();browser_mark_dirty();
}
void js_dispatch_event(DomNode *node,const char *type) {
 if(!node||!type)return;
 for(DomNode *n=node;n;n=n->parent) {
  for(int i=0;i<binding_count;i++)if(bindings[i]==n) {
   char key[48],code[64];snprintf(key,sizeof(key),"_e%d_%s",i,type);
   if(runtime && js_type(get(js_glob(runtime),key))!=JS_UNDEF){snprintf(code,sizeof(code),"%s();",key);js_execute(code,g_doc);}
  }
  for(EventListener *e=n->listeners;e;e=e->next)if(str_eq(e->event_type,type))js_execute(e->js_code,g_doc);
 }
}
