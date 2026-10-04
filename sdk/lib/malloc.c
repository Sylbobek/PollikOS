/* Small process-private allocator for PollikOS userspace.
 * The kernel supplies brk/mmap; all policy lives here. Small blocks come from
 * a contiguous brk arena with boundary tags, a doubly-linked free list and
 * forward/backward coalescing. Blocks of at least LARGE_THRESHOLD bytes are
 * mapped anonymously instead, so large requests do not force heap pages.
 * This is a bootstrap allocator, not a hardened production one. */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pollikos/syscall.h>
#include <pollikos/memory.h>
#define ARENA_MAGIC UINT64_C(0x504b4152454e4131)
#define MAPPED_MAGIC UINT64_C(0x504b4d4150504544)
#define BLOCK_HEADER 64u
#define BLOCK_MIN (BLOCK_HEADER + 16u)
#define HEAP_EXTEND (64u * 1024u)
#define LARGE_THRESHOLD (128u * 1024u)
typedef struct block {
    uint64_t magic;
    uint64_t size;      /* total bytes; bit 0 is INUSE for arena blocks */
    uint64_t prev_size;
    uint64_t kind;      /* 0 arena, 1 anonymous mapping */
    struct block *prev_free;
    struct block *next_free;
    uint64_t reserved[2];
} block_t;
#ifndef __TINYC__
_Static_assert(sizeof(block_t) == BLOCK_HEADER, "allocator header size");
#endif
static block_t *free_list;
static block_t *arena_tail;
static unsigned char *arena_start, *arena_end;
static size_t arena_bytes, mapped_bytes;
size_t pollikos_heap_bytes(void) { return arena_bytes + mapped_bytes; }

