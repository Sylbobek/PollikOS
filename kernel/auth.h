#ifndef POLLIK_AUTH_H
#define POLLIK_AUTH_H

#include "system.h"

void auth_init(void);
int auth_is_active(void);
void auth_lock(void);
void auth_render(int width, int height);
void auth_key(u8 scan_code, int shift);
void auth_key_ex(u8 scan_code, int shift, int control);
int auth_pointer(int x, int y, int button_down);
const char *auth_username(void);

#endif
