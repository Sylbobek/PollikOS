#ifndef POLLIK_X64_TTY_H
#define POLLIK_X64_TTY_H
#include <stddef.h>
#include <stdint.h>
/* Interactive x86_64 console TTY: polled COM1 and PS/2 keyboard bytes enter a
 * bounded queue consumed by userspace stdin. The PS/2 mouse is polled here as
 * well. Disabled until the console boot path enables it, so synchronous tests
 * keep the historical stdin contract. */
void tty64_init(void);
void tty64_enable(void);
int tty64_enabled(void);
void tty64_set_foreground(uint64_t pgid); /* 0 clears; Ctrl+C signals this process group */
void tty64_poll(void);                  /* poll UART, keyboard and mouse on PIT */
size_t tty64_available(void);
size_t tty64_pop(void *destination, size_t count);
#endif
