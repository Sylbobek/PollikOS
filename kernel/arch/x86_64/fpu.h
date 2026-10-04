#ifndef POLLIK_X64_FPU_H
#define POLLIK_X64_FPU_H

#include <stdint.h>

#define FPU64_STATE_SIZE 512

int fpu64_init(void);
int fpu64_state_init(void *state);
void fpu64_state_save(void *state);
void fpu64_state_restore(const void *state);

#endif
