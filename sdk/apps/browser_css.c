/* libcss owns parsing, selector matching, specificity and the cascade.
 * This adapter maps computed values into PollikOS's existing layout boxes. */
#define POLLIK_BROWSER_STANDALONE 1
#include "../../kernel/browser/browser.h"
#include <libcss/libcss.h>
#include <libcss/unit.h>
#include <libwapcaplet/libwapcaplet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern void css_apply_styles_legacy(DomNode *,const char *);
typedef struct StyleState {
    DomNode *node;void *cache;css_computed_style *style;
    lwc_string **classes;uint32_t count;struct StyleState *next;
} StyleState;
static StyleState *states;
static css_select_handler handler;
static StyleState *state(DomNode *node) {
    if(node->upstream_style_data)return node->upstream_style_data;
    StyleState *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->node=node;s->next=states;states=s;node->upstream_style_data=s;return s;
}
static css_error intern(const char *text,lwc_string **out) {
    return lwc_intern_string(text,strlen(text),out)==lwc_error_ok?CSS_OK:CSS_NOMEM;
}
static int same(const char *text,lwc_string *value) {
    return strlen(text)==lwc_string_length(value) && !memcmp(text,lwc_string_data(value),strlen(text));
}
static DomNode *parent(DomNode *node) { return node->parent && node->parent->type==NODE_ELEMENT?node->parent:NULL; }
static DomNode *previous(DomNode *node) { node=node->prev_sibling;while(node && node->type!=NODE_ELEMENT)node=node->prev_sibling;return node; }
static css_error node_name(void *pw,void *node,css_qname *q) {(void)pw;q->ns=NULL;return intern(((DomNode *)node)->tag,&q->name);}
static css_error node_id(void *pw,void *node,lwc_string **id) {(void)pw;const char *v=dom_get_attribute(node,"id");*id=NULL;return v && *v?intern(v,id):CSS_OK;}
static css_error node_classes(void *pw,void *node,lwc_string ***out,uint32_t *count) {
    (void)pw;StyleState *s=state(node);if(!s)return CSS_NOMEM;
    if(!s->classes) {
        const char *value=dom_get_attribute(node,"class");if(!value)value="";
        unsigned tokens=0;for(const char *p=value;*p;){while(*p && *p<=' ')p++;if(!*p)break;++tokens;while(*p && *p>' ')p++;}
        if(tokens){s->classes=calloc(tokens,sizeof(*s->classes));if(!s->classes)return CSS_NOMEM;}
        for(const char *p=value;*p;){while(*p && *p<=' ')p++;const char *start=p;while(*p && *p>' ')p++;
            if(p>start){if(lwc_intern_string(start,(size_t)(p-start),&s->classes[s->count])!=lwc_error_ok)return CSS_NOMEM;++s->count;}}
    }
    *out=s->classes;*count=s->count;for(unsigned i=0;i<s->count;i++)lwc_string_ref(s->classes[i]);return CSS_OK;
}
static css_error parent_node(void *pw,void *node,void **out){(void)pw;*out=parent(node);return CSS_OK;}
static css_error sibling_node(void *pw,void *node,void **out){(void)pw;*out=previous(node);return CSS_OK;}
static css_error named_parent(void *pw,void *node,const css_qname *q,void **out){(void)pw;DomNode *p=parent(node);*out=p && same(p->tag,q->name)?p:NULL;return CSS_OK;}
static css_error named_ancestor(void *pw,void *node,const css_qname *q,void **out){(void)pw;*out=NULL;for(DomNode *p=parent(node);p;p=parent(p))if(same(p->tag,q->name)){*out=p;break;}return CSS_OK;}
static css_error named_sibling(void *pw,void *node,const css_qname *q,void **out){(void)pw;DomNode *p=previous(node);*out=p && same(p->tag,q->name)?p:NULL;return CSS_OK;}
static css_error named_generic(void *pw,void *node,const css_qname *q,void **out){(void)pw;*out=NULL;for(DomNode *p=previous(node);p;p=previous(p))if(same(p->tag,q->name)){*out=p;break;}return CSS_OK;}
static css_error has_name(void *pw,void *node,const css_qname *q,bool *match){(void)pw;*match=same(((DomNode *)node)->tag,q->name);return CSS_OK;}
static css_error has_class(void *pw,void *node,lwc_string *name,bool *match){(void)pw;*match=false;const char *v=dom_get_attribute(node,"class");if(v)for(const char *p=v;*p;){while(*p && *p<=' ')p++;const char *s=p;while(*p && *p>' ')p++;if((size_t)(p-s)==lwc_string_length(name) && !memcmp(s,lwc_string_data(name),(size_t)(p-s))){*match=true;break;}}return CSS_OK;}
static css_error has_id(void *pw,void *node,lwc_string *name,bool *match){(void)pw;const char *v=dom_get_attribute(node,"id");*match=v && same(v,name);return CSS_OK;}
static const char *attribute(void *node,const css_qname *q) {
    char name[DOM_ATTR_NAME_MAX];size_t n=lwc_string_length(q->name);if(n>=sizeof(name))return NULL;
    memcpy(name,lwc_string_data(q->name),n);name[n]=0;return dom_get_attribute(node,name);
}
static css_error has_attribute(void *pw,void *node,const css_qname *q,bool *match){(void)pw;*match=attribute(node,q)!=NULL;return CSS_OK;}
static css_error attr_match(void *node,const css_qname *q,lwc_string *value,bool *match,int mode) {
    const char *v=attribute(node,q),*needle=lwc_string_data(value);size_t n=lwc_string_length(value);*match=false;
    if(!v)return CSS_OK;size_t len=strlen(v);
    if(mode==0)*match=len==n && !memcmp(v,needle,n);
    else if(mode==1)*match=len>=n && !memcmp(v,needle,n) && (len==n || v[n]=='-');
    else if(mode==2 && n)for(const char *p=v;*p;){while(*p && *p<=' ')p++;const char *s=p;while(*p && *p>' ')p++;if((size_t)(p-s)==n && !memcmp(s,needle,n)){*match=true;break;}}
    else if(mode==3 && n)*match=len>=n && !memcmp(v,needle,n);
    else if(mode==4 && n)*match=len>=n && !memcmp(v+len-n,needle,n);
    else if(mode==5 && n)for(size_t i=0;i+n<=len;i++)if(!memcmp(v+i,needle,n)){*match=true;break;}
    return CSS_OK;
}
#define ATTR_FN(name,mode) static css_error name(void *pw,void *node,const css_qname *q,lwc_string *v,bool *m){(void)pw;return attr_match(node,q,v,m,mode);}
ATTR_FN(attr_equal,0) ATTR_FN(attr_dash,1) ATTR_FN(attr_includes,2) ATTR_FN(attr_prefix,3) ATTR_FN(attr_suffix,4) ATTR_FN(attr_substring,5)
static css_error is_root(void *pw,void *node,bool *m){(void)pw;*m=parent(node)==NULL;return CSS_OK;}
static css_error siblings(void *pw,void *node,bool same_name,bool after,int32_t *count){(void)pw;DomNode *n=node;*count=0;for(DomNode *p=after?n->next_sibling:n->prev_sibling;p;p=after?p->next_sibling:p->prev_sibling)if(p->type==NODE_ELEMENT && (!same_name || !strcmp(p->tag,n->tag)))++*count;return CSS_OK;}
static css_error empty_node(void *pw,void *node,bool *m){(void)pw;*m=true;for(DomNode *p=((DomNode *)node)->first_child;p;p=p->next_sibling)if(p->type==NODE_ELEMENT || (p->type==NODE_TEXT && p->text && *p->text)){*m=false;break;}return CSS_OK;}
static css_error link_node(void *pw,void *node,bool *m){(void)pw;DomNode *n=node;*m=(!strcmp(n->tag,"a")||!strcmp(n->tag,"area")) && dom_get_attribute(n,"href");return CSS_OK;}
static css_error not_visited(void *pw,void *node,bool *m){(void)pw;(void)node;*m=false;return CSS_OK;}
static int inside(DomNode *n,DomNode *ancestor){for(;n;n=n->parent)if(n==ancestor)return 1;return 0;}
static css_error hover_node(void *pw,void *node,bool *m){(void)pw;*m=inside(g_browser.hover_node,node);return CSS_OK;}
static css_error active_node(void *pw,void *node,bool *m){(void)pw;*m=inside(g_browser.active_node,node);return CSS_OK;}
static css_error focus_node(void *pw,void *node,bool *m){(void)pw;*m=node==g_browser.focused_input;return CSS_OK;}
static css_error disabled_node(void *pw,void *node,bool *m){(void)pw;*m=dom_get_attribute(node,"disabled")!=NULL;return CSS_OK;}
static css_error enabled_node(void *pw,void *node,bool *m){css_error e=disabled_node(pw,node,m);*m=!*m;return e;}
static css_error checked_node(void *pw,void *node,bool *m){(void)pw;*m=dom_get_attribute(node,"checked")!=NULL;return CSS_OK;}
static css_error target_node(void *pw,void *node,bool *m){(void)pw;*m=false;const char *hash=strchr(g_browser.url,'#');if(hash)*m=!strcmp(((DomNode *)node)->id,hash+1);return CSS_OK;}
static css_error lang_node(void *pw,void *node,lwc_string *lang,bool *m){(void)pw;*m=false;for(DomNode *n=node;n;n=parent(n)){const char *v=dom_get_attribute(n,"lang");if(v){size_t len=strlen(v),l=lwc_string_length(lang);*m=len>=l && !memcmp(v,lwc_string_data(lang),l) && (len==l || v[l]=='-');break;}}return CSS_OK;}
static css_error hints(void *pw,void *node,uint32_t *count,css_hint **out){(void)pw;(void)node;*count=0;*out=NULL;return CSS_OK;}
static css_error defaults(void *pw,uint32_t property,css_hint *hint){(void)pw;
    if(property==CSS_PROP_COLOR){hint->data.color=0xff222222;hint->status=CSS_COLOR_COLOR;}
    else if(property==CSS_PROP_FONT_FAMILY){hint->data.strings=NULL;hint->status=CSS_FONT_FAMILY_SANS_SERIF;}
    else if(property==CSS_PROP_QUOTES){hint->data.strings=NULL;hint->status=CSS_QUOTES_NONE;}
    else if(property==CSS_PROP_VOICE_FAMILY){hint->data.strings=NULL;hint->status=0;}
    else return CSS_INVALID;return CSS_OK;
}
static css_error set_data(void *pw,void *node,void *data){(void)pw;StyleState *s=state(node);if(!s)return CSS_NOMEM;s->cache=data;return CSS_OK;}
static css_error get_data(void *pw,void *node,void **data){(void)pw;StyleState *s=((DomNode *)node)->upstream_style_data;*data=s?s->cache:NULL;return CSS_OK;}
static css_select_handler handler={
    .handler_version=CSS_SELECT_HANDLER_VERSION_1,.node_name=node_name,.node_classes=node_classes,.node_id=node_id,
    .named_ancestor_node=named_ancestor,.named_parent_node=named_parent,.named_sibling_node=named_sibling,.named_generic_sibling_node=named_generic,
    .parent_node=parent_node,.sibling_node=sibling_node,.node_has_name=has_name,.node_has_class=has_class,.node_has_id=has_id,
    .node_has_attribute=has_attribute,.node_has_attribute_equal=attr_equal,.node_has_attribute_dashmatch=attr_dash,.node_has_attribute_includes=attr_includes,
    .node_has_attribute_prefix=attr_prefix,.node_has_attribute_suffix=attr_suffix,.node_has_attribute_substring=attr_substring,
    .node_is_root=is_root,.node_count_siblings=siblings,.node_is_empty=empty_node,.node_is_link=link_node,.node_is_visited=not_visited,
    .node_is_hover=hover_node,.node_is_active=active_node,.node_is_focus=focus_node,.node_is_enabled=enabled_node,.node_is_disabled=disabled_node,.node_is_checked=checked_node,
    .node_is_target=target_node,.node_is_lang=lang_node,.node_presentational_hint=hints,.ua_default_for_property=defaults,.set_libcss_node_data=set_data,.get_libcss_node_data=get_data
};
static css_error resolve_url(void *pw,const char *base,lwc_string *relative,lwc_string **absolute){(void)pw;(void)base;*absolute=lwc_string_ref(relative);return CSS_OK;}
static css_stylesheet *sheet(const char *source,int inline_style) {
    css_stylesheet_params parameters={0};parameters.params_version=CSS_STYLESHEET_PARAMS_VERSION_1;
    parameters.level=CSS_LEVEL_3;parameters.charset="UTF-8";parameters.url="about:document";parameters.title="";
    parameters.inline_style=inline_style!=0;parameters.resolve=resolve_url;
    css_stylesheet *result=NULL;css_error e=css_stylesheet_create(&parameters,&result);
    if(e==CSS_OK)e=css_stylesheet_append_data(result,(const uint8_t *)source,strlen(source));
    if(e==CSS_OK || e==CSS_NEEDDATA)e=css_stylesheet_data_done(result);
    if(e!=CSS_OK && e!=CSS_IMPORTS_PENDING){if(result)css_stylesheet_destroy(result);return NULL;}
    return result;
}
static void collect(DomNode *node,char *text,size_t *used) {
    if(!node)return;
    if(!strcmp(node->tag,"style"))for(DomNode *c=node->first_child;c;c=c->next_sibling)if(c->text){size_t n=strlen(c->text);if(n>WEB_MAX_CSS_SIZE-2-*used)n=WEB_MAX_CSS_SIZE-2-*used;memcpy(text+*used,c->text,n);*used+=n;text[(*used)++]='\n';}
    for(DomNode *c=node->first_child;c;c=c->next_sibling)collect(c,text,used);
}
static int pixels(css_fixed value,css_unit unit,const css_computed_style *style,const css_unit_ctx *context,int base) {
    if(unit==CSS_UNIT_PCT)return (int)(((int64_t)value*base)/(1024*100));
    return FIXTOINT(css_unit_len2css_px(style,context,value,unit));
}
static int length_value(uint8_t type,css_fixed value,css_unit unit,const css_computed_style *style,const css_unit_ctx *context,int base){return type==1?pixels(value,unit,style,context,base):type==2?-2:-1;}
static void map_style(DomNode *node,const css_computed_style *style,const css_unit_ctx *context) {
    ComputedStyle *out=&node->style;css_color color;css_fixed value;css_unit unit;uint8_t type;
    if(css_computed_color(style,&color)==CSS_COLOR_COLOR)out->color=color&0xffffff;
    if(css_computed_background_color(style,&color)==CSS_BACKGROUND_COLOR_COLOR){out->bg_color=color&0xffffff;out->has_bg_color=(color>>24)!=0;}
    DomNode *p=parent(node);int available=p && p->style.width>=0?p->style.width:FIXTOINT(context->viewport_width);
    int width;if(css_computed_width_px(style,context,available,&width)==CSS_WIDTH_SET)out->width=width;else out->width=-1;
    type=css_computed_height(style,&value,&unit);out->height=type==CSS_HEIGHT_SET?pixels(value,unit,style,context,FIXTOINT(context->viewport_height)):-1;
    #define MARGIN(property) type=css_computed_##property(style,&value,&unit);out->property=length_value(type,value,unit,style,context,available);
    MARGIN(margin_top) MARGIN(margin_bottom) MARGIN(margin_left) MARGIN(margin_right)
    #undef MARGIN
    #define LENGTH(property,field) type=css_computed_##property(style,&value,&unit);out->field=type==1?pixels(value,unit,style,context,available):-1;
    LENGTH(padding_top,padding_top) LENGTH(padding_bottom,padding_bottom) LENGTH(padding_left,padding_left) LENGTH(padding_right,padding_right)
    LENGTH(min_width,min_width) LENGTH(max_width,max_width) LENGTH(min_height,min_height) LENGTH(max_height,max_height)
    LENGTH(top,top) LENGTH(bottom,bottom) LENGTH(left,left) LENGTH(right,right)
    #undef LENGTH
    type=css_computed_border_top_width(style,&value,&unit);out->border_width=type==CSS_BORDER_WIDTH_WIDTH?pixels(value,unit,style,context,available):type==CSS_BORDER_WIDTH_THIN?1:type==CSS_BORDER_WIDTH_MEDIUM?3:type==CSS_BORDER_WIDTH_THICK?5:0;
    if(css_computed_border_top_style(style)==CSS_BORDER_STYLE_NONE || css_computed_border_top_style(style)==CSS_BORDER_STYLE_HIDDEN)out->border_width=0;
    if(css_computed_border_top_color(style,&color)==CSS_BORDER_COLOR_COLOR)out->border_color=color&0xffffff;
    type=css_computed_display(style,parent(node)==NULL);
    out->display=type==CSS_DISPLAY_NONE?DISPLAY_NONE:type==CSS_DISPLAY_INLINE?DISPLAY_INLINE:
        type==CSS_DISPLAY_INLINE_BLOCK?DISPLAY_INLINE_BLOCK:(type==CSS_DISPLAY_FLEX || type==CSS_DISPLAY_INLINE_FLEX)?DISPLAY_FLEX:DISPLAY_BLOCK;
    type=css_computed_font_size(style,&value,&unit);int font=type==CSS_FONT_SIZE_DIMENSION?pixels(value,unit,style,context,available):14;
    static const int sizes[]={14,18,26,40,52};unsigned nearest=0;for(unsigned i=1;i<5;i++)if(abs(sizes[i]-font)<abs(sizes[nearest]-font))nearest=i;out->font_size=(int)nearest+1;
    type=css_computed_font_weight(style);out->font_weight=type==CSS_FONT_WEIGHT_BOLD || type==CSS_FONT_WEIGHT_BOLDER?700:type>=CSS_FONT_WEIGHT_100 && type<=CSS_FONT_WEIGHT_900?(type-CSS_FONT_WEIGHT_100+1)*100:400;
    type=css_computed_line_height(style,&value,&unit);out->line_height=type==CSS_LINE_HEIGHT_NUMBER?FIXTOINT(FMUL(value,INTTOFIX(font))):type==CSS_LINE_HEIGHT_DIMENSION?pixels(value,unit,style,context,available):font+4;
    type=css_computed_text_align(style);out->text_align=type==CSS_TEXT_ALIGN_CENTER?TEXT_ALIGN_CENTER:type==CSS_TEXT_ALIGN_RIGHT?TEXT_ALIGN_RIGHT:TEXT_ALIGN_LEFT;
    type=css_computed_position(style);out->position=type==CSS_POSITION_ABSOLUTE?POS_ABSOLUTE:type==CSS_POSITION_FIXED?POS_FIXED:type==CSS_POSITION_RELATIVE?POS_RELATIVE:POS_STATIC;
    if(css_computed_opacity(style,&value)==CSS_OPACITY_SET)out->opacity=(int)(((int64_t)value*256)/1024);
    out->visible=css_computed_visibility(style)==CSS_VISIBILITY_VISIBLE;
    type=css_computed_white_space(style);out->white_space_pre=type==CSS_WHITE_SPACE_PRE || type==CSS_WHITE_SPACE_PRE_WRAP;
    type=css_computed_flex_direction(style);out->flex_dir=type==CSS_FLEX_DIRECTION_COLUMN || type==CSS_FLEX_DIRECTION_COLUMN_REVERSE?FLEX_DIR_COLUMN:FLEX_DIR_ROW;
    type=css_computed_justify_content(style);out->justify=type==CSS_JUSTIFY_CONTENT_CENTER?JUSTIFY_CENTER:type==CSS_JUSTIFY_CONTENT_SPACE_BETWEEN?JUSTIFY_BETWEEN:type==CSS_JUSTIFY_CONTENT_FLEX_END?JUSTIFY_END:JUSTIFY_START;
    type=css_computed_align_items(style);out->align_items=type==CSS_ALIGN_ITEMS_CENTER?ALIGN_ITEMS_CENTER:type==CSS_ALIGN_ITEMS_FLEX_END?ALIGN_ITEMS_END:type==CSS_ALIGN_ITEMS_STRETCH?ALIGN_ITEMS_STRETCH:ALIGN_ITEMS_START;
}
static css_error walk(DomNode *node,css_select_ctx *context,css_computed_style *default_style,css_unit_ctx *units,const css_media *media) {
    if(node->type==NODE_ELEMENT) {
        StyleState *s=state(node);if(!s)return CSS_NOMEM;
        const char *inline_text=dom_get_attribute(node,"style");css_stylesheet *inline_sheet=inline_text && *inline_text?sheet(inline_text,1):NULL;
        css_select_results *result=NULL;css_error e=css_select_style(context,node,units,media,inline_sheet,&handler,NULL,&result);
        if(inline_sheet)css_stylesheet_destroy(inline_sheet);
        if(e!=CSS_OK)return e;
        DomNode *p=parent(node);StyleState *ps=p?p->upstream_style_data:NULL;
        e=css_computed_style_compose(ps && ps->style?ps->style:default_style,result->styles[CSS_PSEUDO_ELEMENT_NONE],units,&s->style);
        css_select_results_destroy(result);if(e!=CSS_OK)return e;
        if(!p)units->root_style=s->style;map_style(node,s->style,units);
    } else if(node->type==NODE_TEXT && node->parent){node->style=node->parent->style;node->style.display=DISPLAY_INLINE;}
    for(DomNode *child=node->first_child;child;child=child->next_sibling){css_error e=walk(child,context,default_style,units,media);if(e!=CSS_OK)return e;}
    return CSS_OK;
}
void css_apply_styles(DomNode *root,const char *extra) {
    if(!root)return;
    /* Preserve existing layout extensions which libcss does not expose. */
    css_apply_styles_legacy(root,extra);
    char *source=malloc(WEB_MAX_CSS_SIZE);if(!source)return;size_t used=0;collect(root,source,&used);
    if(extra){size_t n=strlen(extra);if(n>WEB_MAX_CSS_SIZE-1-used)n=WEB_MAX_CSS_SIZE-1-used;memcpy(source+used,extra,n);used+=n;}source[used]=0;
    static const char ua[]="html,body,div,p,h1,h2,h3,h4,h5,h6,section,article,main,header,footer,nav,ul,ol,li,form,pre,table,tr{display:block}head,meta,script,style,title,link,noscript{display:none}body{margin:8px;font-size:14px;color:#222}p{margin:0 0 12px}h1{font-size:26px;margin:14px 0}h2{font-size:18px;margin:12px 0}a{color:#2868bd}img,canvas,input,button,textarea{display:inline-block}pre{white-space:pre}input,button,textarea{font-size:14px;border:1px solid #777;padding:4px}";
    css_stylesheet *base=sheet(ua,0),*author=sheet(source,0);free(source);
    css_select_ctx *context=NULL;css_computed_style *default_style=NULL;css_error error=base && author?css_select_ctx_create(&context):CSS_NOMEM;
    if(error==CSS_OK)error=css_select_ctx_append_sheet(context,base,CSS_ORIGIN_UA,NULL);
    if(error==CSS_OK)error=css_select_ctx_append_sheet(context,author,CSS_ORIGIN_AUTHOR,NULL);
    if(error==CSS_OK)error=css_select_default_style(context,&handler,NULL,&default_style);
    css_unit_ctx units={.viewport_width=INTTOFIX(g_browser.w>20?g_browser.w-20:896),.viewport_height=INTTOFIX(g_browser.h>110?g_browser.h-110:501),.font_size_default=INTTOFIX(14),.device_dpi=INTTOFIX(96)};
    css_media media={0};media.type=CSS_MEDIA_SCREEN;media.width=units.viewport_width;media.height=units.viewport_height;media.color=INTTOFIX(8);
    if(error==CSS_OK)error=walk(root,context,default_style,&units,&media);
    while(states){StyleState *s=states;states=s->next;if(s->cache)css_libcss_node_data_handler(&handler,CSS_NODE_DELETED,NULL,s->node,NULL,s->cache);
        if(s->style)css_computed_style_destroy(s->style);for(unsigned i=0;i<s->count;i++)lwc_string_unref(s->classes[i]);free(s->classes);s->node->upstream_style_data=NULL;free(s);}
    if(default_style)css_computed_style_destroy(default_style);if(context)css_select_ctx_destroy(context);if(author)css_stylesheet_destroy(author);if(base)css_stylesheet_destroy(base);
    if(error!=CSS_OK)printf("[browser:css] libcss error=%u\n",(unsigned)error);
}
