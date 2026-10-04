#include "pollikmark.h"
#include "apps.h"
#include "app_host.h"
#include "../soft3d.h"

/* Method: deterministic workloads, >=2s/level and >=3 completed iterations,
 * 10s hard deadline (discard incomplete iteration). Exact top-128 intervals
 * for ceil(n/100), n<=8192, so the 1% low is a real tail rather than two
 * samples; intervals include cooperative scheduling and painting.
 * Throughput uses measured slice time, NOT presentation FPS. No render work,
 * allocation or yields during measured steps. Viewport capped at 256x160 with
 * preserved client aspect, displayed 1:1 centered (never stretch); memory work
 * <=256KiB/poll. All 3D loads are software rasterized with matrix transforms,
 * lighting, depth and (test 6) a bit-masked texture sampler. Score = arithmetic
 * mean of completed non-allocation level workload rates / fixed references:
 * 10M pixels/s, 10K shapes/s, 10K triangles/s, 2M textured pixels/s, 60
 * compositor frames/s, 100MiB/s memory, times 100. Not a GPU or
 * cross-resolution score. Unsupported/failed/deadline levels excluded and
 * visibly marked, never zero. */
#define PM_CLEAR 0x182238u
#define PM_TEAL 0x2dd4bfu
#define PM_VIOLET 0x8b7fdbu
#define PM_GREEN 0x34d399u
#define PM_AMBER 0xfbbf24u
#define PM_RED 0xf87171u
#define PM_BLUE 0x60a5fau
#define PM_PINK 0xf472b6u
#define PM_TEXSIZE 128
/* Longer, denser sampling: >=2s per level, 10s hard deadline and up to 8192
 * intervals so the 1% low averages ~82 real samples instead of two. */
#define PM_LEVEL_MIN_US 2000000u
#define PM_LEVEL_MAX_US 10000000u
#define PM_INTERVAL_CAP 8192
/* Keep the slowest 5% (plus margin) so both p95 and p99 are real order
 * statistics over the whole level, not just the top 1%. */
#define PM_SLOW_MAX (PM_INTERVAL_CAP / 20 + 16)

u8 pollikmark_icon[256], pollikmark_alpha[128];
const u32 pollikmark_palette[4]={0x242b48,0x5edac9,0x8b7fdb,0xffffff};
static const char *names[8]={"Fill Rate","2D Shapes","Triangle","Cube","Geometry","Texture","Compositor","Memory"};
static const char *unit_names[8]={"px/s","shapes/s","tris/s","tris/s","tris/s","texpx/s","fps","B/s"};
static const u32 refs[8]={10000000u,10000u,10000u,10000u,10000u,2000000u,60u,104857600u};
static const char *ops[6]={"clear","copy","blend","surface","pitched copy","allocation + touch"};
static const u32 counts[5]={100,500,1000,5000,10000};
static const u32 mib[5]={1,4,8,16,32};
static const u8 levels[8]={1,4,1,3,5,3,1,30};
/* Publicly named read-only probe symbols, not a guest control interface.
 * status: 0 absent,1 completed,2 cancelled/deadline/sample cap,3 no memory.
 * Internal read-only probe: packed 68 bytes, with 64-bit rate and units.
 * This is not a syscall or on-disk structure. */
typedef struct __attribute__((packed)) {
    u32 status,n,mean_us,min_us,max_us,low_fps,fps;
    u64 rate;
    u32 work_us;
    u64 units;
    u32 paint_us,compose_us,present_us,total_us,alloc_us;
} MarkResult;
MarkResult pollikmark_results[8][30];
/* Test-only distribution from completed compositor frames in workload 6:
 * [sample count, mean us, p95 us, max us]. */
u32 pollikmark_compositor_frame_stats[4];
#define PM_COMPOSITOR_FRAME_SAMPLE_CAP 512
static u32 compositor_frame_samples[PM_COMPOSITOR_FRAME_SAMPLE_CAP];
/* Separate probe ABI: successful raster writes and writes/s, not clear pixels. */
struct { u32 pixels, rate; } pollikmark_raster[8][30];
static u32 frame_pixels, raster_pixels;
u32 pollikmark_intervals[PM_INTERVAL_CAP];
/* Parallel percentile probe: [test][level][0]=p95, [1]=p99 microseconds.
 * Separate from the internal MarkResult throughput probe. */
u32 pollikmark_percentiles[8][30][2];
static MarkResult live_result;
u32 pollikmark_running,pollikmark_test,pollikmark_level,pollikmark_completed;
static u32 all, opened, info, pollikmark_generation, progress_ms;
static int summary;
static int win_w=680,win_h=410;
static GfxTarget target;
static u32 *front;
static u32 surface_bytes, front_valid;
static u8 *mem_a,*mem_b;
static u32 memory_bytes, memory_offset;
static u32 phase, item, row, clear_offset;
static u64 frame_units;
static u32 n, minimum, maximum, slow[PM_SLOW_MAX], work_us;
static u64 level_start, iteration_start, sum_us, units, measured_us, last_ui;
static AppPerfView baseline;
static const char *message="Ready - choose 1..8 or Enter for full run";
static Triangle frame_triangle;
static Mat4 frame_mvp, frame_pv, frame_mvp_tex;
static Mat4 geo_mvp[16];
static u32 frame_clock;
/* Software texture, 128x128 RGB888, wrap by 128-mask. No upload path. */
static u32 tex_pixels[PM_TEXSIZE*PM_TEXSIZE];
static GfxTexture scene_tex={tex_pixels,PM_TEXSIZE,PM_TEXSIZE};

/* ---------------------------------------------------------------- layout -- */
typedef struct { int x,y,w,h; } PmRect;
static int pm_hit(PmRect r,int x,int y) {
    return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;
}
typedef struct { u32 bg,panel,card,border,row,row_sel,text,muted,grid; } PmTheme;
typedef struct {
    int compact, header_h, content_y, footer_y;
    PmRect list, rows[8], view, stat, graph, run, stop, info;
} PmLayout;
static PmTheme pm_theme(void) {
    if(ui_is_dark())
        return (PmTheme){0x0b0f17,0x111725,0x151d2e,0x243049,0x0e1420,0x14313a,0xe8eefc,0x8595b0,0x1c2638};
    return (PmTheme){0xe9edf5,0xf5f7fb,0xffffff,0xd4dbe8,0xfbfcfe,0xdcf7f2,0x16203a,0x64718c,0xe8edf5};
}
static PmLayout pm_layout(int width,int height) {
    PmLayout l;
    memset(&l,0,sizeof(l));
    l.compact=(height<400)||(width<620);
    l.header_h=l.compact?40:44;
    l.content_y=34+l.header_h;
    int footer=height-(height>=410?46:24);
    l.footer_y=footer;
    for(int i=0;i<8;i++)l.rows[i]=(PmRect){8,78+i*22,169,22};
    l.list=(PmRect){8,74,169,22*8+4};
    int rx=186, rw=width-rx-8;
    int vw=(target.width>0?target.width:192), vh=(target.height>0?target.height:128);
    if(l.compact) {
        l.view=(PmRect){rx,76,vw,vh};
        l.stat=(PmRect){rx,76+vh+4,rw,footer-80-vh};
    } else {
        l.view=(PmRect){rx,76,vw+8,vh+8};
        int sx=rx+l.view.w+8;
        l.stat=(PmRect){sx,76,width-sx-8,l.view.h};
        if(l.stat.w<120)l.stat=(PmRect){rx,76+l.view.h+6,rw,64};
        int gy=l.stat.y+l.stat.h+6;
        if(gy<226)gy=226;
        l.graph=(PmRect){rx,gy,rw,footer-6-gy};
        l.run=(PmRect){8,258,62,22};
        l.stop=(PmRect){74,258,44,22};
        l.info=(PmRect){122,258,55,22};
    }
    return l;
}

