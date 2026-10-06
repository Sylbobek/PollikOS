#include "app_internal.h"
#include "../audio.h"
#include "../pmm.h"
#include "../auth.h"
int framebuffer_width(void);
int framebuffer_height(void);

enum {
    TAB_APPEARANCE = 0,
    TAB_DESKTOP_DOCK = 1,
    TAB_DISPLAY = 2,
    TAB_SOUND = 3,
    TAB_NETWORK = 4,
    TAB_GENERAL = 5,
    TAB_COUNT = 6
};

typedef struct {
    const char *name;
    u32 badge_color;
    char badge_char;
} SettingsTabInfo;

static const SettingsTabInfo TABS[TAB_COUNT] = {
    [TAB_APPEARANCE]   = {"Appearance",     0x2563eb, 'A'},
    [TAB_DESKTOP_DOCK] = {"Desktop", 0x06b6d4, 'D'},
    [TAB_DISPLAY]      = {"Display",        0xf59e0b, 'S'},
    [TAB_SOUND]        = {"Sound",          0xf43f5e, 'V'},
    [TAB_NETWORK]      = {"Network",        0x10b981, 'N'},
    [TAB_GENERAL]      = {"System",        0x64748b, 'i'},
};

static const u32 ACCENT_COLORS[5] = {
    0x2563eb, /* Royal Blue */
    0x7c3aed, /* Violet */
    0x059669, /* Emerald */
    0xd97706, /* Amber */
    0xe11d48  /* Rose */
};

static const char *ACCENT_NAMES[5] = {
    "Blue", "Violet", "Green", "Amber", "Rose"
};

static int g_settings_tab = TAB_APPEARANCE;
static int g_sound_played = 0;
static const char *g_last_net_status = 0;
static int slider_capture=-1,slider_changed;

/* Layout computation helper */
typedef struct {
    AppRect sidebar;
    AppRect tabs[TAB_COUNT];
    AppRect content;
    int dark;
    u32 bg_card, border_card, text_head, text_body, text_muted;
} SettingsLayout;

static SettingsLayout settings_calc_layout(int width, int height) {
    SettingsLayout l;
    l.dark = ui_is_dark();
    l.bg_card = l.dark ? 0x1c212e : 0xffffff;
    l.border_card = l.dark ? 0x2d3748 : 0xe4dff0;
    l.text_head = l.dark ? 0xf8fafc : 0x221a30;
    l.text_body = l.dark ? 0xe2e8f0 : 0x483d5a;
    l.text_muted = l.dark ? 0x94a3b8 : 0x7c718c;

    int sb_w = 172;
    l.sidebar = (AppRect){0, 34, sb_w, height - 34};
    int tab_y = 80;
    for (int i = 0; i < TAB_COUNT; i++) {
        l.tabs[i] = (AppRect){8, tab_y + i * 36, sb_w - 16, 32};
    }
    l.content = (AppRect){sb_w + 16, 44, width - sb_w - 32, height - 56};
    return l;
}

