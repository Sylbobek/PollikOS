#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "../../third_party/quickjs/quickjs.h"
#pragma clang diagnostic pop
#include <stdio.h>
#include <string.h>
#include <malloc.h>
static int eval(JSContext *ctx,const char *name,const char *code) {
    JSValue value=JS_Eval(ctx,code,strlen(code),name,JS_EVAL_TYPE_GLOBAL);
    if(JS_IsException(value)){
        JSValue exception=JS_GetException(ctx);const char *text=JS_ToCString(ctx,exception);
        printf("FAIL QuickJS %s: %s\n",name,text?text:"exception");
        JS_FreeCString(ctx,text);JS_FreeValue(ctx,exception);return 0;
    }
    JS_FreeValue(ctx,value);printf("PASS QuickJS %s\n",name);return 1;
}
int main(void) {
    void *p=malloc(33);if(!p||malloc_usable_size(p)<33)return 2;free(p);
    JSRuntime *rt=JS_NewRuntime();if(!rt)return 3;
    JS_SetMemoryLimit(rt,32u*1024u*1024u);JS_SetMaxStackSize(rt,128u*1024u);
    JSContext *ctx=JS_NewContext(rt);if(!ctx){JS_FreeRuntime(rt);return 4;}
    int ok=eval(ctx,"closure-arrow-array","const f=x=>y=>x+y; if([1,2,3].map(f(2)).join(',')!=='3,4,5')throw Error('closure');") &&
      eval(ctx,"class-prototype","class Base{constructor(x){this.x=x;}get value(){return this.x;}} class Sub extends Base{} if(new Sub(7).value!==7)throw Error('class');") &&
      eval(ctx,"bigint-map-set","if(String(2n**100n)!=='1267650600228229401496703205376')throw Error('bigint');if(new Set([1,1,2]).size!==2||new Map([['x',7]]).get('x')!==7)throw Error('collections');") &&
      eval(ctx,"regexp-unicode","if(!/^\\p{Letter}+$/u.test('żółć')||'a'.repeat(3)!=='aaa')throw Error('unicode');") &&
      eval(ctx,"json-typedarray","const a=new Uint8Array([1,2,255]);if(JSON.parse(JSON.stringify({a:[...a]})).a[2]!==255)throw Error('typed array');") &&
      eval(ctx,"date-math","if(new Date('2024-02-29T12:00:00Z').toISOString()!=='2024-02-29T12:00:00.000Z')throw Error('date');if(Math.abs(Math.expm1(1e-8)-1.000000005e-8)>1e-20||Math.hypot(3,4)!==5||Math.cbrt(27)!==3)throw Error('math');") &&
      eval(ctx,"promise-job","globalThis.jobResult=0;Promise.resolve(21).then(x=>{jobResult=x*2;});");
    JSContext *job_ctx;int jobs=0,r;
    while((r=JS_ExecutePendingJob(rt,&job_ctx))>0 && jobs++<100){}
    if(r<0||jobs>=100)ok=0;
    if(ok)ok=eval(ctx,"promise-result","if(jobResult!==42)throw Error('jobs');");
    JS_FreeContext(ctx);JS_RunGC(rt);JS_FreeRuntime(rt);
    if(ok)puts("PASS native QuickJS language and runtime teardown");
    return ok?0:1;
}
