#ifndef POLLIK_X64_WINDOW_H
#define POLLIK_X64_WINDOW_H
#include "user.h"
int window64_init(void);
int window64_dispatch(Process64 *process, UserFrame *frame);
void window64_process_cleanup(Process64 *process);
int window64_mapping_busy(const Process64 *process, virt_addr_t address);
int window64_key_event(uint32_t key, uint32_t modifiers, int down);
void window64_console_begin(void);
void window64_console_damage(unsigned x, unsigned y, unsigned width, unsigned height);
void window64_console_end(void);
void window64_session_hide(int hidden);
#endif
