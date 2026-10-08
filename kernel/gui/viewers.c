#include "app_internal.h"
#include "../vfs.h"
#include "../media.h"
#include "../ui.h"

typedef struct {
    u8 *image,*raw;
    MediaGif *gif;
    MediaClip *clip;
    int width,height,playing;
    char name[64];
} MediaViewer;
static MediaViewer photo_view,video_view;
static char *document_data;
static u32 document_size,document_lines,document_first;
static int document_binary;
static char document_name[64];

static void viewer_release(MediaViewer *v){
    if(v->image)media_free(v->image);
    if(v->gif)media_gif_close(v->gif);
    if(v->clip)media_clip_close(v->clip);
    if(v->raw)kfree(v->raw);
    memset(v,0,sizeof(*v));
}
void photos_close(void){viewer_release(&photo_view);}
void video_close(void){viewer_release(&video_view);}
void documents_close(void){
    if(document_data)kfree(document_data);
    document_data=0;document_size=document_lines=document_first=0;
    document_name[0]=0;document_binary=0;
}
static void viewer_name(char out[64],const char *name){
    unsigned n=0;while(name&&name[n]&&n<63){out[n]=name[n];n++;}out[n]=0;
}
static u8 *viewer_read(const char *path,u32 limit,u32 *length){
    vfs_stat_t st;
    if(vfs_stat(path,&st)<0||st.type!=VFS_FILE||st.size>limit)return 0;
    int fd=vfs_open(path,O_RDONLY);if(fd<0)return 0;
    u8 *data=kmalloc(st.size+1);if(!data){vfs_close(fd);return 0;}
    int got=vfs_read(fd,data,st.size),closed=vfs_close(fd);
    if(got!=(int)st.size||closed<0){kfree(data);return 0;}
    data[st.size]=0;*length=st.size;return data;
}
static int viewer_extension(const char *name,const char *ext){
    int n=len(name),m=len(ext);if(n<m)return 0;
    for(int i=0;i<m;i++){char c=name[n-m+i];if(c>='A'&&c<='Z')c+=32;if(c!=ext[i])return 0;}return 1;
}
static int viewer_is_photo(const char *name){
    const char *ext[]={".png",".jpg",".jpeg",".bmp",".gif",".tga",".psd",".pnm",".ppm"};
    for(unsigned i=0;i<sizeof(ext)/sizeof(ext[0]);i++)if(viewer_extension(name,ext[i]))return 1;return 0;
}
int viewers_open_path(const char *path,const char *name){
#ifdef POLLIK_INSTALL_MEDIA
    (void)path;(void)name;
    ui_notify("Files","Media and document viewers are available after installation",ICON_INFO);return 0;
#else
    if(!gui_app_get(APP_DOCUMENTS)){ui_notify("Files","Viewers are available after installation",ICON_INFO);return 0;}
    if(!path||!name)return 0;
    int video=viewer_extension(name,".pkv"),photo=viewer_is_photo(name);
    u32 size=0;u8 *raw=viewer_read(path,video||photo?48u*1024u*1024u:128u*1024u,&size);
    if(!raw){ui_notify("Open file","File is unreadable or exceeds the viewer's size limit",ICON_WARNING);return 0;}
    if(video||photo){
        MediaViewer next;memset(&next,0,sizeof(next));next.raw=raw;
        if(video){
            next.clip=media_clip_open(raw,size);
            if(next.clip){next.width=media_clip_width(next.clip);next.height=media_clip_height(next.clip);}
        }else{
            next.gif=media_gif_open(raw,size);
            if(next.gif){next.width=media_gif_width(next.gif);next.height=media_gif_height(next.gif);}
            else{next.image=media_decode(raw,size,&next.width,&next.height);kfree(next.raw);next.raw=0;}
        }
        if(next.width<=0||next.height<=0||(!next.clip&&!next.gif&&!next.image)){
            viewer_release(&next);ui_notify("Open media","Unsupported or damaged image/video",ICON_WARNING);return 0;
        }
        next.playing=1;viewer_name(next.name,name);
        MediaViewer *v=video?&video_view:&photo_view;viewer_release(v);*v=next;
        int id=video?APP_VIDEO:APP_PHOTOS;app_host_open(id);app_host_invalidate(id);return 1;
    }
    documents_close();document_data=(char *)raw;document_size=size;document_lines=1;
    for(u32 i=0;i<size;i++){
        u8 c=raw[i];if(c=='\n')document_lines++;
        if(c==0||(c<32&&c!='\r'&&c!='\n'&&c!='\t'))document_binary=1;
    }
    if(document_binary)document_lines=(size+7)/8;
    viewer_name(document_name,name);app_host_open(APP_DOCUMENTS);app_host_invalidate(APP_DOCUMENTS);return 1;
#endif
}
static AppRect viewer_frame(MediaViewer *v,int width,int height){
    int aw=width-48,ah=height-154;
    int scale=aw*1000/v->width,sy=ah*1000/v->height;
    if(sy<scale)scale=sy;if(scale>1000)scale=1000;if(scale<1)scale=1;
    int w=v->width*scale/1000,h=v->height*scale/1000;
    if(w<1)w=1;if(h<1)h=1;
    return (AppRect){(width-w)/2,100+(ah-h)/2,w,h};
}
static void viewer_render(MediaViewer *v,int width,int height,int video){
    ThemeColors *t=ui_theme();
    roundrect(12,50,width-24,height-66,18,t->surface);
    if(!v->width){
        app_label(32,74,width-64,video?"Open a .pkv video from Files":"Open an image from Files",t->text,2);return;
    }
    app_label(30,65,width-150,v->name,t->text,2);
    if(v->gif||v->clip){
        roundrect(width-116,60,84,30,11,t->surface_secondary);
        centered(width-116,68,84,v->playing?"Pause":"Play",t->text,1);
    }
    const u8 *pixels=v->clip?media_clip_canvas(v->clip,v->playing):v->gif?media_gif_canvas(v->gif,v->playing):v->image;
    AppRect frame=viewer_frame(v,width,height);
    if(pixels)ui_bridge_blit_rgba(frame.x,frame.y,frame.w,frame.h,pixels,v->width,v->height);
    char info[80],n[16];copy(info,video?"Pollik Video   ":"Image   ");
    number(n,v->width);append_str(info,n,sizeof(info));append_str(info," x ",sizeof(info));
    number(n,v->height);append_str(info,n,sizeof(info));
    if(v->gif||v->clip)append_str(info,v->playing?"   Playing":"   Paused",sizeof(info));
    app_label(30,height-40,width-60,info,t->text_secondary,1);
}
void photos_render(int w,int h,int active){(void)active;viewer_render(&photo_view,w,h,0);}
void video_render(int w,int h,int active){(void)active;viewer_render(&video_view,w,h,1);}
static void viewer_toggle(MediaViewer *v,int id){
    if(!v->gif&&!v->clip)return;v->playing=!v->playing;app_host_invalidate(id);
}
void photos_key(u8 code,char ch,int shift,int control){(void)ch;(void)shift;(void)control;if(code==57)viewer_toggle(&photo_view,APP_PHOTOS);}
void video_key(u8 code,char ch,int shift,int control){(void)ch;(void)shift;(void)control;if(code==57)viewer_toggle(&video_view,APP_VIDEO);}
void photos_click(int x,int y){GuiAppSize s=gui_app_size(APP_PHOTOS);if(x>=s.width-116&&x<s.width-32&&y>=60&&y<90)viewer_toggle(&photo_view,APP_PHOTOS);}
void video_click(int x,int y){GuiAppSize s=gui_app_size(APP_VIDEO);if(x>=s.width-116&&x<s.width-32&&y>=60&&y<90)viewer_toggle(&video_view,APP_VIDEO);}
static int viewer_poll(MediaViewer *v,int id){
    if(!v->playing||(!v->gif&&!v->clip))return 0;
    if((v->gif&&media_gif_due(v->gif))||(v->clip&&media_clip_due(v->clip))){
        GuiAppSize s=gui_app_size(id);AppRect frame=viewer_frame(v,s.width,s.height);
        app_host_invalidate_partial_region(id,frame.x,frame.y,frame.w,frame.h);
    }return 0;
}
int photos_poll(void){return viewer_poll(&photo_view,APP_PHOTOS);}
int video_poll(void){return viewer_poll(&video_view,APP_VIDEO);}
void documents_scroll(int delta){
    GuiAppSize s=gui_app_size(APP_DOCUMENTS);u32 rows=(s.height-168)/20;if(!rows)rows=1;
    u32 max=document_lines>rows?document_lines-rows:0;
    if(document_first>max)document_first=max;
    if(delta<0){u32 n=(u32)(-delta);document_first=n>document_first?0:document_first-n;}
    else{u32 n=(u32)delta;document_first=n>max-document_first?max:document_first+n;}
    app_host_invalidate(APP_DOCUMENTS);
}
void documents_key(u8 code,char ch,int shift,int control){
    (void)ch;(void)shift;(void)control;
    if(code==72)documents_scroll(-1);else if(code==80)documents_scroll(1);
    else if(code==73)documents_scroll(-10);else if(code==81)documents_scroll(10);
    else if(code==71)documents_scroll(-(int)document_first);else if(code==79)documents_scroll((int)document_lines);
}
void documents_render(int width,int height,int active){
    (void)active;ThemeColors *t=ui_theme();
    roundrect(12,50,width-24,height-66,18,t->surface);
    app_label(30,66,width-60,document_data?document_name:"Open a document from Files",t->text,2);
    if(!document_data)return;
    u32 rows=(height-168)/20,max=document_lines>rows?document_lines-rows:0;
    if(document_first>max)document_first=max;
    app_label(30,94,width-60,document_binary?"Binary file - hexadecimal preview":"Text document - read only",t->text_secondary,1);
    u32 offset=0,line=0;
    if(!document_binary)while(offset<document_size&&line<document_first)if(document_data[offset++]=='\n')line++;
    for(int y=124;y+20<=height-44;y+=20){
        char text_line[256];unsigned n=0;
        if(document_binary){
            offset=(document_first+(u32)((y-124)/20))*8;if(offset>=document_size)break;
            static const char hex[]="0123456789abcdef";
            for(int shift=28;shift>=0;shift-=4)text_line[n++]=hex[(offset>>shift)&15];text_line[n++]=' ';text_line[n++]=' ';
            for(u32 i=0;i<8&&offset+i<document_size;i++){u8 c=(u8)document_data[offset+i];text_line[n++]=hex[c>>4];text_line[n++]=hex[c&15];text_line[n++]=' ';}
        }else{
            if(offset>=document_size)break;
            while(offset<document_size&&document_data[offset]!='\n'){
                u8 c=(u8)document_data[offset++];if(c=='\r')continue;
                if(c=='\t'){for(int j=0;j<4&&n<255;j++)text_line[n++]=' ';}
                else if(n<255)text_line[n++]=(char)c;
            }
            if(offset<document_size)offset++;
        }
        text_line[n]=0;app_label(30,y,width-60,text_line,t->text,1);
    }
    char status[64],n[16];number(status,document_size);append_str(status," bytes   Line ",sizeof(status));
    number(n,document_first+1);append_str(status,n,sizeof(status));append_str(status," / ",sizeof(status));
    number(n,document_lines);append_str(status,n,sizeof(status));app_label(30,height-38,width-60,status,t->text_secondary,1);
}