/* ------------------------------------------------------------ ui helpers -- */
static void pm_rect(int x,int y,int w,int h,u32 c) {
    if(x<0){w+=x;x=0;}
    if(y<34){h+=y-34;y=34;}
    if(w<=0||h<=0)return;
    if(x+w>win_w)w=win_w-x;
    if(y+h>win_h)h=win_h-y;
    if(w<=0||h<=0)return;
    ui_bridge_rect(x,y,w,h,c);
}
static int pm_tw(const char *s,int scale) {
    int w=0;
    while(*s){w+=sys_get_glyph_advance((u8)*s,scale);s++;}
    return w;
}
static void pm_text_clip(int x,int y,int maxw,const char *s,u32 c,int scale) {
    if(scale<1)scale=1; if(scale>5)scale=5;
    if(y<34||y+16*scale>win_h||x<0||x>=win_w||maxw<=0)return;
    char b[128];int k=0,w=0;
    while(s[k]&&k<127) {
        int a=sys_get_glyph_advance((u8)s[k],scale);
        if(w+a>maxw)break;
        w+=a;b[k]=s[k];k++;
    }
    b[k]=0;
    if(k)ui_bridge_text(x,y,b,c,scale);
}
static void pm_text(int x,int y,const char *s,u32 c,int scale) {
    pm_text_clip(x,y,win_w-x,s,c,scale);
}
static void pm_center(int x,int y,int w,const char *s,u32 c,int scale) {
    if(w<=0)return;
    pm_text_clip(x+(w-pm_tw(s,scale))/2,y,w,s,c,scale);
}
static void pm_right(int xr,int y,const char *s,u32 c,int scale) {
    pm_text(xr-pm_tw(s,scale),y,s,c,scale);
}
static int pm_put(char *b,int k,u32 v) {
    char t[12];int i=0;
    do {t[i++]=(char)('0'+v%10);v/=10;} while(v);
    while(i)b[k++]=t[--i];
    return k;
}
static int pm_put64(char *b,int k,u64 v) {
    char t[20];int i=0;
    do {
        u64 quotient=0;u32 remainder=0;
        for(int bit=63;bit>=0;bit--) {
            remainder=(remainder<<1)|(u32)((v>>bit)&1u);
            if(remainder>=10u){remainder-=10u;quotient|=1ull<<bit;}
        }
        t[i++]=(char)('0'+remainder);v=quotient;
    } while(v);
    while(i)b[k++]=t[--i];
    return k;
}
static void pm_short(char *b,u64 v) {
    int k=0;
    if(v>=1000000000u){u64 whole=gfx_ratio64(v,1000000000u);k=pm_put64(b,k,whole);b[k++]='.';k=pm_put(b,k,(u32)(gfx_ratio64(v,100000000u)-whole*10u));b[k++]='G';}
    else if(v>=1000000u){k=pm_put64(b,k,gfx_ratio64(v,1000000u));b[k++]='.';k=pm_put(b,k,(u32)gfx_ratio64(v,100000u)%10u);b[k++]='M';}
    else if(v>=1000u){k=pm_put64(b,k,gfx_ratio64(v,1000u));b[k++]='.';k=pm_put(b,k,(u32)gfx_ratio64(v,100u)%10u);b[k++]='K';}
    else k=pm_put64(b,k,v);
    b[k]=0;
}
static void pm_card(PmRect r,u32 bg,u32 border) {
    ui_bridge_roundrect_stroke(r.x,r.y,r.w,r.h,UI_RADIUS_MEDIUM,1,border,bg);
}
static u32 pm_test_status(int t) {
    if(pollikmark_running&&(int)pollikmark_test==t)return 1;
    u32 done=0,failed=0;
    for(u32 l=0;l<levels[t];l++) {
        u32 s=pollikmark_results[t][l].status;
        if(s==1)done++;
        else if(s)failed++;
    }
    if(levels[t]&&done==levels[t])return 2;
    if(failed)return 3;
    if(done)return 4;
    return 0;
}
/* Mean 0..100 points of completed levels of one test (score() shares refs). */
static u32 pm_test_points(int t) {
    u64 total=0;u32 count=0;
    for(u32 l=0;l<levels[t];l++) {
        MarkResult *r=&pollikmark_results[t][l];
        if(r->status!=1||(t==7&&l%6==5))continue;
        total+=gfx_ratio((u64)r->rate*100,refs[t]);count++;
    }
    return count?gfx_ratio(total,count):0;
}
/* Mean raw throughput of completed levels of one test (for the summary table). */
static u64 pm_test_rate(int t) {
    u64 total=0;u32 count=0;
    for(u32 l=0;l<levels[t];l++) {
        MarkResult *r=&pollikmark_results[t][l];
        if(r->status!=1||(t==7&&l%6==5))continue;
        total+=r->rate;count++;
    }
    return count?gfx_ratio64(total,count):0;
}
static u32 pm_test_levels_done(int t) {
    u32 done=0;
    for(u32 l=0;l<levels[t];l++) {
        MarkResult *r=&pollikmark_results[t][l];
        if(r->status==1&&!(t==7&&l%6==5))done++;
    }
    return done;
}
static u32 score(void) {
    u64 total=0;u32 count=0;
    for(int t=0;t<8;t++)for(u32 l=0;l<levels[t];l++) {
        MarkResult *r=&pollikmark_results[t][l];
        if(r->status!=1||(t==7&&l%6==5))continue;
        total+=gfx_ratio((u64)r->rate*100,refs[t]);count++;
    }
    return count?gfx_ratio(total,count):0;
}
static char pm_grade(u32 s) {
    if(s>=90)return 'S';
    if(s>=75)return 'A';
    if(s>=60)return 'B';
    if(s>=40)return 'C';
    if(s>=20)return 'D';
    return 'E';
}
static u32 pm_grade_color(u32 s) {
    if(s>=75)return PM_GREEN;
    if(s>=40)return PM_AMBER;
    return PM_RED;
}

