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
    r.uv=(Vec2){0,0}; return r;
}
static float edge(Vec4 a,Vec4 b,float x,float y) {return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
static u32 channel(float v) {return v<=0?0:v>=1?255:(u32)(v*255);}
static u32 raster(GfxTarget *t,Vertex a,Vertex b,Vertex c,int cull,int mode,int first,int last) {
    Vertex v[3]={a,b,c};
    for(int i=0;i<3;i++) {
        if (!(v[i].p.w>0.00001f)) return 0;
        float q=1/v[i].p.w;
        v[i].p.x=(v[i].p.x*q+1)*0.5f*t->width;
        v[i].p.y=(1-v[i].p.y*q)*0.5f*t->height;
        v[i].p.z=(v[i].p.z*q+1)*0.5f;
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
    u32 count=0;
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) {
        float u=edge(v[1].p,v[2].p,x+0.5f,y+0.5f)/area;
        float z=edge(v[2].p,v[0].p,x+0.5f,y+0.5f)/area, w=1-u-z;
        if(u<0 || z<0 || w<0 || (!mode && u>0.025f && z>0.025f && w>0.025f))continue;
        float d=u*v[0].p.z+z*v[1].p.z+w*v[2].p.z;
        int p=y*t->stride+x;
        if(t->depth && d>=t->depth[p])continue;
        if(t->depth)t->depth[p]=d;
        Vec3 col=a.color;
        if(mode==2) col=(Vec3){u*v[0].color.x+z*v[1].color.x+w*v[2].color.x,
            u*v[0].color.y+z*v[1].color.y+w*v[2].color.y,
            u*v[0].color.z+z*v[1].color.z+w*v[2].color.z};
        t->color[p]=channel(col.x)<<16|channel(col.y)<<8|channel(col.z);count++;
    }
    return count;
}
u32 soft3d_triangle(GfxTarget *t,const Triangle *tri,int cull,int mode,int first,int last) {
    if(!gfx_target_valid(t)||!tri||mode<0||mode>2)return 0;
    if(first<0)first=0; if(last>t->height)last=t->height;
    if(first>=last)return 0;
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
    for(int i=1;i+1<n;i++)count+=raster(t,a[0],a[i],a[i+1],cull,mode,first,last);
    return count;
}
