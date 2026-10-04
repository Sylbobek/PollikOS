#include "user.h"
#include "paging.h"
UserCopyResult user_range_check(const AddressSpace *space, virt_addr_t address, size_t length, int write) {
    memory_context_check();
    if (!space || !space->root || space->root == vmm64_kernel()->root ||
        address < MM_USER_START || address >= MM_USER_END ||
        length > MM_USER_END-address) return USER_COPY_FAULT;
    /* Zero length accepts only an in-range canonical pointer, without touching
     * it. No address+length or page-round-up can wrap after the subtraction test. */
    if (!length) return USER_COPY_OK;
    virt_addr_t last = (address+length-1) & ~(MM_PAGE_SIZE-1);
    for (virt_addr_t page = address & ~(MM_PAGE_SIZE-1);;) {
        Mapping mapping;
        if (vmm64_user_page(space, page, write ? VM_WRITE : 0, &mapping) != VM_OK)
            return USER_COPY_FAULT;
        if (page == last) break;
        page += MM_PAGE_SIZE;
    }
    return USER_COPY_OK;
}
static UserCopyResult copy(const AddressSpace *space, virt_addr_t user, void *kernel, size_t length, int to_user) {
    if ((!kernel && length) || user_range_check(space, user, length, to_user) != USER_COPY_OK)
        return USER_COPY_FAULT;
    /* Full prevalidation guarantees no partial destination write on EFAULT.
     * IF=0 and a single BSP make validation/copy atomic against VMM mutation.
     * Access via the supervisor aperture also works for inactive address spaces. */
    uint8_t *buffer = kernel;
    while (length) {
        Mapping mapping;
        memory_require(vmm64_user_page(space, user, to_user ? VM_WRITE : 0, &mapping) == VM_OK,
                       "user mapping changed during serialized copy");
        size_t offset = user & (MM_PAGE_SIZE-1), chunk = MM_PAGE_SIZE-offset;
        if (chunk > length) chunk = length;
        volatile uint8_t *page = (volatile uint8_t *)physical_view(mapping.physical);
        for (size_t i = 0; i < chunk; ++i) {
            if (to_user) page[offset+i] = buffer[i];
            else buffer[i] = page[offset+i];
        }
        user += chunk; buffer += chunk; length -= chunk;
    }
    return USER_COPY_OK;
}
UserCopyResult copy_from_user64(const AddressSpace *space, void *destination, virt_addr_t source, size_t length) {
    return copy(space, source, destination, length, 0);
}
UserCopyResult copy_to_user64(const AddressSpace *space, virt_addr_t destination, const void *source, size_t length) {
    return copy(space, destination, (void *)source, length, 1);
}
UserCopyResult copy_string_from_user64(const AddressSpace *space, char *destination,
                                      virt_addr_t source, size_t maximum) {
    if (!destination || !maximum) return USER_COPY_TOO_LONG;
    destination[0] = 0;
    if (source < MM_USER_START || source >= MM_USER_END || maximum > MM_USER_END-source)
        return USER_COPY_FAULT;
    /* Find NUL within the explicit capacity before copying anything. Pages
     * beyond the terminator need not exist; a fault/unterminated string leaves
     * an empty destination rather than a misleading partial string. */
    size_t used = 0;
    while (used < maximum) {
        Mapping mapping;
        if (vmm64_user_page(space, source+used, 0, &mapping) != VM_OK) return USER_COPY_FAULT;
        size_t offset = (source+used) & 4095, chunk = 4096-offset;
        if (chunk > maximum-used) chunk = maximum-used;
        volatile uint8_t *page = (volatile uint8_t *)physical_view(mapping.physical);
        for (size_t i = 0; i < chunk; ++i)
            if (!page[offset+i]) return copy_from_user64(space, destination, source, used+i+1);
        used += chunk;
    }
    return USER_COPY_TOO_LONG;
}