/* ------------------------------------------------------------- workloads -- */
static Mat4 pm_rotx(float c,float s) {
    Mat4 m=soft3d_identity();
    m.m[5]=c;m.m[6]=-s;m.m[9]=s;m.m[10]=c;
    return m;
}
static Mat4 pm_roty(float c,float s) {
    Mat4 m=soft3d_identity();
    m.m[0]=c;m.m[2]=s;m.m[8]=-s;m.m[10]=c;
    return m;
}
static Vec3 pm_shade(Vec3 base,Vec3 n) {
    float d=n.x*0.4082f+n.y*0.8165f+n.z*0.4082f;
    if(d<0)d=0;
    float a=0.30f+0.70f*d;
    return (Vec3){base.x*a,base.y*a,base.z*a};
}
static void make_texture(void) {
    for(int y=0;y<PM_TEXSIZE;y++)for(int x=0;x<PM_TEXSIZE;x++) {
        int tile=(x>>4)*3+(y>>4)*5;
        int checker=((x>>2)+(y>>2))&1;
        int edge=((x&15)==0)||((y&15)==0);
        u32 r=40u+(u32)(tile&7)*24u,g=70u+(u32)((tile*3)&7)*22u,b=110u+(u32)((tile*5)&7)*17u;
        if(checker){r=r*3/4+50;g=g*3/4+50;b=b*3/4+50;}
        if(edge){r=232;g=236;b=242;}
        if(r>255)r=255; if(g>255)g=255; if(b>255)b=255;
        tex_pixels[y*PM_TEXSIZE+x]=(r<<16)|(g<<8)|b;
    }
}
static void make_cube(void) {
    static const u8 faces[36]={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
    static const signed char vertices[8][3]={{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    static const Vec3 normals[6]={{0,0,-1},{0,0,1},{0,-1,0},{0,1,0},{-1,0,0},{1,0,0}};
    static const Vec3 bases[6]={{0.16f,0.34f,0.72f},{0.10f,0.66f,0.62f},{0.38f,0.32f,0.70f},
        {0.88f,0.58f,0.16f},{0.78f,0.28f,0.48f},{0.22f,0.68f,0.36f}};
    u32 i=item,face=i/2;
    for(int k=0;k<3;k++) {
        int j=faces[i*3+k];const signed char *v=vertices[j];
        Vec3 col;
        if(pollikmark_level==0) col=normals[face];
        else if(pollikmark_level==1) col=pm_shade(bases[face],normals[face]);
        else col=pm_shade(bases[face],(Vec3){v[0]*0.57735f,v[1]*0.57735f,v[2]*0.57735f});
        if(pollikmark_level==0)col=(Vec3){0.18f,0.83f,0.75f};
        frame_triangle.v[k]=(Vertex){soft3d_transform(frame_mvp,(Vec4){v[0]*0.65f,v[1]*0.65f,v[2]*0.65f,1}),col,{0,0}};
    }
}
static void make_spin_triangle(void) {
    static const Vec3 colors[3]={{0.95f,0.35f,0.35f},{0.35f,0.90f,0.45f},{0.40f,0.50f,1.0f}};
    static const float px[3]={-0.85f,0.85f,0.0f},py[3]={-0.85f,-0.85f,0.85f},pz[3]={0.15f,-0.15f,0.20f};
    for(int k=0;k<3;k++)
        frame_triangle.v[k]=(Vertex){soft3d_transform(frame_mvp,(Vec4){px[k],py[k],pz[k],1}),colors[k],{0,0}};
}
static void make_particle(void) {
    static const Vec3 hues[6]={{0.95f,0.45f,0.30f},{0.95f,0.80f,0.30f},{0.40f,0.90f,0.50f},
        {0.30f,0.75f,0.95f},{0.60f,0.55f,0.98f},{0.95f,0.45f,0.75f}};
    u32 i=item;
    float px=(float)((i*37)%200)/100.0f-1.0f;
    float py=(float)((i*53)%200)/100.0f-1.0f;
    float pz=(float)((i*29)%240)/100.0f-1.2f;
    float s=0.05f+(float)((i*7)%6)*0.02f;
    Vec4 o=soft3d_transform(frame_pv,(Vec4){px,py,pz,1});
    Vec3 hue=hues[i%6];
    for(int k=0;k<3;k++) {
        float x=k==0?-1.0f:k==1?1.0f:0.0f;
        float y=k==2?1.0f:-1.0f;
        Vec4 p=soft3d_transform(geo_mvp[i&15],(Vec4){x*s,y*s,0,1});
        p.x+=o.x;p.y+=o.y;p.z+=o.z;p.w+=o.w;
        frame_triangle.v[k]=(Vertex){p,(Vec3){hue.x*(k==1?0.85f:1.0f),hue.y*(k==1?0.85f:1.0f),hue.z*(k==1?0.85f:1.0f)},{0,0}};
    }
}
static void make_textured_plane(void) {
    static const int tri[2][3]={{0,1,2},{0,2,3}};
    float e=2.0f;
    float u0=(float)(frame_clock%32)/32.0f;
    Vertex q[4]={{{-e,-e,0,1},{1,1,1},{u0,0}},{{e,-e,0,1},{1,1,1},{u0+3.0f,0}},
        {{e,e,0,1},{1,1,1},{u0+3.0f,3.0f}},{{-e,e,0,1},{1,1,1},{u0,3.0f}}};
    for(int k=0;k<3;k++) {
        Vertex v=q[tri[item&1][k]];
        frame_triangle.v[k]=(Vertex){soft3d_transform(frame_mvp_tex,v.p),v.color,v.uv};
    }
}
static void make_triangle(void) {
    if(pollikmark_test==3)make_cube();
    else if(pollikmark_test==4)make_particle();
    else if(pollikmark_test==5)make_textured_plane();
    else make_spin_triangle();
}
static void begin_frame(void) {
    iteration_start=app_host_time_us();work_us=frame_units=frame_pixels=0;
    item=row=clear_offset=memory_offset=0;phase=0;
    Mat4 view=soft3d_identity();
    view.m[7]=-0.1f;view.m[11]=-3;
    Mat4 proj=soft3d_perspective(1.7f,(float)target.width/target.height,0.1f,20);
    frame_pv=soft3d_multiply(proj,view);
    if(pollikmark_test==2||pollikmark_test==3) {
        /* Rational rotation (sin=2t/(1+t*t), cos=(1-t*t)/(1+t*t)), no libm. */
        float t=(float)((int)(frame_clock%160)-80)/80.0f,c=(1-t*t)/(1+t*t),s=2*t/(1+t*t);
        Mat4 tumble=pm_rotx(0.9063078f,0.4226183f);
        frame_mvp=soft3d_multiply(frame_pv,soft3d_multiply(tumble,pm_roty(c,s)));
    } else if(pollikmark_test==5) {
        /* Tilted textured plane: 25 deg yaw, -55 deg pitch, hardcoded constants. */
        Mat4 model=soft3d_multiply(pm_roty(0.9063078f,0.4226183f),pm_rotx(0.5735764f,-0.8191520f));
        frame_mvp_tex=soft3d_multiply(frame_pv,model);
    } else if(pollikmark_test==4) {
        for(int k=0;k<16;k++) {
            float t=(float)(k-8)/8.0f,c=(1-t*t)/(1+t*t),s=2*t/(1+t*t);
            float u=(float)(k%5-2)/2.0f,c2=(1-u*u)/(1+u*u),s2=2*u/(1+u*u);
            geo_mvp[k]=soft3d_multiply(frame_pv,soft3d_multiply(pm_roty(c,s),pm_rotx(c2,s2)));
        }
    }
    frame_clock++;
}
static int graphics_step(void) {
    if(phase==0) {
        u32 color=pollikmark_test==0?(frame_clock&1?0x1b2740u:PM_CLEAR):PM_CLEAR;
        u32 amount=gfx_clear(&target,clear_offset,2048,color);
        clear_offset+=amount;
        if(pollikmark_test==0)frame_units+=amount;
        if(clear_offset==target.capacity) {phase=1;if(pollikmark_test==0)return 1;}
        return 0;
    }
    if(pollikmark_test==1) {
        static const u32 palette[5]={0x2dd4bf,0x8b7fdb,0xf59e0b,0x38bdf8,0xf87171};
        int x=(int)(item*37%target.width)-5,y=(int)(item*53%target.height)-3;
        gfx_shape(&target,x,y,7+item%17,7+item%11,palette[item%5],item%5);
        item++;frame_units++;
        return item==counts[pollikmark_level];
    }
    if(pollikmark_test==2) {
        u32 limit=1;
        make_triangle();
        frame_pixels+=soft3d_triangle(&target,&frame_triangle,0,2,0,target.height);
        item++;frame_units++;
        return item==limit;
    }
    if(pollikmark_test==3) {
        u32 limit=12;
        make_triangle();
        frame_pixels+=soft3d_triangle(&target,&frame_triangle,1,(int)pollikmark_level,0,target.height);
        item++;frame_units++;
        return item==limit;
    }
    if(pollikmark_test==4) {
        make_triangle();
        frame_pixels+=soft3d_triangle(&target,&frame_triangle,0,2,0,target.height);
        item++;frame_units++;
        return item==counts[pollikmark_level];
    }
    /* Texture: one whole textured triangle per step, writes are the unit. */
    if(pollikmark_test==5) {
        make_triangle();
        u32 writes=soft3d_triangle_textured(&target,&frame_triangle,1,0,target.height,
            &scene_tex,pollikmark_level>0,pollikmark_level==2);
        frame_pixels+=writes;frame_units+=writes;
        item++;
        return item==2;
    }
    return 1;
}
static int memory_step(void) {
    u32 count=memory_bytes-memory_offset;
    if(count>262144)count=262144;
    u32 *a=(u32 *)(mem_a+memory_offset),*b=mem_b?(u32 *)(mem_b+memory_offset):0;
    u32 mode=pollikmark_level%6;
    if(phase==3) {
        memset(a,0x35,count);if(b)memset(b,0x68,count);
    } else if(mode==0||mode==5)memset(a,0xa5,count);
    else if(mode==1||mode==3) {
        /* surface copy is 256px scanlines; plain copy one bounded block. */
        u32 chunk=mode==3?1024:count;
        for(u32 i=0;i<count;i+=chunk)memcpy((u8 *)a+i,(u8 *)b+i,chunk);
    } else for(u32 i=0;i<count/4;i++) {
        if(mode==2)a[i]=((a[i]&0xfefefefe)>>1)+((b[i]&0xfefefefe)>>1);
        else if(i%256<240)a[i]=b[i]; /* 240px width, 256px pitch. */
    }
    memory_offset+=count;
    if(phase!=3)frame_units+=mode==2?count*3:mode==0||mode==5?count:mode==4?count/16*30:count*2;
    return memory_offset==memory_bytes;
}

/* --------------------------------------------------------- level control -- */
static void release_memory(void) {
    app_host_free(mem_a,memory_bytes); app_host_free(mem_b,memory_bytes);
    mem_a=mem_b=0; memory_bytes=0;
}
static void release_surface(void) {
    app_host_free(target.color,surface_bytes); app_host_free(target.depth,surface_bytes);
    app_host_free(front,surface_bytes); target=(GfxTarget){0}; front=0;
    surface_bytes=front_valid=0;
}
static int prepare_surface(void) {
    int w=win_w-220,h=win_h-190;
    if(w<1||h<1)return 0;
    /* Fit a real viewport aspect into a cap, no final resampling. */
    if(w>192) {h=h*192/w;w=192;}
    if(h>128) {w=w*128/h;h=128;}
    if(w<1)w=1; if(h<1)h=1;
    if(target.width==w && target.height==h)return 1;
    u32 bytes=(u32)w*h*4;
    u32 *color=app_host_alloc(bytes),*depth=app_host_alloc(bytes),*display=app_host_alloc(bytes);
    if(!color||!depth||!display) {
        app_host_free(color,bytes);app_host_free(depth,bytes);app_host_free(display,bytes);return 0;
    }
    release_surface();surface_bytes=bytes;front=display;
    target=(GfxTarget){color,(float *)depth,w,h,w,bytes/4};return 1;
}
static void reset_samples(void) {
    n=maximum=work_us=raster_pixels=0;minimum=0xffffffffu;
    sum_us=units=measured_us=0;
    for(int i=0;i<PM_SLOW_MAX;i++)slow[i]=0;
    phase=item=row=clear_offset=memory_offset=frame_units=progress_ms=0;
    level_start=iteration_start=app_host_time_us();
    app_host_metrics(&baseline);
    if(pollikmark_test==6) {
        memset(pollikmark_compositor_frame_stats,0,sizeof(pollikmark_compositor_frame_stats));
    }
}
static void sample(u32 interval) {
    if(!interval)interval=1;
    pollikmark_intervals[n]=interval;
    n++;sum_us+=interval;
    if(interval<minimum)minimum=interval;
    if(interval>maximum)maximum=interval;
    for(int i=0;i<PM_SLOW_MAX;i++) if(interval>slow[i]) {
        u32 swap=slow[i];slow[i]=interval;interval=swap;
    }
}
static void compositor_commit_frame_stats(u32 after_frame,u32 count) {
    u32 nframes=app_host_copy_frame_times(after_frame,count,compositor_frame_samples,
                                          PM_COMPOSITOR_FRAME_SAMPLE_CAP);
    if(!nframes)return;
    u64 sum=0;
    for(u32 i=0;i<nframes;i++) {
        u32 value=compositor_frame_samples[i],j=i;
        while(j&&compositor_frame_samples[j-1]>value) {
            compositor_frame_samples[j]=compositor_frame_samples[j-1];j--;
        }
        compositor_frame_samples[j]=value;
        sum+=value;
    }
    u32 rank=(95u*nframes+99u)/100u;
    if(rank<1)rank=1;
    if(rank>nframes)rank=nframes;
    pollikmark_compositor_frame_stats[0]=nframes;
    pollikmark_compositor_frame_stats[1]=gfx_ratio(sum,nframes);
    pollikmark_compositor_frame_stats[2]=compositor_frame_samples[rank-1];
    pollikmark_compositor_frame_stats[3]=compositor_frame_samples[nframes-1];
}
static void snapshot(MarkResult *r,u32 status) {
    r->status=status;r->n=n;r->min_us=n?minimum:0;r->max_us=maximum;
    r->mean_us=gfx_ratio(sum_us,n);r->fps=gfx_ratio((u64)n*1000000,(u32)sum_us);
    u32 k=(n+99)/100,total=0;
    if(k>PM_SLOW_MAX)k=PM_SLOW_MAX;
    for(u32 i=0;i<k;i++)total+=slow[i];
    r->low_fps=gfx_ratio((u64)k*1000000,total);
    /* Real order statistics: slow[] is the descending top slice, so the
     * ascending p95/p99 ranks map to slow[n - rank]. Exposed separately to
     * keep the percentile samples separate from throughput results. */
    u32 p95=0,p99=0;
    if(n) {
        u32 r95=(95u*n+99u)/100u; if(r95<1)r95=1; if(r95>n)r95=n;
        u32 r99=(99u*n+99u)/100u; if(r99<1)r99=1; if(r99>n)r99=n;
        u32 i95=n-r95, i99=n-r99;
        if(i95>=PM_SLOW_MAX)i95=PM_SLOW_MAX-1;
        if(i99>=PM_SLOW_MAX)i99=PM_SLOW_MAX-1;
        p95=slow[i95];p99=slow[i99];
    }
    pollikmark_percentiles[pollikmark_test][pollikmark_level][0]=p95;
    pollikmark_percentiles[pollikmark_test][pollikmark_level][1]=p99;
    r->work_us=(u32)measured_us;r->units=units;
    r->rate=gfx_ratio64(units*1000000,(u32)measured_us);
    pollikmark_raster[pollikmark_test][pollikmark_level].pixels=raster_pixels;
    pollikmark_raster[pollikmark_test][pollikmark_level].rate=gfx_ratio((u64)raster_pixels*1000000,(u32)measured_us);
    if(pollikmark_test==6) {
        u32 frames=0;
        AppPerfView end;app_host_metrics(&end);
        frames=end.frames-baseline.frames;
        r->paint_us=gfx_ratio(end.paint_us-baseline.paint_us,frames);
        r->compose_us=gfx_ratio(end.compose_us-baseline.compose_us,frames);
        r->present_us=gfx_ratio(end.present_us-baseline.present_us,frames);
        r->total_us=gfx_ratio(end.total_us-baseline.total_us,frames);
        if(status) compositor_commit_frame_stats(baseline.frames,frames);
        r->n=frames;r->fps=gfx_ratio((u64)frames*1000000,(u32)(app_host_time_us()-level_start));
        r->rate=r->fps;r->low_fps=0;r->mean_us=r->min_us=r->max_us=0;
        pollikmark_percentiles[6][pollikmark_level][0]=0;
        pollikmark_percentiles[6][pollikmark_level][1]=0;
    }
}
static void stop(void) {
    if(pollikmark_running) snapshot(&pollikmark_results[pollikmark_test][pollikmark_level],2);
    pollikmark_running=all=0;release_memory();message="Stopped - completed raw levels retained";
    app_host_invalidate(APP_POLLIKMARK);
}
static int begin_level(void);
static int begin_level(void) {
    release_memory();reset_samples();
    MarkResult *r=&pollikmark_results[pollikmark_test][pollikmark_level];
    *r=(MarkResult){0};live_result=*r;snapshot(&live_result,0);
    if(pollikmark_test==7) {
        memory_bytes=mib[pollikmark_level/6]*1048576u;
        u64 start=app_host_time_us();
        mem_a=app_host_alloc(memory_bytes);
        if(pollikmark_level%6!=0 && pollikmark_level%6!=5)mem_b=app_host_alloc(memory_bytes);
        r->alloc_us=(u32)(app_host_time_us()-start);
        if(!mem_a || ((pollikmark_level%6!=0 && pollikmark_level%6!=5)&&!mem_b)) {
            r->status=3;release_memory();message="Allocation failed - existing results preserved";return 0;
        }
        phase=3; /* Incremental initialization before measured level. */
    } else begin_frame();
    message="Running - Esc cancels; close releases buffers";return 1;
}
static void start(u32 test,int full) {
    stop();info=0;summary=0;
    pollikmark_test=test;pollikmark_level=0;all=full;
    if(!prepare_surface()) {message="No memory for viewport (old surface retained)";return;}
    pollikmark_running=1;
    if(!begin_level())pollikmark_running=all=0;
}
static void advance(u32 status) {
    MarkResult *r=&pollikmark_results[pollikmark_test][pollikmark_level];snapshot(r,status);
    if(status==1)pollikmark_completed++;
    release_memory();
    if(status!=1) {
        if(!all) {pollikmark_running=0;message="Budget/sample limit - partial level excluded";return;}
        pollikmark_level=levels[pollikmark_test]-1; /* Stop this test's higher loads. */
    }
    if(++pollikmark_level>=levels[pollikmark_test]) {
        if(!all) {pollikmark_level--;pollikmark_running=0;message="Completed - raw results retained";return;}
        pollikmark_level=0;pollikmark_test++;
        if(pollikmark_test==8) {pollikmark_test=7;pollikmark_level=29;pollikmark_running=all=0;message="Full run complete - see RESULTS SUMMARY";summary=1;return;}
    }
    /* Failed memory sizes are recorded, never dereferenced. End at first
     * exhausted size: larger contiguous allocations cannot be assumed. */
    if(!begin_level()) {
        pollikmark_running=all=0;
        message="Memory exhausted - available completed results retained";
    }
}
int pollikmark_poll(void) {
    if(!opened||!pollikmark_running)return 0;
    u64 now=app_host_time_us(),deadline=now+2500;
    progress_ms=(u32)(now-level_start)/1000;
    if(pollikmark_test==6) {
        snapshot(&live_result,0);
        if(now-level_start>=PM_LEVEL_MIN_US)advance(1);
        return 1; /* Real repaint demand, not synthetic fake windows. */
    }
    if(now-level_start>=PM_LEVEL_MAX_US && phase!=3) {advance(2);return 1;}
    if(pollikmark_test==7) {
        u64 before=app_host_time_us();int done=memory_step();u32 elapsed=(u32)(app_host_time_us()-before);
        if(phase==3) {
            if(done) {reset_samples();begin_frame();} return 1;
        }
        work_us+=elapsed;
        if(!done)goto progress;
    } else {
        int done=0;
        /* At most 16 bounded raster slices or small shapes per poll, also
         * bounded if the host has only the ~8.3ms PIT clock. */
        for(int cap=0;cap<16 && app_host_time_us()<deadline;cap++) {
            u64 before=app_host_time_us();done=graphics_step();
            work_us+=(u32)(app_host_time_us()-before);if(done)break;
        }
        if(!done)goto progress;
        u32 *swap=front;front=target.color;target.color=swap;front_valid=1;pollikmark_generation++;
    }
    now=app_host_time_us();u32 interval=(u32)(now-iteration_start);sample(interval);
    units+=frame_units;
    if(!work_us)work_us=interval;
    measured_us+=work_us;
    raster_pixels+=frame_pixels;
    live_result=pollikmark_results[pollikmark_test][pollikmark_level];snapshot(&live_result,0);
    if(now-level_start>=PM_LEVEL_MIN_US && n>=3)advance(1);
    else if(n>=PM_INTERVAL_CAP)advance(2);
    else begin_frame();
    return 1;
progress:
    now=app_host_time_us();
    if(now-last_ui>=100000) {
        last_ui=now;live_result=pollikmark_results[pollikmark_test][pollikmark_level];
        snapshot(&live_result,0);return 1;
    }return 0;
}
void pollikmark_init(void) {
    memset(pollikmark_alpha,255,sizeof(pollikmark_alpha));
    for(int y=0;y<16;y++)for(int x=0;x<16;x++)pollikmark_icon[y*16+x]=
        (x>3 && x<12 && y>2 && y<13)?((x+y)%3+1):0;
    make_texture();
}
void pollikmark_open(void) {opened=1;}
void pollikmark_close(void) {stop();opened=0;release_surface();}
void pollikmark_resize(int width,int height) {
    if(width==win_w&&height==win_h)return;
    win_w=width;win_h=height;
    if(target.color) {
        int w=target.width,h=target.height;
        if(!prepare_surface())message="Resize allocation failed - old viewport retained";
        else if(pollikmark_running && (w!=target.width || h!=target.height)) {
            /* Restart only this level; completed levels and run-all survive. */
            if(!begin_level())pollikmark_running=all=0;
        }
    }
}
void pollikmark_key(u8 code,char ch,int shift,int control) {
    (void)shift;(void)control;
    if(code==1||ch=='s')stop();
    else if(ch>='1'&&ch<='8')start(ch-'1',0);
    else if(code==28)start(0,1);
    else if(ch=='i')info=!info;
    else if(!pollikmark_running && (ch=='n'||ch=='p')) {
        u32 max=levels[pollikmark_test];
        if(max)pollikmark_level=(pollikmark_level+max+(ch=='n'?1:-1))%max;
    }
}
void pollikmark_click(int x,int y) {
    if(y>=42&&y<68 && win_h<410) {
        if(x<120)start(0,1);else if(x<220)stop();else info=!info;
        return;
    }
    if(x>=8 && x<178 && y>=78 && y<78+8*22) {
        start((u32)((y-78)/22), 0);
        return;
    }
    PmLayout l=pm_layout(win_w,win_h);
    if(!l.compact) {
        if(pm_hit(l.run,x,y)){start(0,1);return;}
        if(pm_hit(l.stop,x,y)){stop();return;}
        if(pm_hit(l.info,x,y)){info=!info;return;}
    }
    for(int i=0;i<8;i++) if(pm_hit(l.rows[i],x,y)){start((u32)i,0);return;}
}

/* ------------------------------------------------------------- ui render -- */
static void pm_draw_row(PmTheme th,PmRect r,int i) {
    int sel=((u32)i==pollikmark_test);
    if(sel) {
        ui_bridge_rect(8,r.y,169,r.h,0x405574);
        pm_rect(8,r.y,3,r.h,PM_TEAL);
    } else {
        pm_rect(r.x,r.y,r.w,r.h,th.row);
        pm_rect(r.x,r.y+r.h-1,r.w,1,th.border);
    }
    char num[4]={(char)('1'+i),'.',0};
    pm_text(12,r.y+2,num,sel?PM_TEAL:th.muted,1);
    pm_text(34,r.y+2,names[i],sel?th.text:th.muted,1);
    u32 st=pm_test_status(i);
    u32 dotc=st==1?PM_TEAL:st==2?PM_GREEN:st==3?PM_RED:st==4?PM_AMBER:th.muted;
    pm_rect(r.x+r.w-12,r.y+(r.h-7)/2,7,7,dotc);
    u32 pts=pm_test_points(i);
    if(pts>0) {
        int bx=r.x+r.w-42,bw=26;
        pm_rect(bx,r.y+r.h-5,bw,2,th.grid);
        int fill=(int)((pts*(u32)bw)/100u);
        if(fill>bw)fill=bw;
        if(fill>0)pm_rect(bx,r.y+r.h-5,fill,2,st==2?PM_GREEN:PM_TEAL);
    }
}
static void pm_draw_button(PmRect r,const char *label,u32 bg,u32 fg) {
    pm_rect(r.x,r.y,r.w,r.h,bg);
    pm_center(r.x,r.y+(r.h-16)/2,r.w,label,fg,1);
}
static void pm_draw_graph(PmTheme th,PmRect r) {
    if(r.w<40||r.h<40)return;
    pm_card(r,th.card,th.border);
    pm_text(r.x+8,r.y+4,"FRAME TIME / us",th.muted,1);
    int gx=r.x+8,gy=r.y+22,gw=r.w-16,gh=r.h-30;
    if(gw<20||gh<16)return;
    pm_rect(gx,gy,gw,gh,th.bg);
    for(int i=1;i<4;i++)pm_rect(gx,gy+gh*i/4,gw,1,th.grid);
    if(n&&pollikmark_test!=6) {
        u32 count=n,maxb=(u32)(gw/2);
        if(count>maxb)count=maxb;
        u32 mx=maximum?maximum:1;
        for(u32 k=0;k<count;k++) {
            u32 v=pollikmark_intervals[n-count+k];
            int bh=(int)gfx_ratio((u64)v*(u32)(gh-2),mx);
            if(bh<1)bh=1;
            if(bh>gh-2)bh=gh-2;
            pm_rect(gx+(int)k*2,gy+gh-1-bh,1,bh,PM_TEAL);
        }
        char b[16];number(b,maximum);
        pm_text(gx+4,gy+2,"max ",th.muted,1);
        pm_text(gx+4+pm_tw("max ",1),gy+2,b,th.text,1);
    } else pm_center(gx,gy+gh/2-8,gw,"no samples yet",th.muted,1);
}
static void pm_stat_line(PmTheme th,int x,int y,int w,const char *label,u64 v) {
    char b[24];int length=pm_put64(b,0,v);b[length]=0;
    int label_width=w-pm_tw(b,1)-8;
    if(label_width<0)return;
    pm_text_clip(x,y,label_width,label,th.muted,1);
    pm_right(x+w,y,b,th.text,1);
}
static void pm_info_pair(PmTheme th,int x,int y,int w,int bottom,
                         const char *label,const char *value) {
    if(y+12>bottom||w<48)return;
    int value_width=pm_tw(value,1);
    if(value_width>w*2/3)value_width=w*2/3;
    int label_width=w-value_width-8;
    if(label_width<24){label_width=24;value_width=w-label_width-8;}
    if(value_width<1)return;
    pm_text_clip(x,y,label_width,label,th.muted,1);
    pm_text_clip(x+w-value_width,y,value_width,value,th.text,1);
}
static void pm_info_heading(int x,int *y,int w,int bottom,const char *s) {
    if(*y+12>bottom)return;
    pm_text_clip(x,*y,w,s,PM_TEAL,1);
    *y+=16;
}
static void pm_draw_stats(PmTheme th,PmRect r,const MarkResult *live,int test) {
    pm_card(r,th.card,th.border);
    int x=r.x+10,y=r.y+6,w=r.w-20;
    if(w<60)return;
    char b[24];pm_short(b,live->rate);
    pm_text_clip(x,y,w,"THROUGHPUT",th.muted,1);
    pm_text_clip(x,y+16,w,b,PM_TEAL,2);
    pm_text_clip(x+pm_tw(b,2)+6,y+26,w-pm_tw(b,2)-6,unit_names[test],th.muted,1);
    int ly=y+52,lh=15;
    if(test==6) {
        pm_stat_line(th,x,ly,w,"Paint us",live->paint_us);ly+=lh;
        pm_stat_line(th,x,ly,w,"Compose us",live->compose_us);ly+=lh;
        pm_stat_line(th,x,ly,w,"Present us",live->present_us);ly+=lh;
        pm_stat_line(th,x,ly,w,"Total us",live->total_us);ly+=lh;
        pm_stat_line(th,x,ly,w,"Frames",live->n);
    } else {
        pm_stat_line(th,x,ly,w,"Iteration FPS",live->fps);ly+=lh;
        pm_stat_line(th,x,ly,w,"Samples",live->n);ly+=lh;
        pm_stat_line(th,x,ly,w,"Mean us",live->mean_us);ly+=lh;
        if(ly+14<r.y+r.h) {
            pm_stat_line(th,x,ly,w,"p95 us",
                         pollikmark_percentiles[test][pollikmark_level][0]);ly+=lh;
        }
        if(ly+14<r.y+r.h) {
            pm_stat_line(th,x,ly,w,"p99 us",
                         pollikmark_percentiles[test][pollikmark_level][1]);ly+=lh;
        }
        if(ly+14<r.y+r.h) {
            char t[24];int k=0;
            k=pm_put(t,k,live->min_us);t[k++]='/';k=pm_put(t,k,live->max_us);t[k]=0;
            int value_width=pm_tw(t,1),label_width=w-value_width-8;
            if(value_width<=w) {
                pm_text_clip(x,ly,label_width,"Min/Max",th.muted,1);pm_right(x+w,ly,t,th.text,1);
            }
            ly+=lh;
        }
        if(ly+14<r.y+r.h) {
            pm_stat_line(th,x,ly,w,"1% Low FPS",live->low_fps);ly+=lh;
        }
        if(ly+14<r.y+r.h) {
            pm_stat_line(th,x,ly,w,"Work us",live->work_us);ly+=lh;
        }
        if(ly+14<r.y+r.h) {
            pm_stat_line(th,x,ly,w,"Units",live->units);ly+=lh;
        }
        if(ly+14<r.y+r.h) {
            const char *state = pollikmark_running ? "Running" :
                live->status == 1 ? "Complete" : live->status == 2 ? "Stopped" :
                live->status == 3 ? "No memory" : live->status == 4 ? "Partial" : "Ready";
            int value_width = pm_tw(state,1);
            pm_text_clip(x,ly,w-value_width-8,"Status",th.muted,1);
            pm_right(x+w,ly,state,th.text,1);
        }
    }
}
static void pm_draw_info(PmTheme th,PmRect r) {
    GfxInfo d;AppPerfView m;app_host_metrics(&m);gfx_device_info(&d);
    pm_card(r,th.card,th.border);
    int x=r.x+10,y=r.y+6,w=r.w-20,bottom=r.y+r.h-6,k=0;
    char b[64];
    pm_info_heading(x,&y,w,bottom,"RENDER DEVICE");
    k=pm_put(b,k,(u32)d.width);b[k++]=' ';b[k++]='x';b[k++]=' ';
    k=pm_put(b,k,(u32)d.height);b[k]=0;
    pm_info_pair(th,x,y,w,bottom,"Display",b);y+=15;
    k=0;k=pm_put(b,k,(u32)d.bpp);b[k++]=' ';b[k++]='b';b[k++]='p';b[k++]='p';b[k]=0;
    pm_info_pair(th,x,y,w,bottom,"Framebuffer",b);y+=15;
    pm_info_pair(th,x,y,w,bottom,"Rasterizer","CPU software");y+=15;
    k=0;
    if(d.capabilities&GFX_COLOR){b[k++]='c';b[k++]='o';b[k++]='l';b[k++]='o';b[k++]='r';}
    if(d.capabilities&GFX_TRIANGLE){if(k)b[k++]=',';b[k++]=' ';b[k++]='s';b[k++]='h';b[k++]='a';b[k++]='p';b[k++]='e';}
    if(d.capabilities&GFX_DEPTH){if(k)b[k++]=',';b[k++]=' ';b[k++]='d';b[k++]='e';b[k++]='p';b[k++]='t';b[k++]='h';}
    if(d.capabilities&GFX_TEXTURE){if(k)b[k++]=',';b[k++]=' ';b[k++]='t';b[k++]='e';b[k++]='x';b[k++]='t';b[k++]='u';b[k++]='r';b[k++]='e';}
    if(!k){b[k++]='n';b[k++]='o';b[k++]='n';b[k++]='e';}
    b[k]=0;pm_info_pair(th,x,y,w,bottom,"Features",b);y+=15;
    pm_info_pair(th,x,y,w,bottom,"Texture store","128 x 128, software wrap");y+=15;
    pm_info_heading(x,&y,w,bottom,"SYSTEM METRICS");
    k=0;k=pm_put(b,k,app_host_free_bytes()/1024);b[k++]=' ';b[k++]='K';b[k++]='i';b[k++]='B';b[k]=0;
    pm_info_pair(th,x,y,w,bottom,"Free physical memory",b);y+=15;
    k=0;k=pm_put(b,k,m.clock_resolution_us);b[k++]=' ';b[k++]='u';b[k++]='s';b[k]=0;
    pm_info_pair(th,x,y,w,bottom,"Clock resolution",b);y+=15;
    k=0;k=pm_put(b,k,m.frames);b[k]=0;
    pm_info_pair(th,x,y,w,bottom,"Compositor frames",b);y+=15;
    k=0;k=pm_put(b,k,m.presents);b[k++]=' ';b[k++]='p';b[k++]='/' ;b[k++]='s';b[k]=0;
    pm_info_pair(th,x,y,w,bottom,"Presents per second",b);y+=15;
    k=0;k=pm_put64(b,k,m.paint_us);b[k]=0;
    pm_info_pair(th,x,y,w,bottom,"Paint total us",b);y+=15;
    k=0;k=pm_put64(b,k,m.compose_us);b[k]=0;
    pm_info_pair(th,x,y,w,bottom,"Compose total us",b);y+=15;
    k=0;k=pm_put64(b,k,m.present_us);b[k]=0;
    pm_info_pair(th,x,y,w,bottom,"Present total us",b);y+=15;
    k=0;k=pm_put64(b,k,m.total_us);b[k]=0;
    pm_info_pair(th,x,y,w,bottom,"GUI total us",b);
}
/* Post-run summary: one row per test with grade, 0..100 score, average
 * throughput, unit and completed-level count, so weak tests are obvious. */
static void pm_draw_summary(PmTheme th,PmRect r) {
    if(r.w<360||r.h<70)return;
    pm_card(r,th.card,th.border);
    pm_text(r.x+10,r.y+6,"RESULTS SUMMARY",PM_TEAL,1);
    pm_text_clip(r.x+130,r.y+6,r.w-140,
                 "S>=90  A>=75  B>=60  C>=40  D>=20  E<20",th.muted,1);
    int hx=r.x+10, c1=r.x+120, c2=r.x+168, c3=r.x+226;
    pm_text(hx,r.y+24,"TEST",th.muted,1);
    pm_text(c1,r.y+24,"GRADE",th.muted,1);
    pm_text(c2,r.y+24,"SCORE",th.muted,1);
    pm_text_clip(c3,r.y+24,r.x+r.w-10-c3-38,"AVG RATE",th.muted,1);
    pm_rect(r.x+8,r.y+38,r.w-16,1,th.border);
    int ry=r.y+44;
    for(int i=0;i<8;i++) {
        if(ry+15>r.y+r.h-42)break;
        u32 pts=pm_test_points(i), done=pm_test_levels_done(i), tot=levels[i];
        char g[2]={pts?pm_grade(pts):'-',0};
        char pb[8];number(pb,pts);
        char rb[28];pm_short(rb,pm_test_rate(i));
        int k=0;while(rb[k])k++;
        rb[k++]=' ';const char *un=unit_names[i];int m=0;
        while(un[m]&&k<26)rb[k++]=un[m++];rb[k]=0;
        if(i&1)pm_rect(r.x+6,ry-1,r.w-12,15,th.row);
        if(i==(int)pollikmark_test)pm_rect(r.x+6,ry-1,r.w-12,15,th.row_sel);
        pm_text_clip(hx,ry,106,names[i],pts?th.text:th.muted,1);
        pm_rect(c1,ry,16,14,pts?pm_grade_color(pts):th.grid);
        pm_center(c1,ry+1,16,g,0x08131a,1);
        pm_text(c2,ry,pb,pts?PM_TEAL:th.muted,1);
        pm_text_clip(c3,ry,r.x+r.w-(c3)-44,rb,th.muted,1);
        char lb[8];
        lb[0]=(char)('0'+(done/10));lb[1]=(char)('0'+(done%10));lb[2]='/';
        lb[3]=(char)('0'+(tot/10));lb[4]=(char)('0'+(tot%10));lb[5]=0;
        pm_right(r.x+r.w-10,ry,lb,th.muted,1);
        ry+=15;
    }
    /* Actionable footer: FOCUS is the weakest completed test (optimise this
     * first), SOLID is the strongest (the architecture already handles it). */
    int weak=-1,best=-1;u32 wv=1000,bv=0;
    for(int i=0;i<8;i++) {
        if(!pm_test_levels_done(i))continue;
        u32 p=pm_test_points(i);
        if(p<wv){wv=p;weak=i;}
        if(p>bv){bv=p;best=i;}
    }
    if(ry+34>r.y+r.h)return;
    pm_rect(r.x+8,ry+2,r.w-16,1,th.border);
    ry+=8;
    if(weak>=0) {
        char pb[8];number(pb,wv);
        pm_text(r.x+10,ry,"FOCUS",th.muted,1);
        pm_text(r.x+62,ry,names[weak],pm_grade_color(wv),1);
        pm_text(r.x+70+pm_tw(names[weak],1),ry,pb,th.text,1);
    }
    ry+=15;
    if(best>=0&&best!=weak) {
        char pb[8];number(pb,bv);
        pm_text(r.x+10,ry,"SOLID",th.muted,1);
        pm_text(r.x+62,ry,names[best],PM_GREEN,1);
        pm_text(r.x+70+pm_tw(names[best],1),ry,pb,th.text,1);
    }
}
void pollikmark_render(int width,int height,int active) {
    (void)active;
    PmTheme th=pm_theme();
    PmLayout l=pm_layout(width,height);
    u32 sc=score();
    char nb[16];number(nb,sc);
    /* Full background */
    pm_rect(0,34,width,height-34,th.bg);
    /* Header panel */
    pm_rect(0,34,width,l.header_h,th.panel);
    pm_rect(0,33+l.header_h,width,1,th.border);
    /* 3D badge icon */
    pm_rect(10,38,28,26,PM_VIOLET);
    pm_center(10,43,28,"3D",0xffffff,1);
    if(l.compact) {
        pm_text(44,43,"POLLIKMARK 3D",PM_TEAL,1);
        pm_right(width-10,43,nb,PM_TEAL,1);
        pm_right(width-10-pm_tw(nb,1)-6,43,"SCORE",th.muted,1);
    } else {
        pm_text(44,38,"POLLIKMARK",th.text,2);
        pm_text(44+pm_tw("POLLIKMARK",2)+8,38,"3D",PM_TEAL,2);
        pm_text(44+pm_tw("POLLIKMARK",2)+40,48,"v2.0 PRO",th.muted,1);
        pm_right(width-12,37,"TOTAL SCORE",th.muted,1);
        pm_right(width-12,52,nb,PM_TEAL,2);
        int gx=width-12-pm_tw(nb,2)-34;
        if(gx>260) {
            char g[2]={pm_grade(sc),0};
            pm_rect(gx,52,24,24,pm_grade_color(sc));
            pm_center(gx,57,24,g,0x08131a,1);
        }
    }
    /* Left: test suite rows */
    for(int i=0;i<8;i++) pm_draw_row(th,l.rows[i],i);
    if(!l.compact) {
        pm_draw_button(l.run,"RUN ALL",PM_TEAL,0x06171c);
        pm_draw_button(l.stop,"STOP",th.card,PM_RED);
        pm_draw_button(l.info,"SPECS",th.card,th.text);
        int by=l.rows[7].y+l.rows[7].h+34;
        if(by+18*8<l.footer_y-8) {
            pm_text(12,by,"SCORE BREAKDOWN",th.muted,1);
            for(int i=0;i<8;i++) {
                int yy=by+14+i*15;
                u32 pts=pm_test_points(i);
                pm_text_clip(12,yy,82,names[i],th.text,1);
                pm_rect(98,yy+5,44,3,th.grid);
                int fill=(int)((pts*44u)/100u);
                if(fill>44)fill=44;
                if(fill>0)pm_rect(98,yy+5,fill,3,PM_TEAL);
                char pb[8];number(pb,pts);
                pm_right(174,yy,pb,th.muted,1);
            }
        }
    }
    if(summary) {
        PmRect big={l.view.x,74,width-l.view.x-8,l.footer_y-6-74};
        pm_draw_summary(th,big);
    } else {
    /* Viewport preview */
    if(height>=410) {
        pm_card(l.view,th.card,th.border);
        if(pollikmark_test<6&&front_valid&&target.color&&target.width>0) {
            int bx=l.view.x+(l.view.w-target.width)/2;
            int by=l.view.y+(l.view.h-target.height)/2;
            if(bx>=0&&by>=34&&bx+target.width<=win_w&&by+target.height<=win_h)
                app_host_blit(bx,by,front,target.width,target.height,target.stride,target.capacity);
        } else {
            char b[32];
            if(pollikmark_test==6) pm_center(l.view.x,l.view.y+l.view.h/2-8,l.view.w,"COMPOSITOR WORKLOAD",th.muted,1);
            else if(pollikmark_test==7) {
                pm_center(l.view.x,l.view.y+l.view.h/2-14,l.view.w,ops[pollikmark_level%6],th.text,1);
                int k=0;k=pm_put(b,k,mib[pollikmark_level/6]);b[k++]=' ';b[k++]='M';b[k++]='i';b[k++]='B';b[k]=0;
                pm_center(l.view.x,l.view.y+l.view.h/2+2,l.view.w,b,th.muted,1);
            } else pm_center(l.view.x,l.view.y+l.view.h/2-8,l.view.w,"BENCHMARK READY",th.muted,1);
        }
    }
    /* Stats or Info panel */
    if(info) {
        PmRect ir=l.stat;
        if(l.graph.h>0) ir.h=(l.graph.y+l.graph.h)-ir.y;
        if(ir.h>40) pm_draw_info(th,ir);
    } else {
        MarkResult live=pollikmark_results[pollikmark_test][pollikmark_level];
        if(pollikmark_running)live=live_result;
        pm_draw_stats(th,l.stat,&live,pollikmark_test);
        if(l.graph.h>0) pm_draw_graph(th,l.graph);
    }
    }
    /* Resize probe ink requirement: 'Level (N/P browse)' at (190, y) where y = height>=410 ? 212 : 80.
     * Hidden while the results summary owns that area. */
    if(!summary) {
        int y=height>=410?212:80;
        pm_text_clip(190,y,width-198,"Level (N/P browse)",th.muted,1);
        char lb[12];number(lb,pollikmark_level+1);
        pm_text(190+135,y,lb,th.text,1);
    }
    /* Footer */
    int footer=height-(height>=410?46:24);
    ui_bridge_rect(8,footer,width-16,height-footer-1,0x202b40);
    if(pollikmark_running) {
        int pw=width-24;
        int span=(int)(PM_LEVEL_MIN_US/1000u);
        if(pw>10&&span>0) pm_rect(12,footer+2,pw*(int)(progress_ms>(u32)span?span:progress_ms)/span,2,PM_TEAL);
    }
    pm_text_clip(12,footer+(height>=410?16:4),width-180,message,0xd9e4f0,1);
    char tc[32];int k=0;
    tc[k++]='T';tc[k++]='E';tc[k++]='S';tc[k++]='T';tc[k++]=' ';
    k=pm_put(tc,k,(u32)pollikmark_test+1);tc[k++]='/';tc[k++]='8';
    tc[k++]=' ';tc[k++]=' ';tc[k++]='L';tc[k++]='V';tc[k++]='L';tc[k++]=' ';
    k=pm_put(tc,k,pollikmark_level+1);tc[k++]='/';k=pm_put(tc,k,levels[pollikmark_test]);tc[k]=0;
    pm_right(width-14,footer+(height>=410?16:4),tc,th.muted,1);
}
