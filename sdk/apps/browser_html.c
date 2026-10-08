/* HTML5 tree construction comes from the pinned libdom/libhubbub libraries.
 * The existing layout tree remains the renderer/QuickJS ownership boundary. */
#define POLLIK_BROWSER_STANDALONE 1
#include "../../kernel/browser/browser.h"
#include <dom/dom.h>
#include "../../third_party/libdom/bindings/hubbub/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *string_copy(dom_string *string) {
    size_t length=dom_string_byte_length(string);
    char *text=malloc(length+1);if(!text)return NULL;
    memcpy(text,dom_string_data(string),length);text[length]=0;return text;
}
static DomNode *convert_node(dom_node *native,unsigned depth,unsigned *count,int *failed) {
    dom_node_type type;
    if(dom_node_get_node_type(native,&type)!=DOM_NO_ERR){*failed=1;return NULL;}
    if(type!=DOM_DOCUMENT_NODE && type!=DOM_DOCUMENT_FRAGMENT_NODE && type!=DOM_ELEMENT_NODE &&
       type!=DOM_TEXT_NODE && type!=DOM_CDATA_SECTION_NODE)return NULL;
    if(depth>=DOM_MAX_DEPTH || ++*count>DOM_MAX_NODES){*failed=1;return NULL;}
    DomNode *node=NULL;dom_string *value=NULL;
    if(type==DOM_DOCUMENT_NODE || type==DOM_DOCUMENT_FRAGMENT_NODE)node=dom_create_node(NODE_DOCUMENT);
    else if(type==DOM_ELEMENT_NODE) {
        if(dom_node_get_node_name(native,&value)!=DOM_NO_ERR || !value){*failed=1;return NULL;}
        char *tag=string_copy(value);dom_string_unref(value);value=NULL;
        if(tag){node=dom_create_element(tag);free(tag);}
        if(node) {
            dom_namednodemap *attributes=NULL;uint32_t length=0;
            if(dom_node_get_attributes(native,&attributes)!=DOM_NO_ERR){*failed=1;}
            if(attributes && dom_namednodemap_get_length(attributes,&length)!=DOM_NO_ERR)*failed=1;
            for(uint32_t i=0;attributes && i<length && !*failed;i++) {
                dom_node *attribute=NULL;dom_string *name=NULL,*data=NULL;
                if(dom_namednodemap_item(attributes,i,&attribute)!=DOM_NO_ERR || !attribute){*failed=1;break;}
                if(dom_node_get_node_name(attribute,&name)!=DOM_NO_ERR ||
                   dom_node_get_node_value(attribute,&data)!=DOM_NO_ERR || !name || !data)*failed=1;
                char *n=name?string_copy(name):NULL,*v=data?string_copy(data):NULL;
                if(n && v)dom_set_attribute(node,n,v);else *failed=1;
                free(n);free(v);if(name)dom_string_unref(name);if(data)dom_string_unref(data);dom_node_unref(attribute);
            }
            if(attributes)dom_namednodemap_unref(attributes);
        }
    } else {
        if(dom_node_get_node_value(native,&value)!=DOM_NO_ERR){*failed=1;return NULL;}
        node=dom_create_text(value?dom_string_data(value):"",value?(int)dom_string_byte_length(value):0);
        if(value)dom_string_unref(value);
    }
    if(!node){*failed=1;return NULL;}
    dom_node *child=NULL;
    if(!*failed && dom_node_get_first_child(native,&child)!=DOM_NO_ERR)*failed=1;
    while(child && !*failed) {
        dom_node *next=NULL;
        if(dom_node_get_next_sibling(child,&next)!=DOM_NO_ERR)*failed=1;
        DomNode *converted=!*failed?convert_node(child,depth+1,count,failed):NULL;
        dom_node_unref(child);child=next;
        if(converted)dom_append_child(node,converted);
    }
    if(child)dom_node_unref(child);
    if(*failed){dom_free_tree(node);return NULL;}return node;
}
static dom_hubbub_error defer_script(void *context,dom_node *node) {(void)context;(void)node;return DOM_HUBBUB_OK;}
DomNode *html_parse(const char *html,int length) {
    if(!html || length<0)return NULL;
    dom_hubbub_parser_params parameters={0};parameters.enc="UTF-8";parameters.fix_enc=true;
    parameters.enable_script=true;parameters.script=defer_script; /* frontend executes scripts in document order */
    dom_hubbub_parser *parser=NULL;dom_document *native=NULL;
    dom_hubbub_error result=dom_hubbub_parser_create(&parameters,&parser,&native);
    if(result==DOM_HUBBUB_OK)result=dom_hubbub_parser_parse_chunk(parser,(const uint8_t *)html,(size_t)length);
    if(result==DOM_HUBBUB_OK)result=dom_hubbub_parser_completed(parser);
    if(parser)dom_hubbub_parser_destroy(parser);
    unsigned count=0;int failed=result!=DOM_HUBBUB_OK;
    DomNode *root=!failed && native?convert_node((dom_node *)native,0,&count,&failed):NULL;
    if(native)dom_node_unref(native);
    if(!root)printf("[browser:html] upstream parser failed (%u)\n",(unsigned)result);
    else printf("[browser:html] libhubbub/libdom HTML5 tree: %u nodes\n",count);
    return root;
}
