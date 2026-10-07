/* Real Elk GC, the same reentrant C/JS pattern as DOM wrapper construction. */
#define POLLIK_BROWSER_STANDALONE 1
#include <stdio.h>
#include <string.h>
#include "../third_party/elk/elk.c"
static unsigned char arena[32768];
static int failures;
static void check(int ok,const char *what) {
    if(!ok){printf("FAIL %s\n",what);++failures;}
}
static jsval_t construct(struct js *j,jsval_t *args,int count) {
    (void)args;(void)count;
    char garbage[4096];memset(garbage,'g',sizeof(garbage));
    js_mkstr(j,garbage,sizeof(garbage)); /* dead data before the live wrapper */
    jsval_t object=js_mkobj(j);
#ifdef JS_ROOTS_API
    struct js_root root={0};js_root_acquire(j,&root,&object);
#endif
    js_set(j,js_glob(j),"wrapper",object);
    js_setgct(j,0);pollik_js_budget=100000;
    jsval_t evaluated=js_eval(j,"1;",2);
    check(!is_err(evaluated),"nested eval succeeds");
    jsval_t live=pollik_js_get(j,js_glob(j),"wrapper");
    printf("HANDLE local=%zu live=%zu\n",vdata(object),vdata(live));
    check(object==live,"C object handle updated after compaction");
    /* The old wrapper would write through the stale handle at this point. */
    if(object==live){js_set(j,object,"answer",js_mknum(42));
        check(js_getnum(pollik_js_get(j,live,"answer"))==42,"write reaches live object");}
#ifdef JS_ROOTS_API
    js_root_release(j,&root);
    check(!js_roots,"temporary root released exactly");
#endif
    return js_mknum(42);
}
static jsval_t preserve_arguments(struct js *j,jsval_t *args,int count) {
    check(count==1,"one native argument");
    js_setgct(j,0);pollik_js_budget=100000;
    js_eval(j,"1;",2);
    jsoff_t off=(jsoff_t)vdata(args[0]);
    int valid=vtype(args[0])==T_STR && off+4<=j->brk &&
        (loadoff(j,off)&3)==T_STR && vstrlen(j,args[0])==14;
    if(valid){size_t n=0;char *s=js_getstr(j,args[0],&n);
        valid=n==14 && !memcmp(s,"argument alive",14);}
    check(valid,"native argument rooted and relocated during nested GC");
    return js_mknum(valid?1:0);
}
int main(void) {
    struct js *j=js_create(arena,sizeof(arena));
    js_set(j,js_glob(j),"construct",js_mkfun(construct));
    pollik_js_budget=100000;js_eval(j,"construct();",12);
    j=js_create(arena,sizeof(arena));
    js_set(j,js_glob(j),"preserve",js_mkfun(preserve_arguments));
    pollik_js_budget=100000;js_eval(j,"preserve('argument alive');",26);
    check(!js_native_frames,"native argument frame released exactly");
    j=js_create(arena,512);js_set(j,js_glob(j),"preserve",js_mkfun(preserve_arguments));
    jsoff_t initial_size=j->size;
    char large_call[1024];memcpy(large_call,"preserve(",9);unsigned used=9;
    for(int i=0;i<100;i++){if(i)large_call[used++]=',';large_call[used++]='1';}
    large_call[used++]=')';large_call[used++]=';';large_call[used]=0;
    pollik_js_budget=100000;jsval_t overflow=js_eval(j,large_call,strlen(large_call));
    check(is_err(overflow),"oversized native argument vector rejected");
    check(j->size==initial_size && !js_native_frames,"argument OOM restores exact stack and roots");
    if(failures)return 1;
    puts("PASS Elk roots: reentrant native wrapper, relocated argument, exact root release");
    return 0;
}
