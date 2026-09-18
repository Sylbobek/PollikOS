#ifndef POLLIK_MEM_H
#define POLLIK_MEM_H

#include "system.h"

void mem_init(void);
void *kmalloc(u32 size);
void kfree(void *ptr);
void *kcalloc(u32 n, u32 size);
void *krealloc(void *ptr, u32 new_size);
u32 mem_get_used(void);
u32 mem_get_free(void);

#endif
