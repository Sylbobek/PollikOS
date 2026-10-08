#ifndef POLLIK_UI_ICONS_H
#define POLLIK_UI_ICONS_H
enum { UI_ICON_WELCOME,UI_ICON_FILES,UI_ICON_TERMINAL,UI_ICON_NOTES,
       UI_ICON_SETTINGS,UI_ICON_BROWSER,UI_ICON_POLLIKMARK,UI_ICON_CALCULATOR,
       UI_ICON_FOLDER,UI_ICON_FOLDER_BLUE,UI_ICON_FILE,UI_ICON_TRASH,UI_ICON_COUNT };
void ui_icons_init(void);
void ui_icon_draw(int id,int x,int y,int size);
void ui_app_icon_draw(int id,int x,int y,int size);
#endif
