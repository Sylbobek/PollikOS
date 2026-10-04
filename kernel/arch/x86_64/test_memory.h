#ifndef POLLIK_X64_TEST_MEMORY_H
#define POLLIK_X64_TEST_MEMORY_H
#include "memory.h"

void *kernel_test_buffer_alloc(size_t size);
void kernel_test_buffer_free(void *buffer);

#endif