static void draw_card(AppRect r, u32 bg, u32 border, int radius) {
    (void)border;
    roundrect(r.x, r.y, r.w, r.h, radius, bg);
}
static AppRect slider_rect(SettingsLayout l,int kind) {
    return (AppRect){l.content.x+24,kind?490:222,l.content.w-48,24};
}
static void draw_slider(SettingsLayout l,int kind,int value,int maximum) {
    AppRect r=slider_rect(l,kind);
    int position=(r.w-1)*value/maximum;
    roundrect(r.x,r.y+9,r.w,6,3,l.dark?0x414555:0xdcd8e5);
    roundrect(r.x,r.y+9,position+1,6,3,app_host_accent_color());
    roundrect(r.x+position-7,r.y+4,14,16,7,l.dark?0xe9e7f1:0xffffff);
    roundrect_border(r.x+position-7,r.y+4,14,16,7,1,l.dark?0xaaa6b9:0xbcb5cd);
}
static int slider_update(SettingsLayout l,int x) {
    AppRect r=slider_rect(l,slider_capture);
    int offset=x-r.x;if(offset<0)offset=0;if(offset>=r.w)offset=r.w-1;
    int changed;
    if(slider_capture==0) {
        int value=(offset*100+(r.w-1)/2)/(r.w-1);
        changed=value!=audio_get_volume();if(changed)audio_set_volume((u8)value);
    } else {
        const int sizes[4]={100,125,150,200};
        int index=(offset*3+(r.w-1)/2)/(r.w-1);
        changed=sizes[index]!=app_host_cursor_size();
        if(changed)app_host_preview_cursor_size(sizes[index]);
    }
    if(changed){slider_changed=1;app_host_invalidate_partial(APP_SETTINGS);}
    return changed;
}
void settings_close(void) {
    if(slider_changed)app_host_save_settings();
    slider_capture=-1;slider_changed=0;
}
int settings_drag(int x,int y,int active) {
    GuiAppSize s=gui_app_size(APP_SETTINGS);
    SettingsLayout l=settings_calc_layout(s.width,s.height);
    if(!active){int captured=slider_capture>=0;settings_close();return captured?0:-1;}
    if(slider_capture<0) {
        int kind=g_settings_tab==TAB_SOUND?0:g_settings_tab==TAB_DESKTOP_DOCK?1:-1;
        if(kind<0||!app_hit(slider_rect(l,kind),x,y))return -1;
        slider_capture=kind;
    }
    return slider_update(l,x);
}

int settings_poll(void) {
    if (g_settings_tab == TAB_NETWORK) {
        if (net_status != g_last_net_status) {
            g_last_net_status = net_status;
            return 1;
        }
    }
    return 0;
}

