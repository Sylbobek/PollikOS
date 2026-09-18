#include "pollikmark.h"
#include "apps.h"
#include "app_host.h"
#include "../soft3d.h"

/* Method: deterministic workloads, >=1s/level and >=3 completed iterations,
 * 5s hard deadline (discard incomplete iteration). Exact top-41 intervals for
 * ceil(n/100), n<=4096; intervals include cooperative scheduling and painting.
 * Throughput uses measured slice time, NOT presentation FPS. No render work,
 * allocation or yields. Cap viewport to 192x128 with preserved client aspect,
 * display 1:1 centered (never stretch); memory work <=256KiB/poll.
 * Score = arithmetic mean of completed non-allocation level workload rates /
 * fixed references: 10M pixels/s, 10K shapes/s, 10K triangles/s, 60 compositor
 * frames/s, 100MiB/s memory, times 100. Not a GPU or cross-resolution score.
 * Unsupported/failed/deadline levels excluded and visibly marked, never zero.
 */
u8 pollikmark_icon[256], pollikmark_alpha[128];
const u32 pollikmark_palette[4]={0x242b48,0x5edac9,0x8b7fdb,0xffffff};
static const char *names[8]={"Fill Rate","2D Shapes","Triangle","Cube","Geometry","Texture","Compositor","Memory"};
static const char *ops[6]={"clear","copy","blend","surface","pitched copy","allocation + touch"};
static const u32 counts[5]={100,500,1000,5000,10000};
static const u32 mib[5]={1,4,8,16,32};
static const u8 levels[8]={1,4,1,3,5,0,1,30};
/* Publicly named read-only probe symbols, not a guest control interface.
 * status: 0 absent,1 completed,2 cancelled/deadline/sample cap,3 no memory.
 * layout is all u32 for stable native/QEMU inspection. */
