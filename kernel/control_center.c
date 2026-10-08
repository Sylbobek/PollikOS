#include "control_center.h"
#include "gui/app_host.h"
#include "gui/apps.h"
#include "ui.h"
#include "graphics.h"
#include "audio.h"
#include "net/net_manager.h"
#include "wm.h"
#include "ui_animation.h"
#include "auth.h"
#include "ui_icons.h"
#include "../common/app_search.h"
void request_partial_redraw(int x,int y,int w,int h);
/* Independent overlay state; no change to window, menu or syscall layouts. */
static int cc_shown,cc_opening,cc_animating,cc_position=-520,cc_start_position=-520;
static int cc_capture=-1,cc_settings_changed,cc_list;
static u32 cc_started;
static int cc_detail_kind,cc_detail_slide,cc_detail_origin,cc_search_focus,cc_selected;
static u32 cc_detail_started;
static AppSearchInput cc_input;
#define cc_query cc_input.text
static unsigned cc_pins;
static int cc_context=-1,cc_hover=-1,cc_hot=-1,cc_blink;
static u32 cc_edit_started;
enum { WIDTH=400,HEIGHT=448,DURATION=240,DETAIL_DURATION=180,
       SEARCH_WIDTH=220,SEARCH_HEIGHT=452,DETAIL_WIDTH=248,DETAIL_GAP=16 };
