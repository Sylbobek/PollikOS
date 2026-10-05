#define main held_drag_existing_main
#include "held_drag.c"
#undef main
static int cov(int w,int h,int r,int x,int y) {
    if(x<0||y<0||x>=w||y>=h)return 0;
    int cx=x<r?x:(x>=w-r?w-1-x:r);
    int cy=y<r?y:(y>=h-r?h-1-y:r);
    int n=0;
    if(cx>=r||cy>=r)return 64;
    for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++){
        int dx=cx*8+sx*2+1-r*8,dy=cy*8+sy*2+1-r*8;
        n+=dx*dx+dy*dy<=r*r*64;
    }
    return n*4;
}
int main(void) {
    int checks=0,curved=0;
    setup();
    for(int t=1;t<=3;t++) {
        for(int i=0;i<N;i++)scene[i]=0x102030;
        set_draw_target(scene,W,H,W);graphics_set_clip((GraphicsClip){0,0,W,H});
        roundrect_border(100,100,240,84,29,t,0xffffff);
        for(int y=0;y<84;y++)for(int x=0;x<240;x++) {
            int a=cov(240,84,29,x,y)-cov(240-2*t,84-2*t,29-t,x-t,y-t);
            if(a<0)a=0;
            u32 expected=a>=64?0xffffff:blend(0x102030,0xffffff,a*4);
            u32 actual=scene[(y+100)*W+x+100];
            if(actual!=expected) {
                printf("FAIL rounded outline t=%d x=%d y=%d expected=%06x actual=%06x\n",t,x,y,expected,actual);return 1;
            }
            curved+=a>0&&x>t&&x<29&&y>t&&y<29;
            checks++;
        }
    }
    REQUIRE(curved>0);
    printf("PASS continuous rounded outline: %d exact 4x4 coverage pixels, curved pixels=%d, thickness 1..3\n",checks,curved);
    return 0;
}