typedef struct {
    u32 status,n,mean_us,min_us,max_us,low_fps,fps,rate,work_us,units;
    u32 paint_us,compose_us,present_us,total_us,alloc_us;
} MarkResult;
MarkResult pollikmark_results[8][30];
/* Separate probe ABI: successful raster writes and writes/s, not clear pixels. */
struct { u32 pixels, rate; } pollikmark_raster[8][30];
static u32 frame_pixels, raster_pixels;
u32 pollikmark_intervals[4096];
static MarkResult live_result;
u32 pollikmark_running,pollikmark_test,pollikmark_level,pollikmark_completed;
static u32 all, opened, info, pollikmark_generation, progress_ms;
static int win_w=680,win_h=410;
static GfxTarget target;
static u32 *front;
static u32 surface_bytes, front_valid;
static u8 *mem_a,*mem_b;
static u32 memory_bytes, memory_offset;
static u32 phase, item, row, clear_offset, frame_units;
static u32 n, minimum, maximum, slow[41], work_us;
static u64 level_start, iteration_start, sum_us, units, measured_us, last_ui;
static AppPerfView baseline;
static const char *message="Ready - choose test 1..8; Enter runs all";
static Triangle frame_triangle;
static Mat4 frame_mvp;
static u32 frame_clock;

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
    for(int i=0;i<41;i++)slow[i]=0;
    phase=item=row=clear_offset=memory_offset=frame_units=progress_ms=0;
    level_start=iteration_start=app_host_time_us();
    app_host_metrics(&baseline);
}
static void sample(u32 interval) {
    if(!interval)interval=1;
    pollikmark_intervals[n]=interval;
    n++;sum_us+=interval;
    if(interval<minimum)minimum=interval;
    if(interval>maximum)maximum=interval;
    for(int i=0;i<41;i++) if(interval>slow[i]) {
        u32 swap=slow[i];slow[i]=interval;interval=swap;
    }
}
static void snapshot(MarkResult *r,u32 status) {
    r->status=status;r->n=n;r->min_us=n?minimum:0;r->max_us=maximum;
    r->mean_us=gfx_ratio(sum_us,n);r->fps=gfx_ratio((u64)n*1000000,(u32)sum_us);
    u32 k=(n+99)/100,total=0;
    for(u32 i=0;i<k && i<41;i++)total+=slow[i];
    r->low_fps=gfx_ratio((u64)k*1000000,total);
    r->work_us=(u32)measured_us;r->units=(u32)units;
    r->rate=gfx_ratio(units*1000000,(u32)measured_us);
    pollikmark_raster[pollikmark_test][pollikmark_level].pixels=raster_pixels;
    pollikmark_raster[pollikmark_test][pollikmark_level].rate=gfx_ratio((u64)raster_pixels*1000000,(u32)measured_us);
    if(pollikmark_test==6) {
        AppPerfView end;app_host_metrics(&end);
        u32 frames=end.frames-baseline.frames;
        r->n=frames;r->fps=gfx_ratio((u64)frames*1000000,(u32)(app_host_time_us()-level_start));
        r->paint_us=gfx_ratio(end.paint_us-baseline.paint_us,frames);
        r->compose_us=gfx_ratio(end.compose_us-baseline.compose_us,frames);
        r->present_us=gfx_ratio(end.present_us-baseline.present_us,frames);
        r->total_us=gfx_ratio(end.total_us-baseline.total_us,frames);
        r->rate=r->fps;r->low_fps=0;r->mean_us=r->min_us=r->max_us=0;
    }
}
static void stop(void) {
    if(pollikmark_running) snapshot(&pollikmark_results[pollikmark_test][pollikmark_level],2);
    pollikmark_running=all=0;release_memory();message="Stopped - completed raw levels retained";
    app_host_invalidate(APP_POLLIKMARK);
}
static void begin_frame(void);
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
    message="Running - Esc / Stop cancels; close releases RAM";return 1;
}
static void start(u32 test,int full) {
    stop();info=0;
    if(test==5) {message="Texture: NOT SUPPORTED (no texture capability)";return;}
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
        if(pollikmark_test==5)pollikmark_test++;
        if(pollikmark_test==8) {pollikmark_test=7;pollikmark_level=29;pollikmark_running=all=0;message="Full run complete (Texture unsupported)";return;}
    }
    /* Failed memory sizes are recorded, never dereferenced. End at first
     * exhausted size: larger contiguous allocations cannot be assumed. */
    if(!begin_level()) {
        pollikmark_running=all=0;
        message="Memory exhausted - available completed results retained";
    }
}
static void make_triangle(void) {
    u32 i=item;
    if(pollikmark_test==3) {
        static const u8 faces[36]={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
        static const signed char vertices[8][3]={{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
        for(int k=0;k<3;k++) {
            int j=faces[i*3+k];const signed char *v=vertices[j];
            frame_triangle.v[k]=(Vertex){soft3d_transform(frame_mvp,(Vec4){v[0]*0.65f,v[1]*0.65f,v[2]*0.65f,1}),
                {(j&1)?1:0.2f,(j&2)?1:0.2f,(j&4)?1:0.2f},{0,0}};
        }
    } else {
        float x=0,y=0,s=0.75f;
        if(pollikmark_test==4) {x=(int)(i*37%180)/100.0f-0.9f;y=(int)(i*53%180)/100.0f-0.9f;s=0.08f;}
        frame_triangle=(Triangle){{{{x-s,y-s,0,1},{1,0.2f,0.1f},{0,0}},
            {{x+s,y-s,0,1},{0.1f,1,0.2f},{0,0}},{{x,y+s,0,1},{0.2f,0.1f,1},{0,0}}}};
        if(pollikmark_test==2)for(int k=0;k<3;k++)
            frame_triangle.v[k].p=soft3d_transform(frame_mvp,frame_triangle.v[k].p);
    }
}
static void begin_frame(void) {
    iteration_start=app_host_time_us();work_us=frame_units=frame_pixels=0;
    item=row=clear_offset=memory_offset=0;phase=0;
    if(pollikmark_test==2 || pollikmark_test==3) {
        /* Rational rotation (sin=2t/(1+t*t), cos=(1-t*t)/(1+t*t)), no libm. */
        float t=(int)(frame_clock++%120)/60.0f-1,c=(1-t*t)/(1+t*t),s=2*t/(1+t*t);
        Mat4 model=soft3d_identity(),view=soft3d_identity();
        model.m[0]=c;model.m[2]=s;model.m[8]=-s;model.m[10]=c;
        view.m[7]=-0.1f;view.m[11]=-3;
        frame_mvp=soft3d_multiply(soft3d_perspective(1.7f,(float)target.width/target.height,0.1f,20),soft3d_multiply(view,model));
    }
}
static int graphics_step(void) {
    if(phase==0) {
        u32 amount=gfx_clear(&target,clear_offset,2048,0x182238);
        clear_offset+=amount;
        if(pollikmark_test==0)frame_units+=amount;
        if(clear_offset==target.capacity) {phase=1;if(pollikmark_test==0)return 1;}
        return 0;
    }
    if(pollikmark_test==1) {
        int x=(int)(item*37%target.width)-5,y=(int)(item*53%target.height)-3;
        gfx_shape(&target,x,y,7+item%17,7+item%11,0x6080a0+item*123,item%5);
        item++;frame_units++;
        return item==counts[pollikmark_level];
    }
    u32 limit=pollikmark_test==3?12:pollikmark_test==4?counts[pollikmark_level]:1;
    if(!row)make_triangle();
    /* Geometry triangles cover <=16x11 pixels at the cap, so a whole small
     * triangle is a bounded unit. Large triangle/cube use two-row slices. */
    if(pollikmark_test==4) {
        frame_pixels+=soft3d_triangle(&target,&frame_triangle,0,2,0,target.height);
        item++;frame_units++;return item==limit;
    }
    frame_pixels+=soft3d_triangle(&target,&frame_triangle,pollikmark_test==3,pollikmark_test==3?(int)pollikmark_level:2,row,row+2);
    row+=2;
    if(row>=(u32)target.height) {row=0;item++;frame_units++;}
    return item==limit;
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
int pollikmark_poll(void) {
    if(!opened||!pollikmark_running)return 0;
    u64 now=app_host_time_us(),deadline=now+2500;
    progress_ms=(u32)(now-level_start)/1000;
    if(pollikmark_test==6) {
        snapshot(&live_result,0);
        if(now-level_start>=1000000)advance(1);
        return 1; /* Real repaint demand, not synthetic fake windows. */
    }
    if(now-level_start>=5000000 && phase!=3) {advance(2);return 1;}
    if(pollikmark_test==7) {
        u64 before=app_host_time_us();int done=memory_step();u32 elapsed=(u32)(app_host_time_us()-before);
        if(phase==3) {
            if(done) {reset_samples();begin_frame();} return 1;
        }
        work_us+=elapsed;
        if(!done)goto progress;
    } else {
        int done=0;
        /* At most 16 two-row raster slices or 16 small rects per poll, also
         * bounded if the host has only the ~8.3ms PIT clock. */
        for(int cap=0;cap<16 && app_host_time_us()<deadline;cap++) {
            u64 before=app_host_time_us();done=graphics_step();
            work_us+=(u32)(app_host_time_us()-before);if(done)break;
        }
        if(!done)goto progress;
        u32 *swap=front;front=target.color;target.color=swap;front_valid=1;pollikmark_generation++;
    }
    now=app_host_time_us();sample((u32)(now-iteration_start));units+=frame_units;measured_us+=work_us;
    raster_pixels+=frame_pixels;
    live_result=pollikmark_results[pollikmark_test][pollikmark_level];snapshot(&live_result,0);
    if(now-level_start>=1000000 && n>=3)advance(1);
    else if(n==4096)advance(2);
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
    if(y>=42&&y<68) {
        if(x<120)start(0,1);else if(x<220)stop();else info=!info;
    } else if(x>=10&&x<178&&y>=80&&y<80+8*22)start((y-80)/22,0);
}
static void label(int x,int y,const char *s) {
    if(y<34 || y+18>win_h-1 || x<0 || x>=win_w-8)return;
    char b[100];int n=0,w=0;
    while(s[n] && n<99) {
        int advance=sys_get_glyph_advance((u8)s[n],1);
        if(w+advance>win_w-x-8)break;
        w+=advance;b[n]=s[n];n++;
    }
    b[n]=0;ui_bridge_text(x,y,b,0xd9e4f0,1);
}
static void value(int x,int y,const char *name,u32 v) {
    char b[16];label(x,y,name);number(b,v);label(x+145,y,b);
}
static u32 score(void) {
    u64 total=0;u32 count=0;
    for(int t=0;t<8;t++)for(int l=0;l<levels[t];l++) {
        MarkResult *r=&pollikmark_results[t][l];
        if(r->status!=1||(t==7&&l%6==5))continue;
        u32 ref=t==0?10000000:t==6?60:t==7?104857600:10000;
        total+=gfx_ratio((u64)r->rate*100,ref);count++;
    }
    return gfx_ratio(total,count);
}
void pollikmark_render(int width,int height,int active) {
    (void)active;
    ui_bridge_rect(1,34,width-2,height-35,0x202b40);
    label(12,44,"Run all [Enter]   Stop [Esc]   Info [I]");
    for(int i=0;i<8;i++) {
        if((u32)i==pollikmark_test)ui_bridge_rect(8,78+i*22,169,22,0x405574);
        char b[4]={(char)('1'+i),'.',0};label(12,80+i*22,b);label(34,80+i*22,i==5?"Texture: N/A":names[i]);
    }
    if(width<480||height<280)return;
    int x=190;
    if(info) {
        GfxInfo device;gfx_device_info(&device);AppPerfView m;app_host_metrics(&m);
        label(x,80,"Software CPU / no GPU acceleration");
        value(x,100,"Runtime VBE width",device.width);value(x,118,"Height / pixels",device.height);
        value(x,136,"Color bits",device.bpp);value(x,154,"Free RAM / KiB",app_host_free_bytes()/1024);
        value(x,172,"Clock step / us",m.clock_resolution_us);
        label(x,192,"Texture: NOT SUPPORTED");
        label(x,212,"1:1 capped viewport; x87 depth + RGB");
    } else {
        if(front_valid && height>=410 && pollikmark_test<6)
            app_host_blit(x+(width-x-target.width)/2,76,front,target.width,target.height,target.stride,target.capacity);
        MarkResult live=pollikmark_results[pollikmark_test][pollikmark_level];
        if(pollikmark_running)live=live_result;
        int y=height>=410?212:80;
        if(height<410)ui_bridge_rect(x,76,width-x-8,164,0x202b40);
        value(x,y,"Level (N/P browse)",pollikmark_level+1);
        value(x,y+18,"Samples",live.n);
        value(x,y+36,pollikmark_test==6?"Compositor FPS":"Iteration FPS",live.fps);
        value(x,y+54,"1% low FPS",live.low_fps);
        value(x,y+72,"Min / us",live.min_us);value(x,y+90,"Max / us",live.max_us);
        value(x,y+108,"Mean / us",live.mean_us);
        if(height>=410) {
            value(x+250,y,"Work / us",live.work_us);
            value(x+250,y+36,"Score (partial)",score());
            value(x+250,y+54,"Status 1=complete",live.status);
        }
        int tx=height>=410?x+250:x,ty=height>=410?y+18:y+126;
        value(tx,ty,pollikmark_test>=2&&pollikmark_test<=4?"Triangles/s":"Rate / second",live.rate);
        if(pollikmark_test>=2&&pollikmark_test<=4)
            value(tx,height>=410?y+72:y+144,"Raster pixels/s",pollikmark_raster[pollikmark_test][pollikmark_level].rate);
        if(height>=410 && pollikmark_test==6) {
            value(x,80,"Paint / us",live.paint_us);value(x,98,"Compose / us",live.compose_us);
            value(x,116,"Present / us",live.present_us);value(x,134,"Total / us",live.total_us);
            label(x,156,"Real client repaint demand; no fake window tests");
        }
        if(height>=410 && pollikmark_test==7) {
            label(x,80,ops[pollikmark_level%6]);value(x,98,"Size / MiB",mib[pollikmark_level/6]);
            value(x,116,"Alloc / us",live.alloc_us);
        }
        if(height>=410) {
            if(pollikmark_test==1 || pollikmark_test==4)value(12,260,"Objects",counts[pollikmark_level]);
            else if(pollikmark_test==3)label(12,260,pollikmark_level==0?"12 tri / wire":pollikmark_level==1?"12 tri / flat":"12 tri / vertex");
            value(x,y+126,"Elapsed / ms",progress_ms);
        }
    }
    /* All status rows remain inside full-window client bounds at minimum. */
    int footer=height-(height>=410?50:24);
    ui_bridge_rect(8,footer,width-16,height-footer-1,0x202b40);
    label(12,footer+2,message);
    if(pollikmark_running)ui_bridge_rect(12,footer-3,(width-24)*(int)(progress_ms>5000?5000:progress_ms)/5000,3,0x5edac9);
    if(height>=410)label(12,height-28,pollikmark_test==7?"Rate: bytes/s (read+write); allocation once/level":
        pollikmark_test==0?"Rate: pixels/s; FPS = completed workload, NOT presents":
        pollikmark_test==6?"Real repaint metrics; interval low/min/max not sampled":
        "Rate: shapes or submitted triangles/s; NOT presentation FPS");
}