static int left(void){
    int screen=ui_bridge_screen_width();
    int x=(screen-WIDTH-SEARCH_WIDTH-12)/2+SEARCH_WIDTH+12;
    int limit=screen-WIDTH-DETAIL_WIDTH-DETAIL_GAP-20;
    /* Keep the launcher visible whenever all three panels fit. */
    if(limit>=SEARCH_WIDTH+28 && x>limit)x=limit;
    return x;
}
static int search_left(void){return left()-SEARCH_WIDTH-12;}
static int top(void){return 38+cc_position;}
static int detail_width(void){int w=ui_bridge_screen_width()-left()-WIDTH-DETAIL_GAP-12;return w<DETAIL_WIDTH?w:DETAIL_WIDTH;}
static int detail_height(int kind){return kind==3?204:kind==4?184:176;}
static int detail_top(int kind){return kind==3?228:kind==4?248:18;}
static int detail_left(void){return left()+WIDTH+DETAIL_GAP+(256-cc_detail_slide)*12/256;}
static void damage(void){
    int x=search_left()-8;
    int right=left()+WIDTH+DETAIL_GAP+detail_width()+16;
    if(right>ui_bridge_screen_width())right=ui_bridge_screen_width();
    request_partial_redraw(x,32,right-x,HEIGHT+48);
}
static void detail_set(int kind){
    cc_list=kind;cc_detail_origin=cc_detail_slide;cc_detail_started=wm_time_ms();cc_hot=-1;
    if(kind)cc_context=-1;
    if(!cc_detail_kind && kind)cc_detail_kind=kind;
    damage();
}
static void edited(void){cc_edit_started=wm_time_ms();cc_blink=1;cc_selected=0;damage();}
static int matches(int id){const GuiApp *app=gui_app_get(id);return app&&app->name&&(cc_query[0]?app_search_matches(app->name,cc_query):(cc_pins&(1u<<id))!=0);}
static int app_at(int row){for(int id=0;id<APP_COUNT;id++)if(matches(id)&&row--==0)return id;return -1;}
static int query_width(int count){char prefix[32];int i=0;while(cc_query[i]&&i<count){prefix[i]=cc_query[i];i++;}prefix[i]=0;return text_width(prefix,1);}
static void avatar(int x,int y,u32 color){ui_bridge_roundrect(x+6,y,12,12,6,color);ui_bridge_roundrect(x,y+13,24,11,7,color);}
int control_center_active(void){return cc_shown;}
static void commit(void){if(cc_settings_changed)app_host_save_settings();cc_settings_changed=0;cc_capture=-1;}
void control_center_close(void){if(cc_shown){damage();commit();cc_shown=cc_animating=cc_opening=0;cc_position=-HEIGHT;cc_list=cc_detail_kind=cc_detail_slide=cc_search_focus=0;app_search_reset(&cc_input);cc_selected=0;cc_context=cc_hover=cc_hot=-1;}}
void control_center_toggle(void){
    if(!cc_shown){cc_shown=1;cc_position=-HEIGHT;cc_opening=0;app_search_reset(&cc_input);cc_pins=app_host_launcher_pins();cc_context=cc_hover=-1;}
    detail_set(0);
    commit();cc_start_position=cc_position;cc_opening=!cc_opening;cc_animating=1;cc_started=wm_time_ms();damage();
}
int control_center_poll(unsigned now){
    if(!cc_shown)return 0;
    int profile_changed=0;
    int blink=cc_search_focus&&((now-cc_edit_started)/500%2==0);
    if(blink!=cc_blink){cc_blink=blink;damage();profile_changed=1;}
    int detail_goal=cc_detail_kind==cc_list && cc_list?256:0;
    if(cc_detail_slide!=detail_goal){
        u32 elapsed=now-cc_detail_started;int t=elapsed>=DETAIL_DURATION?256:(int)(elapsed*256/DETAIL_DURATION);
        cc_detail_slide=cc_detail_origin+(detail_goal-cc_detail_origin)*ease_in_out_cubic(t)/256;
        damage();profile_changed=1;
    }
    if(!cc_detail_slide && cc_detail_kind!=cc_list){
        cc_detail_kind=cc_list;cc_detail_origin=0;cc_detail_started=now;
        damage();profile_changed=1;
    }
    if(!cc_animating)return profile_changed;
    u32 elapsed=now-cc_started;int t=elapsed>=DURATION?256:(int)(elapsed*256/DURATION);
    int goal=cc_opening?0:-HEIGHT;
    cc_position=cc_start_position+(goal-cc_start_position)*ease_in_out_cubic(t)/256;
    damage();
    if(t==256){cc_animating=0;if(!cc_opening)control_center_close();}
    return 1;
}
static void label(int x,int y,const char *s,u32 color){ui_bridge_text(left()+x,top()+y,s,color,2);}
static u32 panel_color(void){ThemeColors *t=ui_theme();return blend(t->surface_elevated,t->accent,6);}
static u32 card_color(void){return blend(panel_color(),ui_theme()->surface_secondary,160);}
static void panel(int x,int y,int w,int h,int radius){
    ui_bridge_roundrect(x,y,w,h,radius,panel_color());
}
static void card(int y,int h){ui_bridge_roundrect(left()+16,top()+y,WIDTH-32,h,16,card_color());}
static void detail_button(int x,int y,int kind){
    ThemeColors *t=ui_theme();int active=cc_list==kind,hover=cc_hot==kind;
    u32 fill=blend(card_color(),t->accent,active?80:hover?44:20);
    u32 arrow=active||hover?t->accent_hover:t->text_secondary;
    u32 edge=blend(fill,arrow,180);
    int bx=left()+x,by=top()+y;
    ui_bridge_roundrect(bx,by,30,30,10,fill);
    /* A 8x12 chevron centered on the button, independent of font bearings. */
    for(int row=0;row<12;row++){
        int step=row<6?row:11-row;
        int px=bx+(active?16-step:11+step),py=by+9+row;
        ui_bridge_rect(px,py,3,1,edge);
        ui_bridge_rect(px+1,py,1,1,arrow);
    }
}
static void slider(int y,int value,int minimum){
    ThemeColors *t=ui_theme();int x=left()+30,w=WIDTH-60;
    int offset=(w-1)*(value-minimum)/(100-minimum);
    ui_bridge_roundrect(x,top()+y+9,w,8,4,blend(card_color(),t->border,140));
    ui_bridge_roundrect(x,top()+y+9,offset+1,8,4,blend(t->accent,t->text,28));
    ui_bridge_roundrect(x+offset-9,top()+y+3,18,18,9,0xf7f9ff);
}
static void detail_draw(GraphicsClip clip){
    if(!cc_detail_kind || !cc_detail_slide)return;
    ThemeColors *t=ui_theme();int kind=cc_detail_kind,w=detail_width();
    int x=detail_left(),y=top()+detail_top(kind)+(256-cc_detail_slide)*8/256;
    int h=detail_height(kind)*cc_detail_slide/256;
    if(h<8)return;
    panel(x,y,w,h,h<44?h/2:22);
    GraphicsClip inner=clip;
    if(inner.x1<x+12)inner.x1=x+12;if(inner.x2>x+w-12)inner.x2=x+w-12;
    if(inner.y1<y+12)inner.y1=y+12;if(inner.y2>y+h-12)inner.y2=y+h-12;
    graphics_set_clip(inner);
    GraphicsClip heading=inner;if(heading.x2>x+w-48)heading.x2=x+w-48;graphics_set_clip(heading);
    ui_bridge_text(x+20,y+18,kind==1?"Wi-Fi":kind==2?"Bluetooth":kind==3?"Sound output":"Profile",t->text,2);
    graphics_set_clip(inner);
    ui_bridge_text(x+w-30,y+19,"x",t->text_secondary,1);
    if(kind<3){
        ui_bridge_roundrect(x+16,y+62,w-32,58,14,card_color());
        ui_bridge_text(x+26,y+75,"No supported adapter",t->text_secondary,1);
        ui_bridge_text(x+20,y+138,kind==1?"Wi-Fi is unavailable":"Bluetooth is unavailable",t->text_muted,1);
    }else{
        int rows=kind==3?2:3;
        const char *actions[]={"Turn off","Restart","Logout"};
        for(int i=0;i<rows;i++){
            int available=kind!=3 || !i || audio_is_available();
            int selected=kind==3 && audio_output()==i;
            u32 fill=selected?blend(card_color(),t->accent,56):cc_hot==10+i?blend(card_color(),t->accent,28):card_color();
            ui_bridge_roundrect(x+16,y+58+i*38,w-32,32,12,fill);
            ui_bridge_text(x+28,y+65+i*38,kind==3?(i?"AC'97":"PC Speaker"):actions[i],available?t->text:t->text_muted,1);
            if(selected)ui_bridge_roundrect(x+w-36,y+69+i*38,8,8,4,t->accent_hover);
        }
        if(kind==3)ui_bridge_text(x+20,y+156,audio_output()?"Selected: AC'97":"Selected: PC Speaker",t->text_secondary,1);
    }
    graphics_set_clip(clip);
}
void control_center_draw(void){
    if(!cc_shown)return;
    GraphicsClip old=graphics_get_clip(),clip=old;if(clip.y1<32)clip.y1=32;graphics_set_clip(clip);
    ThemeColors *t=ui_theme();int x=left(),y=top();
    int sx=search_left(),row=0;
    panel(sx,y,SEARCH_WIDTH,SEARCH_HEIGHT,22);
    ui_bridge_text(sx+16,y+14,"Applications",t->text,2);
    ui_bridge_roundrect(sx+12,y+48,SEARCH_WIDTH-24,36,12,card_color());
    ui_bridge_rect(sx+12,y+82,SEARCH_WIDTH-24,cc_search_focus?2:1,cc_search_focus?t->accent:t->border);
    GraphicsClip field=clip;
    if(field.x1<sx+20)field.x1=sx+20;if(field.x2>sx+SEARCH_WIDTH-20)field.x2=sx+SEARCH_WIDTH-20;
    if(field.y1<y+50)field.y1=y+50;if(field.y2>y+80)field.y2=y+80;graphics_set_clip(field);
    int offset=query_width(cc_input.cursor)-(SEARCH_WIDTH-42);if(offset<0)offset=0;
    int tx=sx+20-offset;
    if(cc_search_focus&&cc_input.anchor>=0&&cc_input.anchor!=cc_input.cursor){int a=cc_input.anchor,b=cc_input.cursor;if(a>b){int z=a;a=b;b=z;}
        ui_bridge_rect(tx+query_width(a),y+53,query_width(b)-query_width(a),24,t->selection);}
    ui_bridge_text(tx,y+55,cc_query[0]?cc_query:"Search apps...",cc_query[0]?t->text:t->text_muted,1);
    if(cc_search_focus&&cc_blink)ui_bridge_rect(tx+query_width(cc_input.cursor),y+54,2,21,t->text);
    graphics_set_clip(clip);
    ui_bridge_text(sx+16,y+90,cc_query[0]?"Results":"Recommended",t->text_secondary,1);
    for(int id=0;id<APP_COUNT;id++){
        const GuiApp *app=gui_app_get(id);
        if(!app||!matches(id))continue;
        if(row==cc_hover||(cc_search_focus&&row==cc_selected))ui_bridge_roundrect(sx+8,y+116+row*30,SEARCH_WIDTH-16,28,7,t->surface_secondary);
        ui_app_icon_draw(id,sx+14,y+118+row*30,24);
        ui_bridge_text(sx+44,y+123+row*30,app->name,t->text,1);
        for(int dot=0;dot<3;dot++)ui_bridge_roundrect(sx+SEARCH_WIDTH-20,y+123+row*30+dot*4,2,2,1,t->text_secondary);
        row++;
    }
    if(!row&&cc_query[0])ui_bridge_text(sx+20,y+125,"No applications",t->text_muted,1);
    if(cc_context>=0){
        ui_bridge_roundrect(sx+8,y+112,SEARCH_WIDTH-16,106,8,t->surface_secondary);
        ui_bridge_text(sx+18,y+122,"Open",t->text,1);
        ui_bridge_text(sx+18,y+156,"Run as admin",t->text,1);
        ui_bridge_text(sx+18,y+190,cc_pins&(1u<<cc_context)?"Unpin":"Pin to recommended",t->text,1);
    }
    panel(x,y,WIDTH,HEIGHT,24);
    for(int i=0;i<2;i++){
        ui_bridge_roundrect(x+16+i*184,y+18,176,70,16,card_color());
        label(30+i*184,28,i?"Bluetooth":"Wi-Fi",t->text);
        label(30+i*184,58,"No adapter",t->text_secondary);
        detail_button(154+i*184,26,i+1);
    }
    card(104,60);label(30,116,"Connection",t->text);
    NetworkInterface *iface=net_manager_get_active_iface();
    int connected=net_manager_enabled()&&iface&&iface->link_up;
    ui_bridge_roundrect(x+30,y+146,8,8,4,connected?t->success:t->text_muted);
    label(50,140,!net_manager_enabled()?"Paused":connected?"Ethernet connected":"Disconnected",t->text_secondary);
    card(180,92);label(30,194,"Brightness",t->text);char b[16];number(b,(u32)framebuffer_brightness());
    label(322,194,b,t->text_secondary);slider(236,framebuffer_brightness(),20);
    card(284,92);label(30,298,"Volume",cc_hot==3||cc_list==3?t->accent_hover:t->text);
    number(b,audio_get_volume());label(322,298,b,t->text_secondary);slider(340,audio_get_volume(),0);
    card(392,40);avatar(x+28,y+400,t->text_secondary);label(64,402,"Profile",t->text);
    detail_button(346,396,4);
    detail_draw(clip);
    graphics_set_clip(old);
}
static int update_slider(int x){
    int distance=x-(left()+30),w=WIDTH-60;if(distance<0)distance=0;if(distance>=w)distance=w-1;
    int value=(distance*100+(w-1)/2)/(w-1),changed=0;
    if(cc_capture==0){changed=value!=audio_get_volume();if(changed)audio_set_volume((u8)value);}
    else {value=20+value*80/100;changed=value!=framebuffer_brightness();if(changed){framebuffer_set_brightness(value);app_host_invalidate(-1);}}
    if(changed){cc_settings_changed=1;damage();}return changed;
}
int control_center_pointer(int x,int y,int down){
    if(!cc_shown)return 0;
    int hover=x>=search_left()+8&&x<search_left()+SEARCH_WIDTH-8&&y>=top()+116?(y-top()-116)/30:-1;
    if(hover<0||app_at(hover)<0)hover=-1;
    if(hover!=cc_hover){cc_hover=hover;damage();}
    int lx=x-left(),ly=y-top(),hot=-1;
    if(ly>=26&&ly<56){if(lx>=154&&lx<184)hot=1;else if(lx>=338&&lx<368)hot=2;}
    if(lx>=16&&lx<WIDTH-16&&ly>=284&&ly<330)hot=3;
    if(lx>=16&&lx<WIDTH-16&&ly>=392&&ly<432)hot=4;
    if(cc_detail_slide==256&&x>=detail_left()+16&&x<detail_left()+detail_width()-16){
        int dy=ly-detail_top(cc_detail_kind)-58;
        if(dy>=0&&dy<(cc_detail_kind==4?3:2)*38&&dy%38<32)hot=10+dy/38;
    }
    if(hot!=cc_hot){cc_hot=hot;damage();}
    if(!down){commit();return 1;}
    if(cc_capture>=0)update_slider(x);
    return 1;
}
int control_center_click(int x,int y){
    if(!cc_shown)return 0;
    int lx=x-left(),ly=y-top();
    if(cc_detail_kind && x>=detail_left() && x<detail_left()+detail_width() &&
       ly>=detail_top(cc_detail_kind) && ly<detail_top(cc_detail_kind)+detail_height(cc_detail_kind)){
        if(cc_animating || cc_detail_slide!=256 || cc_detail_kind!=cc_list)return 1;
        int dy=ly-detail_top(cc_detail_kind);
        if(x>=detail_left()+detail_width()-38 && dy>=12 && dy<40){detail_set(0);return 1;}
        if(x>=detail_left()+16 && x<detail_left()+detail_width()-16 && dy>=58){
            int action=(dy-58)/38;
            if((dy-58)%38<32){
                if(cc_detail_kind==3 && action<2){if(audio_select_output(action)){app_host_save_settings();damage();}}
                else if(cc_detail_kind==4 && action<3){control_center_close();if(action==2)auth_logout();else app_host_power(action==1);}
            }
        }
        return 1;
    }
    if(x>=search_left()&&x<search_left()+SEARCH_WIDTH&&ly>=0&&ly<SEARCH_HEIGHT){
        if(cc_animating)return 1;
        if(cc_list)detail_set(0);
        if(ly>=48&&ly<84){cc_search_focus=1;cc_context=-1;
            int offset=query_width(cc_input.cursor)-(SEARCH_WIDTH-42);if(offset<0)offset=0;
            int position=0,px=x-search_left()-20+offset;while(cc_query[position]&&query_width(position+1)<px)position++;
            app_search_select(&cc_input,position,0);edited();return 1;
        }
        cc_search_focus=0;
        if(cc_context>=0){int id=cc_context;cc_context=-1;
            if(ly>=112&&ly<214){int action=(ly-112)/34;
                if(action==2){unsigned pins=cc_pins^(1u<<id);if(app_host_launcher_set_pins(pins))cc_pins=pins;}
                else {control_center_close();if(action==1)auth_run_admin(id);else app_host_open(id);return 1;}
            }
            damage();return 1;
        }
        if(ly>=116){int id=app_at((ly-116)/30);
            if(id>=0){if(x>=search_left()+SEARCH_WIDTH-36){cc_context=id;damage();}else{control_center_close();app_host_open(id);}return 1;}
        }
        damage();return 1;
    }
    if(lx>=WIDTH&&lx<WIDTH+DETAIL_GAP&&ly>=0&&ly<HEIGHT){detail_set(0);return 1;}
    if(lx<0||lx>=WIDTH||ly<0||ly>=HEIGHT){control_center_toggle();return 1;}
    if(cc_animating)return 1;
    cc_search_focus=0;
    if(ly>=392&&ly<432&&lx>=16&&lx<WIDTH-16){detail_set(cc_list==4?0:4);return 1;}
    if(ly>=18&&ly<88){
        if((lx>=154&&lx<184)||(lx>=338&&lx<368)){int list=lx>=204?2:1;detail_set(cc_list==list?0:list);}
        return 1; /* Unsupported radio is never displayed as enabled. */
    }
    if(ly>=284&&ly<330&&lx>=16&&lx<WIDTH-16){detail_set(cc_list==3?0:3);return 1;}
    if(lx>=22&&lx<=WIDTH-22) {
        if(ly>=230&&ly<264){cc_capture=1;update_slider(x);return 1;}
        if(ly>=334&&ly<368){cc_capture=0;update_slider(x);return 1;}
    }
    return 1;
}
int control_center_key_ex(unsigned char code,int shift,int control){
    if(!cc_shown)return 0;
    if(code&128)return 1;
    if(code==1){if(cc_context>=0){cc_context=-1;damage();}else if(cc_list)detail_set(0);else if(cc_opening)control_center_toggle();return 1;}
    if(!cc_search_focus||cc_animating)return 1;
    int count=0;
    for(int id=0;id<APP_COUNT;id++)if(matches(id))count++;
    if(code==72&&cc_selected>0)cc_selected--;
    else if(code==80&&cc_selected+1<count)cc_selected++;
    else if(code==28){int id=app_at(cc_selected);if(id>=0){control_center_close();app_host_open(id);return 1;}}
    else {unsigned key=code==14?8:code==83?127:code==75?APP_SEARCH_LEFT:code==77?APP_SEARCH_RIGHT:code==71?APP_SEARCH_HOME:code==79?APP_SEARCH_END:(unsigned char)gui_key_character(code);
        if(app_search_edit(&cc_input,key,shift,control))edited();
    }
    damage();return 1;
}
int control_center_key(unsigned char code){return control_center_key_ex(code,0,0);}
