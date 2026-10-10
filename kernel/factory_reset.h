#ifndef POLLIK_FACTORY_RESET_H
#define POLLIK_FACTORY_RESET_H
/* Kernel-only broker. Callers authenticate/confirm and stop the old session
 * before finish. These operations touch only the mounted PollikFS volume. */
int factory_reset_pending(void); /* 0 absent, 1 valid request, -1 invalid/I/O */
int factory_reset_mark(void);
int factory_reset_finish(void);
#endif
