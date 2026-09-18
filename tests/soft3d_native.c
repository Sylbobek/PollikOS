#include "../kernel/soft3d.h"
#include "gfx_corner_stub.h"
extern int printf(const char *,...);
static int failed;
#define CHECK(x) do { if(!(x)) {printf("FAIL line %d: %s\n",__LINE__,#x);failed++;} } while(0)
int framebuffer_width(void){return 1024;}
int framebuffer_height(void){return 768;}
int framebuffer_bpp(void){return 32;}
static u32 pixels[34*32+2];
static float depth[34*32+2];
static int near(float a,float b){return a-b<0.001f && b-a<0.001f;}
int main(void) {
    GfxTarget t={pixels+1,depth+1,32,32,34,34*32};
    pixels[0]=pixels[34*32+1]=0xabcdef;depth[0]=depth[34*32+1]=123;
    CHECK(gfx_target_valid(&t));CHECK(!gfx_target_valid(0));
    GfxTarget bad=t;bad.stride=31;CHECK(!gfx_target_valid(&bad));
    bad=t;bad.capacity=100;CHECK(!gfx_target_valid(&bad));
    bad=t;bad.color=0;CHECK(!gfx_target_valid(&bad));
    bad=t;bad.width=2049;CHECK(!gfx_target_valid(&bad));
    CHECK(gfx_clear(&t,0,~0u,0)==1088);CHECK(gfx_clear(&t,1088,1,1)==0);
    CHECK(gfx_rect(&t,-5,-5,10,10,7)==25);
    CHECK(gfx_ratio(1,2)==0);CHECK(gfx_ratio(0xffffffffffffffffull,1)==~0u);
    CHECK(gfx_ratio(1234567890123ull,1000000)==1234567);
    Mat4 id=soft3d_identity(),m=id;m.m[3]=2;m.m[7]=3;
    Vec4 v=soft3d_transform(soft3d_multiply(id,m),(Vec4){1,2,3,1});
    CHECK(near(v.x,3)&&near(v.y,5)&&near(v.z,3)&&near(v.w,1));
    Mat4 p=soft3d_perspective(1,2,1,10);
    v=soft3d_transform(p,(Vec4){0,0,-1,1});CHECK(near(v.z/v.w,-1));
    v=soft3d_transform(p,(Vec4){0,0,-10,1});CHECK(near(v.z/v.w,1));
    CHECK(near(p.m[0],0.5f));CHECK(soft3d_perspective(1,0,1,10).m[0]==0);
    Triangle tri={{{{-0.8f,-0.8f,0,1},{1,0,0},{0,0}},{{0.8f,-0.8f,0,1},{0,1,0},{0,0}},{{0,0.8f,0,1},{0,0,1},{0,0}}}};
    gfx_clear(&t,0,1088,0);
    CHECK(soft3d_triangle(&t,&tri,1,2,0,32)>200);
    u32 center=t.color[16*34+16];CHECK(center!=0 && center!=0xff0000);
    CHECK(soft3d_triangle(&t,&tri,1,2,0,32)==0); /* equal-depth rejected */
    for(int i=0;i<3;i++)tri.v[i].p.z=-0.5f;
    CHECK(soft3d_triangle(&t,&tri,1,1,0,32)>200);
    Vertex swap=tri.v[1];tri.v[1]=tri.v[2];tri.v[2]=swap;
    gfx_clear(&t,0,1088,0);CHECK(soft3d_triangle(&t,&tri,1,2,0,32)==0);
    CHECK(soft3d_triangle(&t,&tri,0,0,0,32)>0);
    tri.v[1]=tri.v[0];CHECK(soft3d_triangle(&t,&tri,0,2,0,32)==0);
    /* Six-plane clipping, crossing near and side planes, finite bounded inputs. */
    tri=(Triangle){{{{-2,-0.5f,-2,1},{1,0,0},{0,0}},{{2,-0.5f,0,1},{0,1,0},{0,0}},{{0,2,0,1},{0,0,1},{0,0}}}};
    gfx_clear(&t,0,1088,0);CHECK(soft3d_triangle(&t,&tri,0,2,0,32)>0);
    for(int i=0;i<3;i++)tri.v[i].p.w=-1;
    CHECK(soft3d_triangle(&t,&tri,0,2,0,32)==0);
    CHECK(soft3d_triangle(&t,0,0,2,0,32)==0);
    for(int i=0;i<300;i++) {
        for(int j=0;j<3;j++)tri.v[j].p=(Vec4){((i*17+j*31)%80-40)/10.f,((i*29+j*19)%80-40)/10.f,((i*13+j*7)%40-20)/10.f,1};
        gfx_clear(&t,0,1088,0);soft3d_triangle(&t,&tri,0,2,-1,33);
        for(int y=0;y<32;y++)CHECK(t.color[y*34+32]==0 && t.color[y*34+33]==0);
    }
    CHECK(pixels[0]==0xabcdef && pixels[1089]==0xabcdef);
    CHECK(depth[0]==123 && depth[1089]==123);
    printf("soft3d native: %s (matrices, depth, culling, clipping, bounds)\n",failed?"FAIL":"PASS");return !!failed;
}
