/* Real PollikMark + offscreen rasterizer, deterministic host-only services. */
#include "../kernel/gui/pollikmark.c"
#include "gfx_corner_stub.h"
extern int printf(const char *,...);
extern void *malloc(__SIZE_TYPE__);
extern void free(void *);
static int failed, allocations, frees, fail_after=-1, painting;
static u64 clock_us;
static u32 ui[680*410];
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);failed++;}}while(0)
_Static_assert(sizeof(MarkResult)==15*4,"existing probe ABI");
int framebuffer_width(void){return 1024;}
int framebuffer_height(void){return 768;}
int framebuffer_bpp(void){return 32;}
u64 app_host_time_us(void){clock_us+=10;return clock_us;}
void app_host_metrics(AppPerfView *v){*v=(AppPerfView){0};}
u32 app_host_free_bytes(void){return 64*1048576;}
void app_host_invalidate(int id){CHECK(id==APP_POLLIKMARK);}
void *app_host_alloc(u32 bytes){
    CHECK(!painting);
    if(fail_after==0)return 0;
    if(fail_after>0)fail_after--;
    u32 *p=malloc(bytes+8);if(!p)return 0;
    p[0]=p[bytes/4+1]=0xdeadbeef;allocations++;return p+1;
}
void app_host_free(void *p,u32 bytes){
    CHECK(!painting);if(!p)return;
    u32 *b=(u32 *)p-1;CHECK(b[0]==0xdeadbeef && b[bytes/4+1]==0xdeadbeef);
    frees++;free(b);
}
int sys_get_glyph_advance(u8 c,int scale){(void)c;return 6*scale;}
void number(char *b,u32 n){char a[10];int i=0,j=0;do{a[i++]=(char)('0'+n%10);n/=10;}while(n);while(i)b[j++]=a[--i];b[j]=0;}
void ui_bridge_rect(int x,int y,int w,int h,u32 c){
    CHECK(x>=0 && y>=34 && w>=0 && h>=0 && x+w<=win_w && y+h<=win_h);
    for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)if(i>=0 && i<680 && j>=0 && j<410)ui[j*680+i]=c;
}
void ui_bridge_text(int x,int y,const char *s,u32 c,int scale){
    (void)scale;for(;*s;s++,x+=6)if(*s!=' ')ui_bridge_rect(x,y,5,16,c);
}
void app_host_blit(int x,int y,const u32 *src,int w,int h,int stride,u32 capacity){
    CHECK(src && stride>=w && capacity>=(u32)(stride*h));ui_bridge_rect(x,y,w,h,1);
}
static u32 changed(GfxTarget *t,u32 bg){u32 count=0;for(int y=0;y<t->height;y++)for(int x=0;x<t->width;x++)count+=t->color[y*t->stride+x]!=bg;return count;}
static void shapes(void){
    u32 p[18*16+2];float z[18*16];
    for(unsigned i=0;i<sizeof(p)/sizeof(*p);i++)p[i]=0x123456;
    for(int i=0;i<18*16;i++)z[i]=0.25f;
    GfxTarget t={p+1,z,16,16,18,18*16};
    for(u32 kind=0;kind<5;kind++){
        gfx_rect(&t,0,0,16,16,0);
        u32 writes=gfx_shape(&t,0,0,11,7,0xffffff,kind);
        CHECK(writes>0 && writes==changed(&t,0));
        if(kind==0)CHECK(writes==77 && p[1]==0xffffff);
        if(kind==1)CHECK(writes<77 && p[1]==0 && p[2]!=0 && p[2]!=0xffffff);
        if(kind==2)CHECK(writes<77 && p[1]==0xffffff && p[6*18+11]==0xffffff);
        if(kind==3){
            CHECK(p[1]==0xffffff && p[5]==0 && p[6]==0 && p[7]==0xffffff);
            CHECK(p[6*18+1]==0xffffff && p[6*18+2]==0);
        }
        if(kind==4)CHECK(writes==77 && p[1]==0x7f7f7f);
        for(int y=-8;y<20;y++)for(int x=-12;x<20;x++)gfx_shape(&t,x,y,11,7,0xffffff,kind);
        for(int y=0;y<16;y++)CHECK(p[1+y*18+16]==0x123456 && p[1+y*18+17]==0x123456);
        CHECK(p[0]==0x123456 && p[18*16+1]==0x123456);
        for(int i=0;i<18*16;i++)CHECK(z[i]==0.25f);
    }
    CHECK(!gfx_shape(&t,0x7fffffff,0,11,7,1,1));
    CHECK(!gfx_shape(&t,0,0,65,7,1,1));
    CHECK(!gfx_shape(&t,0,0,11,7,1,5));
    GfxTarget bad=t;bad.capacity=1;CHECK(!gfx_shape(&bad,0,0,11,7,1,0));
}
static void rotation_and_counters(void){
    start(2,0);frame_clock=60;begin_frame();make_triangle();
    Triangle first=frame_triangle;
    CHECK(first.v[0].p.w>1 && first.v[0].p.z>1);
    begin_frame();make_triangle();CHECK(frame_triangle.v[0].p.x!=first.v[0].p.x);
    CHECK(frame_triangle.v[0].p.w!=first.v[0].p.w);
    /* A completed frame counts exactly successful raster writes, excluding clear. */
    frame_clock=60;begin_frame();
    for(int cap=0;n==0 && cap<1000;cap++)pollikmark_poll();
    CHECK(n==1 && units==1 && raster_pixels>0);
    GfxTarget visible=target;visible.color=front;
    CHECK(raster_pixels==changed(&visible,0x182238));
    CHECK(pollikmark_raster[2][0].pixels==raster_pixels);
    CHECK(pollikmark_raster[2][0].rate==gfx_ratio((u64)raster_pixels*1000000,(u32)measured_us));
    CHECK(live_result.rate==gfx_ratio(units*1000000,(u32)measured_us));
    u32 saved=raster_pixels;graphics_step();stop();
    CHECK(pollikmark_raster[2][0].pixels==saved && pollikmark_results[2][0].status==2);
    for(u32 test=3;test<=4;test++){
        start(test,0);frame_clock=60;begin_frame();
        while(!graphics_step()){}
        CHECK(frame_pixels>0 && frame_units==(test==3?12:100));
    }
    start(1,0);while(!graphics_step()){}CHECK(frame_units==100 && frame_pixels==0);
}
static void resize_and_layout(void){
    start(1,1);pollikmark_level=2;CHECK(begin_level());
    pollikmark_results[1][0].status=1;pollikmark_results[1][0].rate=123;
    pollikmark_completed=11;n=4;raster_pixels=1234;
    pollikmark_resize(480,280);
    CHECK(pollikmark_running && all && pollikmark_test==1 && pollikmark_level==2);
    CHECK(n==0 && raster_pixels==0 && !front_valid && target.width==192 && target.height==66);
    CHECK(pollikmark_results[1][0].rate==123 && pollikmark_completed==11);
    CHECK(allocations-frees==3);
    int allocated=allocations;
    for(int i=0;i<3;i++)pollikmark_poll();
    CHECK(allocated==allocations);
    /* Reallocation failure at each of the three buffers retains old target. */
    for(int fail=0;fail<3;fail++){
        u32 *old=target.color;u32 oldn=n;
        fail_after=fail;pollikmark_resize(680+fail,410);fail_after=-1;
        CHECK(target.color==old && pollikmark_running && all && n==oldn);
        CHECK(allocations-frees==3);
    }
    pollikmark_resize(680,410);CHECK(pollikmark_running && all && n==0);
    int w=target.width,h=target.height;
    CHECK(w<=192 && h<=128);
    /* 480x280: all eight rows including Memory at y=234 survive the footer. */
    pollikmark_resize(480,280);
    for(int test=0;test<8;test++){
        pollikmark_test=test;pollikmark_level=0;
        for(int show=0;show<2;show++){
            info=show;painting=1;pollikmark_render(480,280,1);painting=0;
            CHECK(ui[234*680+34]==0xd9e4f0 && ui[249*680+34]==0xd9e4f0);
        }
    }
    info=0;pollikmark_click(40,240);CHECK(pollikmark_test==7 && pollikmark_running);
    CHECK(memory_bytes==1048576);
    pollikmark_resize(680,410);CHECK(pollikmark_test==7 && pollikmark_level==0 && pollikmark_running);
    painting=1;pollikmark_render(680,410,1);painting=0;
    pollikmark_close();CHECK(allocations==frees && !target.color && !front && !mem_a && !mem_b);
}
int main(void){
    shapes();pollikmark_init();pollikmark_open();rotation_and_counters();resize_and_layout();
    printf("pollikmark native: %s (rotation, raster counters/ABI, mixed shapes, clipping, alpha, resize/failure/leaks, 480x280 eight rows)\n",failed?"FAIL":"PASS");
    return !!failed;
}