static void setting_text(int x,int y,int width,const char *label,u32 color) {app_label(x,y,width,label,color,2);}
static void setting_button(int x,int y,int w,int h,const char *label,int selected,SettingsLayout l) {
    roundrect(x,y,w,h,8,selected?app_host_accent_color():l.dark?0x303447:0xe8e4ef);
    centered(x,y+(h-20)/2,w,label,selected?0xffffff:l.text_body,2);
}
void settings_render(int width,int height,int active) {
    (void)active;SettingsLayout l=settings_calc_layout(width,height);
    int cx=l.content.x,cw=l.content.w,dark=l.dark;u32 acc=app_host_accent_color();
    rect(0,34,l.sidebar.w,height-34,dark?0x131620:0xf2eef7);
    rect(l.sidebar.w,34,1,height-34,l.border_card);
    setting_text(16,47,l.sidebar.w-24,"Settings",l.text_head);
    for(int i=0;i<TAB_COUNT;i++) {
        AppRect r=l.tabs[i];if(i==g_settings_tab)roundrect(r.x,r.y,r.w,r.h,8,acc);
        roundrect(r.x+6,r.y+6,20,20,5,TABS[i].badge_color);
        char badge[]={TABS[i].badge_char,0};centered(r.x+6,r.y+9,20,badge,0xffffff,1);
        setting_text(r.x+32,r.y+6,r.w-36,TABS[i].name,i==g_settings_tab?0xffffff:l.text_body);
    }
    setting_text(cx,44,cw,TABS[g_settings_tab].name,l.text_head);
    if(g_settings_tab==TAB_APPEARANCE) {
        draw_card((AppRect){cx,90,cw,106},l.bg_card,l.border_card,12);
        setting_text(cx+14,96,cw-28,"Theme",l.text_head);
        for(int i=0;i<2;i++) {
            int x=cx+24+i*114,y=118,selected=dark==i;
            if(selected)roundrect(x-3,y-3,102,62,8,acc);
            roundrect(x,y,96,56,6,i?0x171b28:0xf8fafc);
            rect(x+8,y+8,80,6,i?0x363b50:0xdedbea);
            centered(x,y+32,96,i?"Dark":"Light",i?0xffffff:0x202331,2);
        }
        draw_card((AppRect){cx,206,cw,106},l.bg_card,l.border_card,12);
        setting_text(cx+14,214,cw-28,"Accent color",l.text_head);
        for(int i=0;i<5;i++) {
            int x=cx+18+i*38;if(i==app_host_accent())roundrect(x-3,245,32,32,16,l.text_head);
            roundrect(x,248,26,26,13,ACCENT_COLORS[i]);
        }
        setting_text(cx+14,286,cw-28,ACCENT_NAMES[app_host_accent()],l.text_body);
        draw_card((AppRect){cx,326,cw,90},l.bg_card,l.border_card,12);
        setting_text(cx+14,338,cw-28,"Wallpaper follows the theme",l.text_head);
        setting_text(cx+14,372,cw-28,dark?"Dark wallpaper":"Light wallpaper",l.text_body);
    } else if(g_settings_tab==TAB_DESKTOP_DOCK) {
        draw_card((AppRect){cx,90,cw,80},l.bg_card,l.border_card,12);
        setting_text(cx+14,98,cw-28,"Window animations",l.text_head);
        setting_button(cx+14,134,130,26,"On",app_host_animations(),l);
        setting_button(cx+152,134,110,26,"Off",!app_host_animations(),l);
        draw_card((AppRect){cx,180,cw,80},l.bg_card,l.border_card,12);
        setting_text(cx+14,188,cw-28,"Dock magnification",l.text_head);
        setting_button(cx+14,224,130,26,app_host_dock_zoom()?"On":"Off",app_host_dock_zoom(),l);
        draw_card((AppRect){cx,270,cw,78},l.bg_card,l.border_card,12);
        setting_text(cx+14,278,cw-28,"Desktop grid",l.text_head);
        setting_text(cx+14,310,cw-28,"Snap icons into place",l.text_body);
        draw_card((AppRect){cx,358,cw,58},l.bg_card,l.border_card,12);
        setting_text(cx+14,362,cw-28,"Pointer acceleration",l.text_head);
        setting_button(cx+14,384,146,24,app_host_pointer_acceleration()?"On":"Off",app_host_pointer_acceleration(),l);
        draw_card((AppRect){cx,426,cw,88},l.bg_card,l.border_card,12);
        setting_text(cx+14,430,cw-28,"Cursor size",l.text_head);
        const int sizes[]={100,125,150,200};const char *labels[]={"100%","125%","150%","200%"};
        for(int i=0;i<4;i++)setting_button(cx+14+i*72,456,64,26,labels[i],app_host_cursor_size()==sizes[i],l);
        int size=app_host_cursor_size();draw_slider(l,1,size==100?0:size==125?1:size==150?2:3,3);
    } else if(g_settings_tab==TAB_DISPLAY) {
        draw_card((AppRect){cx,90,cw,96},l.bg_card,l.border_card,12);
        setting_text(cx+14,98,cw-28,"Display resolution",l.text_head);
        char mode[48],number_text[16];number(mode,(u32)framebuffer_width());append_str(mode," x ",sizeof mode);
        number(number_text,(u32)framebuffer_height());append_str(mode,number_text,sizeof mode);
        setting_text(cx+14,138,cw-28,mode,l.text_body);
        draw_card((AppRect){cx,206,cw,88},l.bg_card,l.border_card,12);
        setting_text(cx+14,214,cw-28,"Presentation target",l.text_head);
        setting_button(cx+14,252,135,26,"120 Hz",app_host_target_fps()==120,l);
        setting_button(cx+156,252,125,26,"60 Hz",app_host_target_fps()==60,l);
    } else if(g_settings_tab==TAB_SOUND) {
        draw_card((AppRect){cx,90,cw,80},l.bg_card,l.border_card,12);
        setting_text(cx+14,98,cw-28,"Sound output",l.text_head);
        setting_button(cx+14,134,140,26,"Speaker",audio_output()==0,l);
        if(audio_is_available())setting_button(cx+166,134,140,26,"AC'97",audio_output()==1,l);
        draw_card((AppRect){cx,180,cw,86},l.bg_card,l.border_card,12);
        setting_text(cx+14,188,cw-28,"Volume",l.text_head);
        char vol[16];number(vol,audio_get_volume());append_str(vol,"%",sizeof vol);
        setting_text(cx+cw-72,188,58,vol,l.text_body);draw_slider(l,0,audio_get_volume(),100);
        draw_card((AppRect){cx,276,cw,78},l.bg_card,l.border_card,12);
        setting_button(cx+14,306,95,30,app_host_sound_muted()?"Muted":"On",app_host_sound_muted(),l);
        setting_button(cx+115,306,120,30,"Chime",0,l);
        setting_button(cx+242,306,110,30,"Trash",0,l);
    } else if(g_settings_tab==TAB_NETWORK) {
        draw_card((AppRect){cx,90,cw,102},l.bg_card,l.border_card,12);
        setting_text(cx+14,98,cw-28,"Ethernet",l.text_head);
        setting_text(cx+14,138,cw-28,net_ready?"Connected":"Disconnected",l.text_body);
        draw_card((AppRect){cx,206,cw,66},l.bg_card,l.border_card,12);
        setting_text(cx+14,218,cw-28,"Wi-Fi: no supported adapter",l.text_body);
        draw_card((AppRect){cx,282,cw,72},l.bg_card,l.border_card,12);
        setting_button(cx+14,310,150,30,"Ping",0,l);
    } else {
        draw_card((AppRect){cx,90,cw,92},l.bg_card,l.border_card,12);
        setting_text(cx+14,98,cw-28,"PollikOS",l.text_head);
        setting_text(cx+14,138,cw-28,"i386 desktop",l.text_body);
        draw_card((AppRect){cx,206,cw,80},l.bg_card,l.border_card,12);
        setting_text(cx+14,216,cw-28,"Storage",l.text_head);
        setting_text(cx+14,250,cw-28,"PollikFS v2",l.text_body);
        setting_button(cx+14,336,110,30,"Restart",0,l);
        setting_button(cx+134,336,120,30,"Shut down",0,l);
        setting_button(cx+264,336,110,30,"Lock",1,l);
    }
}

