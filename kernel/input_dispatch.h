#ifndef POLLIK_INPUT_DISPATCH_H
#define POLLIK_INPUT_DISPATCH_H
/* PS/2 initialization is called before interrupts are enabled. */
void input_dispatch_init(void);
/* Drain the same decoder, but discard client/launch input; preserve modifiers
 * and mouse button releases. Only existing window chrome is interactive. */
void input_dispatch_poll_window_only(void);
#endif
