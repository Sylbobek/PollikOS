#include "net_platform.h"
#include "paging.h"
#define TCP64_BUFFER_BASE (MM_KERNEL_START+UINT64_C(0x3a000000))
#define TCP64_BUFFER_BYTES 32768u
#define TCP64_BUFFER_SLOTS 8u
static unsigned char tcp_buffers[TCP64_BUFFER_SLOTS];
void *tcp64_buffer_alloc(unsigned slot,unsigned bytes) {
    memory_context_check();
    if(slot>=TCP64_BUFFER_SLOTS || bytes!=TCP64_BUFFER_BYTES || tcp_buffers[slot])return NULL;
    virt_addr_t base=TCP64_BUFFER_BASE+(uint64_t)slot*TCP64_BUFFER_BYTES;
    unsigned pages=0;
    for(;pages<TCP64_BUFFER_BYTES/MM_PAGE_SIZE;pages++) {
        if(vmm64_alloc_page(vmm64_kernel(),base+(uint64_t)pages*MM_PAGE_SIZE,VM_WRITE)!=VM_OK)break;
    }
    if(pages!=TCP64_BUFFER_BYTES/MM_PAGE_SIZE) {
        while(pages){pages--;memory_require(vmm64_unmap(vmm64_kernel(),base+(uint64_t)pages*MM_PAGE_SIZE,1,0)==VM_OK,"TCP buffer allocation rollback");}
        return NULL;
    }
    tcp_buffers[slot]=1;return (void *)(uintptr_t)base;
}
void tcp64_buffer_free(void *buffer) {
    memory_context_check();if(!buffer)return;
    virt_addr_t base=(virt_addr_t)(uintptr_t)buffer;
    memory_require(base>=TCP64_BUFFER_BASE && base<TCP64_BUFFER_BASE+TCP64_BUFFER_SLOTS*TCP64_BUFFER_BYTES &&
        (base-TCP64_BUFFER_BASE)%TCP64_BUFFER_BYTES==0,"TCP buffer ownership");
    unsigned slot=(unsigned)((base-TCP64_BUFFER_BASE)/TCP64_BUFFER_BYTES);
    memory_require(tcp_buffers[slot],"TCP buffer single release");
    for(unsigned i=0;i<TCP64_BUFFER_BYTES/MM_PAGE_SIZE;i++)memory_require(vmm64_unmap(vmm64_kernel(),base+(uint64_t)i*MM_PAGE_SIZE,1,0)==VM_OK,"TCP buffer release");
    tcp_buffers[slot]=0;
}

void serial(const char *text) { memory_log(text); }

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

int memcmp(const void *left, const void *right, size_t size) {
    const unsigned char *a = left, *b = right;
    for (size_t i = 0; i < size; ++i) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

void number(char *out, u32 value) {
    char reverse[10];
    unsigned count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < sizeof(reverse));
    unsigned at = 0;
    while (count) out[at++] = reverse[--count];
    out[at] = 0;
}
