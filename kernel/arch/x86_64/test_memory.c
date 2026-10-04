#include "test_memory.h"
#include "paging.h"

#define TEST_BUFFER_BASE (MM_KERNEL_START + UINT64_C(0x08000000))
#define TEST_BUFFER_LIMIT (MM_KERNEL_START + UINT64_C(0x09000000))
#define TEST_BUFFER_MAGIC UINT64_C(0x504f4c4c494b5442)

typedef struct {
    virt_addr_t previous;
    size_t pages;
    uint64_t magic;
} TestBufferHeader;

static virt_addr_t cursor = TEST_BUFFER_BASE;

void *kernel_test_buffer_alloc(size_t size) {
    memory_context_check();
    if (!size || size > SIZE_MAX - sizeof(TestBufferHeader) - (MM_PAGE_SIZE-1))
        return 0;
    size_t pages = (size + sizeof(TestBufferHeader) + MM_PAGE_SIZE-1) / MM_PAGE_SIZE;
    if (pages > (TEST_BUFFER_LIMIT-cursor)/MM_PAGE_SIZE)
        return 0;
    virt_addr_t base = cursor;
    for (size_t i = 0; i < pages; ++i) {
        if (vmm64_alloc_page(vmm64_kernel(), base+i*MM_PAGE_SIZE, VM_WRITE) == VM_OK)
            continue;
        while (i) {
            --i;
            memory_require(vmm64_unmap(vmm64_kernel(), base+i*MM_PAGE_SIZE, 1, 0) == VM_OK,
                           "test buffer allocation rollback");
        }
        return 0;
    }
    TestBufferHeader *header = (TestBufferHeader *)(uintptr_t)base;
    header->previous = cursor;
    header->pages = pages;
    header->magic = TEST_BUFFER_MAGIC;
    cursor += pages*MM_PAGE_SIZE;
    return header+1;
}

void kernel_test_buffer_free(void *buffer) {
    memory_context_check();
    if (!buffer) return;
    TestBufferHeader *header = (TestBufferHeader *)buffer - 1;
    virt_addr_t base = (virt_addr_t)(uintptr_t)header;
    memory_require(base >= TEST_BUFFER_BASE && header->magic == TEST_BUFFER_MAGIC &&
                   header->pages && base+header->pages*MM_PAGE_SIZE == cursor,
                   "test buffers released in reverse allocation order");
    size_t pages = header->pages;
    virt_addr_t previous = header->previous;
    while (pages) {
        --pages;
        memory_require(vmm64_unmap(vmm64_kernel(), base+pages*MM_PAGE_SIZE, 1, 0) == VM_OK,
                       "test buffer release");
    }
    cursor = previous;
}
