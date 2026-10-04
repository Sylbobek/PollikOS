#ifndef POLLIKOS_MEMORY_H
#define POLLIKOS_MEMORY_H
#include <stddef.h>
/* Memory/runtime diagnostics for applications. Ordinary code uses malloc and
 * friends from <stdlib.h>; direct brk/mmap policy stays inside the C runtime. */
size_t pollikos_page_size(void);
/* Total bytes obtained by the runtime allocator from brk plus successful
 * anonymous mappings (not the same as bytes currently in use). */
size_t pollikos_heap_bytes(void);
#endif
