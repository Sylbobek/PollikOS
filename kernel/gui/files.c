#include "app_internal.h"
#include "../vfs.h"
#include "../trash.h"
#include "../pollikfs.h"
#include "../ui_icons.h"
#include "../process.h"
#include "../ui.h"

#define FILES_MAX_ITEMS 128
#define FILES_ROW_HEIGHT 40
#define FILES_ROW_BODY 36

typedef struct {
    char name[64];
    u32 size;
    int is_dir;
} FilesEntry;

static FilesEntry g_files_entries[FILES_MAX_ITEMS];
static int g_files_count = 0;
static int g_files_selected = -1;
static int first_row = 0;
static char g_files_current_path[128] = "/home";
static int g_files_last_click_valid;
static char g_files_last_click_path[128];
static u32 g_files_last_click_tick;
static AppRect files_list(int width, int height);

static int str_equal(const char *a, const char *b) {
    int i = 0;
    while (a[i] || b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return 1;
}

static void files_refresh_entries(void) {
    g_files_count = 0;
    g_files_selected = -1;
    first_row = 0;
    g_files_last_click_valid = 0;

    int fd = vfs_open(g_files_current_path, O_RDONLY);
    if (fd >= 0) {
        vfs_dirent_t dent;
        while (vfs_readdir(fd, &dent) > 0 && g_files_count < FILES_MAX_ITEMS) {
            if (dent.name[0] == '.') continue;
            FilesEntry *entry = &g_files_entries[g_files_count];
            memset(entry, 0, sizeof(FilesEntry));
            u32 n = 0;
            while (dent.name[n] && n < 63) { entry->name[n] = dent.name[n]; n++; }
            entry->name[n] = 0;
            entry->is_dir = (dent.type == VFS_DIR);

            char sub_path[128];
            u32 p = 0;
            while (g_files_current_path[p]) { sub_path[p] = g_files_current_path[p]; p++; }
            if (p > 0 && sub_path[p - 1] != '/') sub_path[p++] = '/';
            n = 0;
            while (dent.name[n] && p < 127) { sub_path[p++] = dent.name[n++]; }
            sub_path[p] = 0;

            vfs_stat_t st;
            if (vfs_stat(sub_path, &st) == 0) {
                entry->size = st.size;
            } else {
                entry->size = 0;
            }
            g_files_count++;
        }
        vfs_close(fd);
    }
}

void files_open_path(const char *path) {
    if (!path || !path[0]) return;
    u32 p = 0;
    while (path[p] && p < 127) { g_files_current_path[p] = path[p]; p++; }
    g_files_current_path[p] = 0;

    files_refresh_entries();
    app_host_open(APP_FILES);
    app_host_invalidate(APP_FILES);
}

static int files_has_extension(const char *name, const char *extension) {
    int name_len = len(name), ext_len = len(extension);
    if (name_len < ext_len) return 0;
    for (int i = 0; i < ext_len; ++i) {
        char a = name[name_len - ext_len + i];
        char b = extension[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static void files_entry_path(const FilesEntry *entry, char path[128]) {
    u32 p = 0;
    while (g_files_current_path[p] && p < 127) { path[p] = g_files_current_path[p]; p++; }
    if (p > 0 && path[p - 1] != '/' && p < 127) path[p++] = '/';
    u32 n = 0;
    while (entry->name[n] && p < 127) path[p++] = entry->name[n++];
    path[p] = 0;
}

static void files_launch_pol(const char *path) {
    char elf_ident[5];
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0 || vfs_read(fd, elf_ident, sizeof(elf_ident)) != (int)sizeof(elf_ident)) {
        if (fd >= 0) vfs_close(fd);
        ui_notify("Pollik App", "The .pol file is unreadable", ICON_WARNING);
        return;
    }
    vfs_close(fd);

    if (elf_ident[0] != 0x7f || elf_ident[1] != 'E' || elf_ident[2] != 'L' || elf_ident[3] != 'F') {
        ui_notify("Pollik App", ".pol must be a PollikOS ELF program", ICON_WARNING);
        return;
    }
    if ((u8)elf_ident[4] == 2) {
        ui_notify("Pollik App", "64-bit .pol needs the x86_64 desktop", ICON_WARNING);
        return;
    }
    if ((u8)elf_ident[4] != 1) {
        ui_notify("Pollik App", "Unsupported .pol program format", ICON_WARNING);
        return;
    }

    int pid = process_spawn_elf_path(path);
    if (pid < 0) {
        ui_notify("Pollik App", "Program is invalid or process slots are full", ICON_WARNING);
        return;
    }
    char message[48] = "Started as process ";
    char pid_text[12];
    number(pid_text, (u32)pid);
    append_str(message, pid_text, sizeof(message));
    ui_notify("Pollik App", message, ICON_APP);
}

static void files_open_entry(const FilesEntry *entry, const char *path) {
    if (entry->is_dir) {
        files_open_path(path);
    } else if (files_has_extension(entry->name, ".pol")) {
        files_launch_pol(path);
    } else {
        viewers_open_path(path, entry->name);
    }
}

void files_open_file(const char *path,const char *name){
    if(files_has_extension(name,".pol"))files_launch_pol(path);
    else viewers_open_path(path,name);
}
void files_close(void){g_files_last_click_valid=0;}
void files_refresh(void){files_refresh_entries();app_host_invalidate(APP_FILES);}
void files_key(u8 code){
    if(code==72||code==80){
        if(!g_files_count)return;
        if(g_files_selected<0)g_files_selected=code==72?g_files_count-1:0;
        else if(code==72&&g_files_selected>0)g_files_selected--;
        else if(code==80&&g_files_selected+1<g_files_count)g_files_selected++;
        GuiAppSize s=gui_app_size(APP_FILES);AppRect list=files_list(s.width,s.height);
        int rows=list.h/FILES_ROW_HEIGHT;
        if(g_files_selected<first_row)first_row=g_files_selected;
        else if(g_files_selected>=first_row+rows)first_row=g_files_selected-rows+1;
        app_host_invalidate(APP_FILES);
    }else if(code==28)files_open_selected();
    else if(code==83)files_delete_selected();
}

static AppRect files_list(int width, int height) {
    int left=width<600?156:196;
    int rows=(height-180)/FILES_ROW_HEIGHT;
    if(rows<1)rows=1;if(rows>FILES_MAX_ITEMS)rows=FILES_MAX_ITEMS;
    if(first_row>g_files_count-rows)first_row=g_files_count-rows;
    if(first_row<0)first_row=0;
    return (AppRect){left,136,width-left-28,rows*FILES_ROW_HEIGHT};
}

void files_scroll(int delta) {
    GuiAppSize s = gui_app_size(APP_FILES);
    AppRect list = files_list(s.width, s.height);
    int max = g_files_count - list.h / FILES_ROW_HEIGHT;
    if (max < 0) max = 0;
    if (delta > max - first_row) first_row = max;
    else if (delta < -first_row) first_row = 0;
    else first_row += delta;
    app_host_invalidate(APP_FILES);
}

static const char *const places[]={"/home","/home/Desktop","/home/Desktop/Documents","/home/Trash","/Applications"};
static const char *const place_names[]={"Home","Desktop","Documents","Trash","Applications"};
static void files_size(char out[32],u32 size){
    number(out,size>=1048576?size/1048576:size>=1024?size/1024:size);
    append_str(out,size>=1048576?" MB":size>=1024?" KB":" B",32);
}
static void files_parent(void){
    char parent[128];copy(parent,g_files_current_path);int n=len(parent);
    while(n>1&&parent[n-1]!='/')n--;
    if(n>1)n--;parent[n]=0;files_open_path(parent);
}
void files_render(int width,int height,int active){
    (void)active;ThemeColors *t=ui_theme();AppRect list=files_list(width,height);
    int sidebar=list.x-32;
    roundrect(12,50,sidebar,height-66,18,t->surface_secondary);
    app_label(28,68,sidebar-32,"Places",t->text_secondary,1);
    for(int i=0;i<5;i++){
        int sy=94+i*32,selected=str_equal(g_files_current_path,places[i]);
        if(selected)roundrect(20,sy,sidebar-16,28,10,t->selection);
        ui_draw_icon(i==3?ICON_TRASH:i==4?ICON_APP:ICON_FOLDER,28,sy+6,16,t->accent,t->text_secondary);
        app_label(52,sy+7,sidebar-52,place_names[i],selected?t->text:t->text_secondary,1);
    }
    roundrect(list.x,56,32,30,11,t->surface_secondary);centered(list.x,64,32,"<",t->text_secondary,1);
    const char *title=g_files_current_path;
    for(const char *p=g_files_current_path;*p;p++)if(*p=='/'&&p[1])title=p+1;
    app_label(list.x+46,60,list.w-148,title,t->text,2);
    app_label(list.x,93,list.w,g_files_current_path,t->text_secondary,1);
    int trash=str_equal(g_files_current_path,"/home/Trash");
    roundrect(width-116,56,88,30,11,t->surface_secondary);centered(width-116,64,88,trash?"Empty Trash":"Refresh",trash?t->danger:t->text_secondary,1);
    app_label(list.x+44,116,list.w-140,"Name",t->text_secondary,1);
    app_label(width-116,116,88,"Size",t->text_secondary,1);
    for(int row=0;row<list.h/FILES_ROW_HEIGHT;row++){
        int i=first_row+row;if(i>=g_files_count)break;
        FilesEntry *e=&g_files_entries[i];int y=list.y+row*FILES_ROW_HEIGHT;
        roundrect(list.x,y,list.w,FILES_ROW_BODY,12,i==g_files_selected?t->selection:t->surface);
        ui_icon_draw(e->is_dir?UI_ICON_FOLDER:UI_ICON_FILE,list.x+8,y+6,24);
        app_label(list.x+44,y+10,list.w-(trash?200:140),e->name,t->text,1);
        char size[32];if(e->is_dir)copy(size,"Folder");else files_size(size,e->size);
        app_label(width-116,y+10,88,size,t->text_secondary,1);
        if(trash){roundrect(width-196,y+5,68,26,9,t->surface_secondary);centered(width-196,y+12,68,"Restore",t->accent,1);}
    }
    if(!g_files_count)app_label(list.x+20,list.y+24,list.w-40,"This folder is empty",t->text_secondary,1);
    char count[48],n[16];number(count,g_files_count);append_str(count," items",sizeof(count));
    if(g_files_selected>=0){number(n,g_files_selected+1);append_str(count,"   Selected: ",sizeof(count));append_str(count,n,sizeof(count));}
    app_label(list.x,height-34,list.w,count,t->text_secondary,1);
}

void files_click(int x, int y) {
    GuiAppSize s = gui_app_size(APP_FILES);
    AppRect list = files_list(s.width, s.height);
    int sidebar_w = list.x - 38;

    /* Check sidebar clicks */
    if (x >= 20 && x < 20 + sidebar_w - 16) {
        if (y >= 94 && y < 122) { files_open_path("/home"); return; }
        if (y >= 126 && y < 154) { files_open_path("/home/Desktop"); return; }
        if (y >= 158 && y < 186) { files_open_path("/home/Desktop/Documents"); return; }
        if (y >= 190 && y < 218) { files_open_path("/home/Trash"); return; }
        if (y >= 222 && y < 250) { files_open_path("/Applications"); return; }
    }

    if(x>=list.x&&x<list.x+32&&y>=56&&y<86){files_parent();return;}
    if(x>=s.width-116&&x<s.width-28&&y>=56&&y<86){
        if(str_equal(g_files_current_path,"/home/Trash")){trash_empty();extern void desktop_items_scan(void);desktop_items_scan();}
        files_refresh();return;
    }


    /* Check list item clicks */
    if (app_hit(list, x, y) && (y - list.y) % FILES_ROW_HEIGHT < FILES_ROW_BODY) {
        int idx = first_row + (y - list.y) / FILES_ROW_HEIGHT;
        if (idx >= 0 && idx < g_files_count) {
            FilesEntry *entry = &g_files_entries[idx];
            g_files_selected = idx;

            /* Check if Restore button was clicked in Trash view */
            if (str_equal(g_files_current_path, "/home/Trash")) {
                if (x >= s.width - 196 && x < s.width - 128) {
                    trash_restore_item(entry->name);
                    files_refresh_entries();
                    extern void desktop_items_scan(void);
                    desktop_items_scan();
                    app_host_invalidate(APP_FILES);
                    return;
                }
            }

            char full_path[128];
            files_entry_path(entry, full_path);
            int double_click = g_files_last_click_valid &&
                str_equal(g_files_last_click_path, full_path) &&
                (u32)(ticks - g_files_last_click_tick) <= 75u;
            g_files_last_click_valid = !double_click;
            if (double_click) {
                files_open_entry(entry, full_path);
            } else {
                copy(g_files_last_click_path, full_path);
                g_files_last_click_tick = ticks;
                app_host_invalidate(APP_FILES);
            }
        }
    }
}

int files_select_at(int x, int y) {
    GuiAppSize s = gui_app_size(APP_FILES);
    AppRect list = files_list(s.width, s.height);
    if (app_hit(list, x, y) && (y - list.y) % FILES_ROW_HEIGHT < FILES_ROW_BODY) {
        int idx = first_row + (y - list.y) / FILES_ROW_HEIGHT;
        if (idx >= 0 && idx < g_files_count) {
            g_files_selected = idx;
            return idx;
        }
    }
    return -1;
}

void files_open_selected(void) {
    if (g_files_selected >= 0 && g_files_selected < g_files_count) {
        FilesEntry *entry = &g_files_entries[g_files_selected];
        char full_path[128];
        files_entry_path(entry, full_path);
        files_open_entry(entry, full_path);
    }
}

void files_delete_selected(void) {
    if (g_files_selected >= 0 && g_files_selected < g_files_count) {
        FilesEntry *entry = &g_files_entries[g_files_selected];
        char full_path[128];
        u32 p = 0;
        while (g_files_current_path[p]) { full_path[p] = g_files_current_path[p]; p++; }
        if (p > 0 && full_path[p - 1] != '/') full_path[p++] = '/';
        u32 n = 0;
        while (entry->name[n] && p < 127) { full_path[p++] = entry->name[n++]; }
        full_path[p] = 0;

if (str_equal(g_files_current_path, "/home/Trash")) {
            trash_delete_permanent(entry->name);
            files_refresh_entries();
            extern void desktop_items_scan(void);
            desktop_items_scan();
            app_host_invalidate(APP_FILES);
            return;
        }

        trash_move_item(full_path);
        files_refresh_entries();
        app_host_invalidate(APP_FILES);
    }
}

const char *files_selected_name(void) {
    if (g_files_selected >= 0 && g_files_selected < g_files_count) {
        return g_files_entries[g_files_selected].name;
    }
    return 0;
}
int files_drag_path(int x,int y,char path[128],char name[64],int *is_dir){
    if(!g_files_last_click_valid||g_files_selected<0||str_equal(g_files_current_path,"/home/Trash"))return 0;
    GuiAppSize s=gui_app_size(APP_FILES);AppRect list=files_list(s.width,s.height);
    if(!app_hit(list,x,y)||(y-list.y)%FILES_ROW_HEIGHT>=FILES_ROW_BODY)return 0;
    int row=first_row+(y-list.y)/FILES_ROW_HEIGHT;if(row!=g_files_selected)return 0;
    files_entry_path(&g_files_entries[row],path);copy(name,g_files_entries[row].name);*is_dir=g_files_entries[row].is_dir;
    return 1;
}
void files_cancel_click(void){g_files_last_click_valid=0;}
int files_drop_target(int x,int y,char path[128]){
    GuiAppSize s=gui_app_size(APP_FILES);AppRect list=files_list(s.width,s.height);
    if(x>=20&&x<list.x-20&&y>=94&&y<250){
        int i=(y-94)/32;if(i<5&&(y-94)%32<28){copy(path,places[i]);return 1;}
    }
    if(!app_hit(list,x,y))return 0;
    int row=first_row+(y-list.y)/FILES_ROW_HEIGHT;
    if(row<g_files_count&&(y-list.y)%FILES_ROW_HEIGHT<FILES_ROW_BODY&&g_files_entries[row].is_dir)
        files_entry_path(&g_files_entries[row],path);
    else copy(path,g_files_current_path);
    return 1;
}
int files_move_path(const char *source,const char *directory){
    if(!source||!directory||!source[0]||!directory[0])return 0;
    const char *name=source;for(const char *p=source;*p;p++)if(*p=='/')name=p+1;
    int n=len(directory),m=len(name);char destination[128];
    if(!m||n+m+2>(int)sizeof(destination)){ui_notify("Move file","Path is too long",ICON_WARNING);return 0;}
    copy(destination,directory);if(n&&destination[n-1]!='/')destination[n++]='/';
    for(int i=0;i<=m;i++)destination[n+i]=name[i];
    if(str_equal(source,destination))return 2;
    vfs_stat_t st;
    if(vfs_stat(directory,&st)<0||st.type!=VFS_DIR){ui_notify("Move file","Destination folder is unavailable",ICON_WARNING);return 0;}
    int result=str_equal(directory,"/home/Trash")?trash_move_item(source):vfs_rename(source,destination);
    if(result<0){
        const char *reason=pollikfs_readonly()?"Disk is read only; your file was not moved":
            pollikfs_error()==VFS_EXISTS?"A file with this name already exists":
            pollikfs_error()==VFS_DENIED?"Move denied: protected folder or a folder inside itself":"Move failed; your original file is preserved";
        ui_notify("Move file",reason,ICON_WARNING);return 0;
    }
    return 1;
}
