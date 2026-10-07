#ifndef POLLIKOS_MALLOC_H
#define POLLIKOS_MALLOC_H
#include <stdlib.h>
/* Payload capacity of a live allocation returned by this SDK; NULL returns 0.
 * Passing foreign or freed storage is outside the allocation contract. */
size_t malloc_usable_size(void *memory);
#endif
