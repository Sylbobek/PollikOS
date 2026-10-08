#include "system.h"
#include "ui_icons.h"
#include "graphics.h"
#include "media.h"
#include "vfs.h"
#include "mem.h"
#include "ui.h"
#include "gui/apps.h"
static u8 *icon_pixels[UI_ICON_COUNT];
static int initialized;
static const char *names[]={"welcome","files","terminal","notes","settings","browser",
    "pollikmark","calculator","folder","folder-blue","file","trash"};
void ui_icons_init(void) {
    if(initialized)return;initialized=1;unsigned loaded=0;
    for(int i=0;i<UI_ICON_COUNT;i++) {
        char path[80]="/usr/share/icons/";int n=17,j=0;
        while(names[i][j])path[n++]=names[i][j++];
        path[n++]='.';path[n++]='p';path[n++]='n';path[n++]='g';path[n]=0;
        vfs_stat_t st;if(vfs_stat(path,&st)!=VFS_OK||st.type!=VFS_FILE||st.size<33||st.size>65536)continue;
        int fd=vfs_open(path,O_RDONLY);if(fd<0)continue;
        u8 *encoded=kmalloc(st.size);if(!encoded){vfs_close(fd);continue;}
        int got=vfs_read(fd,encoded,st.size);vfs_close(fd);
        static const u8 sig[]={137,80,78,71,13,10,26,10};
        int w=0,h=0;
        /* Bound decoder allocation before touching a potentially corrupt PNG. */
        if(got==(int)st.size&&!memcmp(encoded,sig,8)&&!memcmp(encoded+12,"IHDR",4)&&
           encoded[16]==0&&encoded[17]==0&&encoded[18]==0&&encoded[19]==64&&
           encoded[20]==0&&encoded[21]==0&&encoded[22]==0&&encoded[23]==64)
            icon_pixels[i]=media_decode(encoded,st.size,&w,&h);
        kfree(encoded);
        if(icon_pixels[i]&&(w!=64||h!=64)){media_free(icon_pixels[i]);icon_pixels[i]=0;}
        if(icon_pixels[i])loaded++;
    }
    char value[16];serial("[ICONS] PollikFS loaded=");number(value,loaded);serial(value);
    serial(" fallback=");number(value,UI_ICON_COUNT-loaded);serial(value);serial("\n");
}
void ui_icon_draw(int id,int x,int y,int size) {
    if(size<=0||id<0||id>=UI_ICON_COUNT)return;
    if(icon_pixels[id]){graphics_blit_rgba(x,y,size,size,icon_pixels[id],64,64);return;}
    /* Small procedural fallback; login/installer still work without a data disk. */
    int pad=size/16,side=size-pad*2;
    roundrect(x+pad,y+pad,side,side,side/4,0x777089);
    centered(x,y+(size-13)/2,size,"?",0xffffff,1);
}
void ui_app_icon_draw(int id,int x,int y,int size) {
    if(id<APP_PHOTOS){ui_icon_draw(id,x,y,size);return;}
    ThemeColors *t=ui_theme();
    if(id==APP_PHOTOS)ui_draw_icon(ICON_IMAGE,x,y,size,t->accent,t->text);
    else if(id==APP_DOCUMENTS)ui_draw_icon(ICON_TEXT,x,y,size,t->accent,t->text);
    else if(id==APP_VIDEO){
        roundrect(x+size/16,y+size/16,size-size/8,size-size/8,size/4,t->accent);
        ui_draw_icon(ICON_CHEVRON_RIGHT,x+size/4,y+size/4,size/2,0xffffff,0xffffff);
    }
}
