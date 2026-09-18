#include "mem.h"
#include <stdint.h>

extern u8 __bss_end[];

#define HEAP_MAGIC 0x4d454d48u /* "MEMH" */
#define HEAP_MAX   0x7f0000u   /* Stop before user process space at 0x800000 */

typedef struct BlockHeader {
    u32 size;                  /* Bit 0: 1 = allocated, 0 = free */
    struct BlockHeader *next;  /* Next block in physical memory */
    struct BlockHeader *prev;  /* Previous block in physical memory */
    u32 magic;
} BlockHeader;

#define BLOCK_OVERHEAD sizeof(BlockHeader)
#define BLOCK_ALLOCATED 1u

static BlockHeader *heap_head = 0;
static u32 heap_start_addr = 0;
static u32 heap_end_addr = 0;
static u32 bytes_allocated = 0;

void mem_init(void) {
    uintptr_t start = ((uintptr_t)__bss_end + 4095u) & ~(uintptr_t)4095u;
    if (start >= HEAP_MAX)
        return;
    heap_start_addr = (u32)start;
    heap_end_addr = HEAP_MAX;

    heap_head = (BlockHeader *)(uintptr_t)heap_start_addr;
    heap_head->size = (heap_end_addr - heap_start_addr - BLOCK_OVERHEAD) & ~BLOCK_ALLOCATED;
    heap_head->next = 0;
    heap_head->prev = 0;
    heap_head->magic = HEAP_MAGIC;
    bytes_allocated = 0;
    serial("MEM heap initialized\n");
}

void *kmalloc(u32 size) {
    if (!heap_head)
        mem_init();
    if (!heap_head || size > HEAP_MAX - 7u || size == 0)
        return 0;

    /* Align size to 8 bytes, minimum 16 bytes */
    size = (size + 7u) & ~7u;
    if (size < 16u)
        size = 16u;

    BlockHeader *curr = heap_head;
    while (curr) {
        if (!(curr->size & BLOCK_ALLOCATED) && curr->size >= size) {
            /* Can we split? */
            if (curr->size >= size + BLOCK_OVERHEAD + 16u) {
                u32 remaining = curr->size - size - BLOCK_OVERHEAD;
                BlockHeader *new_block = (BlockHeader *)((u8 *)curr + BLOCK_OVERHEAD + size);
                new_block->size = remaining & ~BLOCK_ALLOCATED;
                new_block->next = curr->next;
                new_block->prev = curr;
                new_block->magic = HEAP_MAGIC;

                if (curr->next)
                    curr->next->prev = new_block;
                curr->next = new_block;
                curr->size = size;
            }
            curr->size |= BLOCK_ALLOCATED;
            bytes_allocated += (curr->size & ~BLOCK_ALLOCATED);
            return (void *)((u8 *)curr + BLOCK_OVERHEAD);
        }
        curr = curr->next;
    }
    serial("MEM kmalloc out of memory\n");
    return 0;
}

void kfree(void *ptr) {
    if (!ptr)
        return;
    BlockHeader *block = (BlockHeader *)((u8 *)ptr - BLOCK_OVERHEAD);
    if (block->magic != HEAP_MAGIC) {
        serial("MEM kfree corrupt block\n");
        return;
    }
    if (!(block->size & BLOCK_ALLOCATED))
        return;

    u32 actual_size = block->size & ~BLOCK_ALLOCATED;
    if (bytes_allocated >= actual_size)
        bytes_allocated -= actual_size;
    block->size = actual_size;

    /* Coalesce with next block if free */
    if (block->next && !(block->next->size & BLOCK_ALLOCATED)) {
        block->size += BLOCK_OVERHEAD + block->next->size;
        block->next = block->next->next;
        if (block->next)
            block->next->prev = block;
    }

    /* Coalesce with prev block if free */
    if (block->prev && !(block->prev->size & BLOCK_ALLOCATED)) {
        block->prev->size += BLOCK_OVERHEAD + block->size;
        block->prev->next = block->next;
        if (block->next)
            block->next->prev = block->prev;
    }
}

void *kcalloc(u32 n, u32 size) {
    if (size && n > 0xffffffffu / size) return 0;
    u32 total = n * size;
    void *p = kmalloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

void *krealloc(void *ptr, u32 new_size) {
    if (!ptr)
        return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return 0;
    }
    BlockHeader *block = (BlockHeader *)((u8 *)ptr - BLOCK_OVERHEAD);
    if (block->magic != HEAP_MAGIC)
        return 0;
    u32 old_size = block->size & ~BLOCK_ALLOCATED;
    if (old_size >= new_size)
        return ptr;

    void *new_p = kmalloc(new_size);
    if (!new_p)
        return 0;
    memcpy(new_p, ptr, old_size);
    kfree(ptr);
    return new_p;
}

u32 mem_get_used(void) {
    return bytes_allocated;
}

u32 mem_get_free(void) {
    if (heap_end_addr > heap_start_addr) {
        u32 total = heap_end_addr - heap_start_addr;
        return total > bytes_allocated ? total - bytes_allocated : 0;
    }
    return 0;
}

void *memmove(void *dest, const void *src, unsigned n) {
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, unsigned n) {
    const u8 *a = (const u8 *)s1;
    const u8 *b = (const u8 *)s2;
    while (n--) {
        if (*a != *b)
            return (int)*a - (int)*b;
        a++;
        b++;
    }
    return 0;
}

unsigned strlen(const char *s) {
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