static size_t align16(size_t value) { return (value + 15u) & ~(size_t)15u; }
static size_t align_page(size_t value) { return (value + 4095u) & ~(size_t)4095u; }
static void *payload_of(block_t *block) { return (unsigned char *)block + BLOCK_HEADER; }
static block_t *header_of(void *memory) { return (block_t *)((unsigned char *)memory - BLOCK_HEADER); }
static void corrupt(const char *message) {
    (void)__pollikos_syscall3(USER_WRITE, 2, (uint64_t)(uintptr_t)message, strlen(message));
    (void)__pollikos_syscall1(USER_EXIT, 134);
    for (;;) {}
}
static void list_insert(block_t *block) {
    block->prev_free = NULL;
    block->next_free = free_list;
    if (free_list) free_list->prev_free = block;
    free_list = block;
}
static void list_remove(block_t *block) {
    if (block->prev_free) block->prev_free->next_free = block->next_free;
    else free_list = block->next_free;
    if (block->next_free) block->next_free->prev_free = block->prev_free;
    block->prev_free = block->next_free = NULL;
}
static block_t *find_fit(size_t need) {
    for (block_t *block = free_list; block; block = block->next_free)
        if ((block->size & ~UINT64_C(1)) >= need) return block;
    return NULL;
}
static block_t *arena_extend(size_t need) {
    if (!arena_start) {
        long base = __pollikos_syscall1(USER_BRK, 0);
        if (base < 0) return NULL;
        arena_start = arena_end = (unsigned char *)(uintptr_t)base;
    }
    size_t total = need > HEAP_EXTEND ? align16(need) : HEAP_EXTEND;
    if ((uintptr_t)arena_end > UINTPTR_MAX-total) return NULL;
    long result = __pollikos_syscall1(USER_BRK, (uint64_t)(uintptr_t)(arena_end+total));
    if (result < 0) return NULL;
    block_t *block = (block_t *)arena_end;
    block->magic = ARENA_MAGIC;
    block->size = total;
    block->prev_size = arena_tail ? (arena_tail->size & ~UINT64_C(1)) : 0;
    block->kind = 0;
    block->prev_free = block->next_free = NULL;
    block->reserved[0] = block->reserved[1] = 0;
    arena_end += total;
    arena_bytes += total;
    arena_tail = block;
    return block;
}
static void *large_allocate(size_t size) {
    size_t total = align_page(BLOCK_HEADER + size);
    if (total/4096u > USER_MMAP_MAX_PAGES) return NULL;
    long base = __pollikos_syscall2(USER_MMAP, (uint64_t)total, 0);
    if (base < 0) return NULL;
    block_t *block = (block_t *)(uintptr_t)base;
    block->magic = MAPPED_MAGIC;
    block->size = total;
    block->prev_size = 0;
    block->kind = 1;
    block->prev_free = block->next_free = NULL;
    block->reserved[0] = block->reserved[1] = 0;
    mapped_bytes += total;
    return payload_of(block);
}
void *malloc(size_t size) {
    if (size > SIZE_MAX-BLOCK_HEADER-15u) { errno = ENOMEM; return NULL; }
    size_t need = BLOCK_HEADER + align16(size ? size : 1u);
    if (need < BLOCK_MIN) need = BLOCK_MIN;
    if (size + BLOCK_HEADER >= LARGE_THRESHOLD) {
        void *large = large_allocate(size);
        if (large) return large;
        /* Requests beyond the bounded anonymous mapping fall back to brk. */
    }
    block_t *block = find_fit(need);
    if (block) list_remove(block);
    else { block = arena_extend(need); if (!block) { errno = ENOMEM; return NULL; } }
    if ((block->size & ~UINT64_C(1)) >= need+BLOCK_MIN) {
        block_t *rest = (block_t *)((unsigned char *)block + need);
        size_t rest_size = (block->size & ~UINT64_C(1)) - need;
        rest->magic = ARENA_MAGIC;
        rest->size = rest_size;
        rest->prev_size = need;
        rest->kind = 0;
        rest->prev_free = rest->next_free = NULL;
        rest->reserved[0] = rest->reserved[1] = 0;
        block->size = need;
        unsigned char *after = (unsigned char *)rest + rest_size;
        if (after < arena_end) ((block_t *)after)->prev_size = rest_size;
        if (arena_tail == block) arena_tail = rest;
        list_insert(rest);
    }
    block->size |= 1;
    return payload_of(block);
}
static void arena_release(block_t *block) {
    size_t size = block->size & ~UINT64_C(1);
    block->size = size;
    unsigned char *next_address = (unsigned char *)block + size;
    if (next_address < arena_end) {
        block_t *next = (block_t *)next_address;
        if (next->magic != ARENA_MAGIC) corrupt("pollikc: heap corruption (next)\n");
        next->prev_size = size;
        if (!(next->size & 1)) {
            list_remove(next);
            size += next->size & ~UINT64_C(1);
            block->size = size;
            next_address = (unsigned char *)block + size;
            if (next_address < arena_end) ((block_t *)next_address)->prev_size = size;
            if (arena_tail == next) arena_tail = block;
        }
    }
    if (block->prev_size) {
        block_t *previous = (block_t *)((unsigned char *)block - block->prev_size);
        if (previous->magic == ARENA_MAGIC && !(previous->size & 1)) {
            list_remove(previous);
            previous->size += size;
            size = previous->size;
            next_address = (unsigned char *)previous + size;
            if (next_address < arena_end) ((block_t *)next_address)->prev_size = size;
            if (arena_tail == block) arena_tail = previous;
            block = previous;
        }
    }
    list_insert(block);
}
void free(void *memory) {
    if (!memory) return;
    block_t *block = header_of(memory);
    if (block->magic == MAPPED_MAGIC) {
        size_t total = block->size;
        long result = __pollikos_syscall2(USER_MUNMAP, (uint64_t)(uintptr_t)block, total);
        if (result < 0) corrupt("pollikc: anonymous release failed\n");
        return;
    }
    if (block->magic != ARENA_MAGIC) corrupt("pollikc: invalid free\n");
    if (!(block->size & 1)) corrupt("pollikc: double free\n");
    arena_release(block);
}
void *calloc(size_t count, size_t size) {
    if (count && size > SIZE_MAX/count) { errno = ENOMEM; return NULL; }
    size_t bytes = count*size;
    void *memory = malloc(bytes);
    if (memory) memset(memory, 0, bytes ? bytes : 1u);
    return memory;
}
void *realloc(void *memory, size_t size) {
    if (!memory) return malloc(size);
    if (!size) { free(memory); return NULL; }
    block_t *block = header_of(memory);
    if (block->magic == MAPPED_MAGIC) {
        size_t capacity = (size_t)block->size - BLOCK_HEADER;
        if (size <= capacity) return memory;
        void *fresh = malloc(size);
        if (!fresh) return NULL;
        memcpy(fresh, memory, capacity);
        free(memory);
        return fresh;
    }
    if (block->magic != ARENA_MAGIC) corrupt("pollikc: invalid realloc\n");
    size_t capacity = (block->size & ~UINT64_C(1)) - BLOCK_HEADER;
    if (size <= capacity) {
        size_t need = BLOCK_HEADER + align16(size);
        if (need < BLOCK_MIN) need = BLOCK_MIN;
        if (capacity + BLOCK_HEADER >= need+BLOCK_MIN) {
            size_t rest_size = (block->size & ~UINT64_C(1)) - need;
            block_t *rest = (block_t *)((unsigned char *)block + need);
            rest->magic = ARENA_MAGIC;
            rest->size = rest_size;
            rest->prev_size = need;
            rest->kind = 0;
            rest->prev_free = rest->next_free = NULL;
            rest->reserved[0] = rest->reserved[1] = 0;
            block->size = need | 1;
            unsigned char *after = (unsigned char *)rest + rest_size;
            if (after < arena_end) ((block_t *)after)->prev_size = rest_size;
            if (arena_tail == block) arena_tail = rest;
            list_insert(rest);
        }
        return memory;
    }
    void *fresh = malloc(size);
    if (!fresh) return NULL;
    memcpy(fresh, memory, capacity);
    free(memory);
    return fresh;
}
