#ifndef POLLIK_APP_SEARCH_H
#define POLLIK_APP_SEARCH_H
typedef struct { char text[32];int cursor,anchor; } AppSearchInput;
enum { APP_SEARCH_LEFT=256,APP_SEARCH_RIGHT,APP_SEARCH_HOME,APP_SEARCH_END };
static inline void app_search_reset(AppSearchInput *e){e->text[0]=0;e->cursor=0;e->anchor=-1;}
static inline int app_search_length(const AppSearchInput *e){int n=0;while(e->text[n])n++;return n;}
static inline void app_search_select(AppSearchInput *e,int position,int extend){
    int n=app_search_length(e);if(position<0)position=0;if(position>n)position=n;
    if(extend){if(e->anchor<0)e->anchor=e->cursor;}else e->anchor=-1;e->cursor=position;
}
static inline int app_search_delete_selection(AppSearchInput *e){
    if(e->anchor<0||e->anchor==e->cursor){e->anchor=-1;return 0;}
    int a=e->anchor,b=e->cursor;if(a>b){int t=a;a=b;b=t;}
    int i=a;do{e->text[i++]=e->text[b++];}while(e->text[i-1]);e->cursor=a;e->anchor=-1;return 1;
}
static inline int app_search_edit(AppSearchInput *e,unsigned key,int shift,int control){
    int n=app_search_length(e);
    if(control&&(key=='a'||key=='A')){e->anchor=0;e->cursor=n;return 1;}
    if(key>=APP_SEARCH_LEFT&&key<=APP_SEARCH_END){
        int next=key==APP_SEARCH_LEFT?e->cursor-1:key==APP_SEARCH_RIGHT?e->cursor+1:key==APP_SEARCH_HOME?0:n;
        if(!shift&&e->anchor>=0&&(key==APP_SEARCH_LEFT||key==APP_SEARCH_RIGHT))
            next=key==APP_SEARCH_LEFT?(e->anchor<e->cursor?e->anchor:e->cursor):(e->anchor>e->cursor?e->anchor:e->cursor);
        app_search_select(e,next,shift);return 1;
    }
    if(key==8||key==127){
        if(app_search_delete_selection(e))return 1;
        int at=key==8?e->cursor-1:e->cursor;
        if(at>=0&&at<n){for(int i=at;i<n;i++)e->text[i]=e->text[i+1];if(key==8)e->cursor--;}
        return 1;
    }
    if(!control&&key>=32&&key<127){
        app_search_delete_selection(e);n=app_search_length(e);
        if(n<(int)sizeof(e->text)-1){for(int i=n+1;i>e->cursor;i--)e->text[i]=e->text[i-1];e->text[e->cursor++]=(char)key;}
        return 1;
    }
    return 0;
}
static inline unsigned app_search_parse_pins(const char *text){
    const char *prefix="pins=";for(int i=0;i<5;i++)if(text[i]!=prefix[i])return 0;
    unsigned pins=0;const char *p=text+5;
    if(*p<'0'||*p>'9')return 0;
    while(*p>='0'&&*p<='9'){unsigned digit=(unsigned)(*p++-'0');if(pins>(0xffffffffu-digit)/10)return 0;pins=pins*10+digit;}
    return !*p||*p=='\n'||*p=='\r'?pins:0;
}
static inline char app_search_lower(char ch) {
    return ch>='A'&&ch<='Z'?(char)(ch+32):ch;
}
static inline int app_search_matches(const char *name,const char *query) {
    if(!name||!query)return 0;
    if(!*query)return 1;
    for(;*name;name++) {
        const char *a=name,*b=query;
        while(*a&&*b&&app_search_lower(*a)==app_search_lower(*b)){a++;b++;}
        if(!*b)return 1;
    }
    return 0;
}
#endif
