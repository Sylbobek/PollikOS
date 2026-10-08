#ifndef POLLIK_CONTROL_CENTER_H
#define POLLIK_CONTROL_CENTER_H
void control_center_toggle(void);
void control_center_close(void);
int control_center_active(void);
int control_center_poll(unsigned now_ms);
int control_center_click(int x,int y);
int control_center_pointer(int x,int y,int down);
int control_center_key(unsigned char code);
int control_center_key_ex(unsigned char code,int shift,int control);
void control_center_draw(void);
#endif
