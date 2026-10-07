#ifndef POLLIK_BROWSER_SCRIPT_TYPE_H
#define POLLIK_BROWSER_SCRIPT_TYPE_H
/* HTML script type classification shared by both browser frontends.
 * Data blocks are retained in the DOM, never evaluated as JavaScript.
 * Modules are distinct from classic scripts and remain unsupported. */
static int browser_script_type_equal(const char *a,const char *b) {
    while (*a && *b) {
        unsigned char c=(unsigned char)*a++;
        if(c>='A'&&c<='Z')c+='a'-'A';
        if(c!=(unsigned char)*b++)return 0;
    }
    return !*a && !*b;
}
static int browser_script_kind(DomNode *node) {
    const char *type=dom_get_attribute(node,"type");
    if(!type||!*type)return 1;
    if(browser_script_type_equal(type,"module"))return 2;
    static const char *const classic[]={"application/ecmascript","application/javascript",
        "application/x-ecmascript","application/x-javascript","text/ecmascript",
        "text/javascript","text/javascript1.0","text/javascript1.1","text/javascript1.2",
        "text/javascript1.3","text/javascript1.4","text/javascript1.5","text/jscript",
        "text/livescript","text/x-ecmascript","text/x-javascript"};
    for(unsigned i=0;i<sizeof(classic)/sizeof(classic[0]);++i)
        if(browser_script_type_equal(type,classic[i]))return 1;
    return 0;
}
#endif
