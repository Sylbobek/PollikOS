#include "soft3d.h"
Mat4 soft3d_identity(void) {
    Mat4 m = {{0}};
    for (int i=0;i<4;i++) m.m[i*5]=1;
    return m;
}
Mat4 soft3d_multiply(Mat4 a, Mat4 b) {
    Mat4 r = {{0}};
    for (int y=0;y<4;y++) for (int x=0;x<4;x++)
        for (int k=0;k<4;k++) r.m[y*4+x] += a.m[y*4+k]*b.m[k*4+x];
    return r;
}
Vec4 soft3d_transform(Mat4 m, Vec4 v) {
    float a[4]={v.x,v.y,v.z,v.w}, r[4]={0};
    for (int y=0;y<4;y++) for (int x=0;x<4;x++) r[y]+=m.m[y*4+x]*a[x];
    return (Vec4){r[0],r[1],r[2],r[3]};
}
Mat4 soft3d_perspective(float focal,float aspect,float n,float f) {
    Mat4 m={{0}};
    if (!(focal>0 && focal<=100 && aspect>0.01f && aspect<100 && n>0 && f>n && f<=16384)) return m;
    m.m[0]=focal/aspect; m.m[5]=focal;
    m.m[10]=-(f+n)/(f-n); m.m[11]=-2*f*n/(f-n); m.m[14]=-1;
    return m;
}
static float plane(Vec4 p,int i) {
    float v=i<2?p.x:i<4?p.y:p.z;
    return p.w+(i&1?-v:v);
}
static Vertex mix(Vertex a,Vertex b,float f) {
    Vertex r;
    r.p=(Vec4){a.p.x+(b.p.x-a.p.x)*f,a.p.y+(b.p.y-a.p.y)*f,
        a.p.z+(b.p.z-a.p.z)*f,a.p.w+(b.p.w-a.p.w)*f};
    r.color=(Vec3){a.color.x+(b.color.x-a.color.x)*f,a.color.y+(b.color.y-a.color.y)*f,a.color.z+(b.color.z-a.color.z)*f};
    r.uv=(Vec2){a.uv.x+(b.uv.x-a.uv.x)*f,a.uv.y+(b.uv.y-a.uv.y)*f};
    return r;
}
static float edge(Vec4 a,Vec4 b,float x,float y) {return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
static u32 channel(float v) {return v<=0?0:v>=1?255:(u32)(v*255);}
static int texture_valid(const GfxTexture *x) {
    return x && x->pixels && x->width>0 && x->height>0 &&
        x->width<=2048 && x->height<=2048 &&
        !(x->width&(x->width-1)) && !(x->height&(x->height-1));
}
static u32 texel(const GfxTexture *x,int u,int v) {
    return x->pixels[(v&(x->height-1))*x->width+(u&(x->width-1))];
}
static u32 sample_nearest(const GfxTexture *x,float u,float v) {
    return texel(x,(int)(u*(float)x->width),(int)(v*(float)x->height));
}
/* 2x2 bilinear blend with integer weights (256 = 1.0); no float in the loop. */
static u32 sample_bilinear(const GfxTexture *x,float u,float v) {
    float fx=u*(float)x->width-0.5f, fy=v*(float)x->height-0.5f;
    int x0=fx>=0?(int)fx:(int)(fx-0.999999f), y0=fy>=0?(int)fy:(int)(fy-0.999999f);
    int ax=(int)((fx-(float)x0)*256.0f), ay=(int)((fy-(float)y0)*256.0f);
    if(ax<0)ax=0; else if(ax>256)ax=256;
    if(ay<0)ay=0; else if(ay>256)ay=256;
    u32 c00=texel(x,x0,y0),c10=texel(x,x0+1,y0),c01=texel(x,x0,y0+1),c11=texel(x,x0+1,y0+1);
    int w00=(256-ax)*(256-ay),w10=ax*(256-ay),w01=(256-ax)*ay,w11=ax*ay;
    int r=((((int)(c00>>16)&255)*w00+((int)(c10>>16)&255)*w10+((int)(c01>>16)&255)*w01+((int)(c11>>16)&255)*w11)+32768)>>16;
    int g=((((int)(c00>>8)&255)*w00+((int)(c10>>8)&255)*w10+((int)(c01>>8)&255)*w01+((int)(c11>>8)&255)*w11)+32768)>>16;
    int b=((((int)c00&255)*w00+((int)c10&255)*w10+((int)c01&255)*w01+((int)c11&255)*w11)+32768)>>16;
    return ((u32)r<<16)|((u32)g<<8)|(u32)b;
}
static u32 raster(GfxTarget *t,Vertex a,Vertex b,Vertex c,int cull,int mode,int first,int last,
    const GfxTexture *tex,int perspective,int bilinear) {
    Vertex v[3]={a,b,c};
    float q[3]={0,0,0};
    for(int i=0;i<3;i++) {
        if (!(v[i].p.w>0.00001f)) return 0;
        q[i]=1/v[i].p.w;
        v[i].p.x=(v[i].p.x*q[i]+1)*0.5f*t->width;
        v[i].p.y=(1-v[i].p.y*q[i])*0.5f*t->height;
        v[i].p.z=(v[i].p.z*q[i]+1)*0.5f;
    }
    float area=edge(v[0].p,v[1].p,v[2].p.x,v[2].p.y);
    if ((cull && area>=0) || (area>-0.0001f && area<0.0001f)) return 0;
    int x0=t->width,x1=0,y0=last,y1=first;
    for(int i=0;i<3;i++) {
        int x=(int)v[i].p.x,y=(int)v[i].p.y;
        if(x<x0)x0=x; if(x>x1)x1=x;
        if(y<y0)y0=y; if(y>y1)y1=y;
    }
    if(x0<0)x0=0; if(x1>=t->width)x1=t->width-1;
    if(y0<first)y0=first; if(y1>=last)y1=last-1;
    if(x0>x1||y0>y1) return 0;
    /* Incremental edge functions: per pixel just adds, no multiplies/divisions
     * for the barycentrics. u/area -> v0, z/area -> v1, w=1-u-z -> v2. */
    float inv_area=1.0f/area;
    float dx12=v[2].p.x-v[1].p.x, dy12=v[2].p.y-v[1].p.y;
    float dx20=v[0].p.x-v[2].p.x, dy20=v[0].p.y-v[2].p.y;
    float dx01=v[1].p.x-v[0].p.x, dy01=v[1].p.y-v[0].p.y;
    float Au=-dy12, Av=dx12, Ac=dy12*v[1].p.x-dx12*v[1].p.y;
    float Bu=-dy20, Bv=dx20, Bc=dy20*v[2].p.x-dx20*v[2].p.y;
    float Cu=-dy01, Cv=dx01, Cc=dy01*v[0].p.x-dx01*v[0].p.y;
    float z0=v[0].p.z, z1=v[1].p.z, z2=v[2].p.z;
    u32 flat_col=0;
    if(mode!=2 && mode!=3)
        flat_col=channel(a.color.x)<<16|channel(a.color.y)<<8|channel(a.color.z);
    int stride=t->stride;
    int has_depth=t->depth!=0;
    /* Per-pixel increments: depth, interpolated color and affine uv advance by
     * one constant step, so the hot loop needs no extra multiplies. */
    float gu=Au*inv_area, gz=Bu*inv_area, gw=Cu*inv_area;
    float gd=gu*z0+gz*z1+gw*z2;
    float cr0=v[0].color.x,cr1=v[1].color.x,cr2=v[2].color.x;
    float cg0=v[0].color.y,cg1=v[1].color.y,cg2=v[2].color.y;
    float cb0=v[0].color.z,cb1=v[1].color.z,cb2=v[2].color.z;
    float gcr=gu*cr0+gz*cr1+gw*cr2, gcg=gu*cg0+gz*cg1+gw*cg2, gcb=gu*cb0+gz*cb1+gw*cb2;
    float u0=v[0].uv.x,u1=v[1].uv.x,u2=v[2].uv.x, vv0=v[0].uv.y,vv1=v[1].uv.y,vv2=v[2].uv.y;
    float guv=gu*u0+gz*u1+gw*u2, gvv=gu*vv0+gz*vv1+gw*vv2;
    u32 count=0;
    for(int y=y0;y<=y1;y++) {
        float xf=(float)x0+0.5f, yf=(float)y+0.5f;
        float Ax=Au*xf+Av*yf+Ac, Bx=Bu*xf+Bv*yf+Bc, Cx=Cu*xf+Cv*yf+Cc;
        float u=Ax*inv_area, z=Bx*inv_area, w=Cx*inv_area;
        float d=u*z0+z*z1+w*z2;
        float cr=0,cg=0,cb=0,tu=0,tv=0;
        if(mode==2) { cr=u*cr0+z*cr1+w*cr2; cg=u*cg0+z*cg1+w*cg2; cb=u*cb0+z*cb1+w*cb2; }
        else if(mode==3 && !perspective) { tu=u*u0+z*u1+w*u2; tv=u*vv0+z*vv1+w*vv2; }
        int row=y*stride;
        for(int x=x0;x<=x1;x++) {
            do {
                if(u<0 || z<0 || w<0 || (!mode && u>0.025f && z>0.025f && w>0.025f))break;
                int p=row+x;
                if(has_depth && d>=t->depth[p])break;
                if(has_depth)t->depth[p]=d;
                u32 col;
                if(mode==3) {
                    float sut=tu,svt=tv;
                    if(perspective) {
                        float iw=u*q[0]+z*q[1]+w*q[2];
                        if(!(iw>0.0000001f))break;
                        sut=(u*u0*q[0]+z*u1*q[1]+w*u2*q[2])/iw;
                        svt=(u*vv0*q[0]+z*vv1*q[1]+w*vv2*q[2])/iw;
                    }
                    col=bilinear?sample_bilinear(tex,sut,svt):sample_nearest(tex,sut,svt);
                } else if(mode==2) {
                    col=channel(cr)<<16|channel(cg)<<8|channel(cb);
                } else {
                    col=flat_col;
                }
                t->color[p]=col;count++;
            } while(0);
            u+=gu; z+=gz; w+=gw; d+=gd;
            cr+=gcr; cg+=gcg; cb+=gcb; tu+=guv; tv+=gvv;
        }
    }
    return count;
}
static u32 clip_and_raster(GfxTarget *t,const Triangle *tri,int cull,int mode,int first,int last,
    const GfxTexture *tex,int perspective,int bilinear) {
    Vertex a[12],b[12]; int n=3;
    for(int i=0;i<3;i++) {
        a[i]=tri->v[i]; Vec4 p=a[i].p; Vec3 c=a[i].color;
        if(!(p.x>=-16384 && p.x<=16384 && p.y>=-16384 && p.y<=16384 &&
            p.z>=-16384 && p.z<=16384 && p.w>=-16384 && p.w<=16384 &&
            c.x>=0 && c.x<=1 && c.y>=0 && c.y<=1 && c.z>=0 && c.z<=1))return 0;
    }
    for(int p=0;p<6 && n;p++) {
        int count=0;
        for(int i=0;i<n;i++) {
            Vertex s=a[i],e=a[(i+1)%n]; float ds=plane(s.p,p),de=plane(e.p,p);
            if((ds>=0)!=(de>=0)) {if(count==12)return 0;b[count++]=mix(s,e,ds/(ds-de));}
            if(de>=0) {if(count==12)return 0;b[count++]=e;}
        }
        n=count; for(int i=0;i<n;i++)a[i]=b[i];
    }
    u32 count=0;
    for(int i=1;i+1<n;i++)count+=raster(t,a[0],a[i],a[i+1],cull,mode,first,last,tex,perspective,bilinear);
    return count;
}
u32 soft3d_triangle(GfxTarget *t,const Triangle *tri,int cull,int mode,int first,int last) {
    if(!gfx_target_valid(t)||!tri||mode<0||mode>2)return 0;
    if(first<0)first=0; if(last>t->height)last=t->height;
    if(first>=last)return 0;
    return clip_and_raster(t,tri,cull,mode,first,last,0,0,0);
}
u32 soft3d_triangle_textured(GfxTarget *t,const Triangle *tri,int cull,int first,int last,
    const GfxTexture *tex,int perspective,int bilinear) {
    if(!gfx_target_valid(t)||!tri||!texture_valid(tex)||perspective<0||perspective>1||bilinear<0||bilinear>1)return 0;
    if(first<0)first=0; if(last>t->height)last=t->height;
    if(first>=last)return 0;
    return clip_and_raster(t,tri,cull,3,first,last,tex,perspective,bilinear);
}