void settings_click(int x, int y) {
    GuiAppSize s = gui_app_size(APP_SETTINGS);
    SettingsLayout l = settings_calc_layout(s.width, s.height);
    if(settings_drag(x,y,1)>=0)return;

    /* 1. Sidebar clicks */
    for (int i = 0; i < TAB_COUNT; i++) {
        if (app_hit(l.tabs[i], x, y)) {
            g_settings_tab = i;
            app_host_invalidate(APP_SETTINGS);
            return;
        }
    }

    int cx = l.content.x;

    /* 2. Tab content clicks */
    if (g_settings_tab == TAB_APPEARANCE) {
        /* Light tile: x: cx + 24, y: 118, w: 96, h: 56 */
        if (x >= cx + 24 && x <= cx + 120 && y >= 118 && y <= 174) {
            app_host_set_theme(1);
            app_host_invalidate(APP_SETTINGS);
        }
        /* Dark tile: x: cx + 138, y: 118, w: 96, h: 56 */
        else if (x >= cx + 138 && x <= cx + 234 && y >= 118 && y <= 174) {
            app_host_set_theme(0);
            app_host_invalidate(APP_SETTINGS);
        }
        /* Accent colors: 5 circles at y: 248..274 */
        for (int a = 0; a < 5; a++) {
            int ax = cx + 18 + a * 38;
            if (x >= ax && x <= ax + 26 && y >= 248 && y <= 274) {
                app_host_set_accent(a);
                app_host_invalidate(APP_SETTINGS);
                break;
            }
        }
    } else if (g_settings_tab == TAB_DESKTOP_DOCK) {
        /* Smooth animations: cx + 14, 134, 130, 26 */
        if (x >= cx + 14 && x <= cx + 144 && y >= 134 && y <= 160) {
            app_host_set_animations(1);
            app_host_invalidate(APP_SETTINGS);
        }
        /* Fast animations: cx + 152, 134, 110, 26 */
        else if (x >= cx + 152 && x <= cx + 262 && y >= 134 && y <= 160) {
            app_host_set_animations(0);
            app_host_stop_minimize();
            app_host_invalidate(APP_SETTINGS);
        }
        /* Dock zoom: cx + 14, 224, 130, 26 */
        else if (x >= cx + 14 && x <= cx + 144 && y >= 224 && y <= 250) {
            app_host_set_dock_zoom(!app_host_dock_zoom());
            app_host_invalidate(APP_SETTINGS);
        }
        if (x >= cx + 14 && x <= cx + 160 && y >= 384 && y <= 408) {
            app_host_set_pointer_acceleration(!app_host_pointer_acceleration());
            app_host_invalidate(APP_SETTINGS);
        }
        if (y >= 456 && y < 482) {
            const int cursor_sizes[4] = {100, 125, 150, 200};
            for (int i = 0; i < 4; ++i) {
                int bx = cx + 14 + i * 72;
                if (x >= bx && x < bx + 64) {
                    app_host_set_cursor_size(cursor_sizes[i]);
                    app_host_invalidate(APP_SETTINGS);
                }
            }
        }
    } else if (g_settings_tab == TAB_DISPLAY) {
        /* 120 Hz: cx + 14, 252, 135, 26 */
        if (x >= cx + 14 && x <= cx + 149 && y >= 252 && y <= 278) {
            app_host_set_target_fps(120);
            app_host_invalidate(APP_SETTINGS);
        }
        /* 60 Hz: cx + 156, 252, 125, 26 */
        else if (x >= cx + 156 && x <= cx + 281 && y >= 252 && y <= 278) {
            app_host_set_target_fps(60);
            app_host_invalidate(APP_SETTINGS);
        }
    } else if (g_settings_tab == TAB_SOUND) {
        if(y>=134&&y<160&&x>=cx+14&&x<cx+306) {
            if(audio_select_output(x>=cx+166)){app_host_save_settings();app_host_invalidate_partial(APP_SETTINGS);}
            return;
        }
        /* Sound Mute Toggle: cx + 14, 306, 95, 30 */
        if (x >= cx + 14 && x <= cx + 109 && y >= 306 && y <= 336) {
            app_host_set_sound_muted(!app_host_sound_muted());
            if (app_host_sound_muted()) audio_set_volume(0);
            else audio_set_volume(85);
            app_host_invalidate(APP_SETTINGS);
        }
        /* Play Chime button: cx + 115, 306, 120, 30 */
        else if (x >= cx + 115 && x <= cx + 235 && y >= 306 && y <= 336) {
            audio_play_sound(SOUND_STARTUP);
            g_sound_played = 1;
            app_host_invalidate(APP_SETTINGS);
        }
        /* Play Trash Sound button: cx + 242, 306, 110, 30 */
        else if (x >= cx + 242 && x <= cx + 352 && y >= 306 && y <= 336) {
            audio_play_sound(SOUND_TRASH);
            g_sound_played = 1;
            app_host_invalidate(APP_SETTINGS);
        }
    } else if (g_settings_tab == TAB_NETWORK) {
        /* Run Ping Test: cx + 14, 310, 150, 30 */
        if (x >= cx + 14 && x <= cx + 164 && y >= 310 && y <= 340) {
            net_ping();
            app_host_invalidate(APP_SETTINGS);
        }
    } else if (g_settings_tab == TAB_GENERAL) {
        /* Restart: cx + 14, 336, 110, 30 */
        if (x >= cx + 14 && x <= cx + 124 && y >= 336 && y <= 366) {
            app_host_power(1);
        }
        /* Shut Down: cx + 134, 336, 120, 30 */
        else if (x >= cx + 134 && x <= cx + 254 && y >= 336 && y <= 366) {
            app_host_power(0);
        }
        else if (x >= cx + 264 && x <= cx + 374 && y >= 336 && y <= 366) {
            auth_lock();
            app_host_invalidate(APP_SETTINGS);
        }
    }
}
