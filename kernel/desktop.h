#ifndef POLLIK_DESKTOP_H
#define POLLIK_DESKTOP_H
/* Boot/runtime boundary. All built-in clients still run in Ring0. */
void desktop_init(void);
void desktop_start(void);
/* One cooperative UI iteration; true means the runtime may idle. */
int desktop_poll(int network_changed);
#endif
