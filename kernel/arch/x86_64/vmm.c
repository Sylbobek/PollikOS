#include "paging.h"
#include "user.h"
extern uint64_t boot_pt[512];
static AddressSpace kernel_space;
static phys_addr_t active_root;

static unsigned index_at(virt_addr_t address, unsigned level) {
    return (unsigned)((address >> (12+9*level)) & 511);
}
static uint64_t entry_read(phys_addr_t table, unsigned index) {
    return physical_view(table)[index];
}
static void entry_write(phys_addr_t table, unsigned index, uint64_t value) {
    physical_view(table)[index] = value;
}
static phys_addr_t table_allocate(void) {
    phys_addr_t frame = pmm64_alloc(PMM_NORMAL);
    if (frame) {
        memory_require(pmm64_claim(frame), "claim table frame");
        physical_zero(frame);
    }
    return frame;
}
static void frame_release(phys_addr_t frame) {
    memory_require(pmm64_free_owned(frame), "release VMM-owned frame");
}
static int canonical(virt_addr_t address) {
    return address < MM_USER_END || address >= UINT64_C(0xffff800000000000);
}
VmResult vmm64_user_page(const AddressSpace *space, virt_addr_t address, unsigned required, Mapping *out) {
    if (!space || !space->root || space->root == kernel_space.root || !out ||
        address < MM_USER_START || address >= MM_USER_END || (required & ~(VM_WRITE|VM_EXEC)))
        return VM_INVALID;
    phys_addr_t table = space->root;
    for (int level = 3; level >= 0; --level) {
        uint64_t entry = entry_read(table, index_at(address, (unsigned)level));
        if (!(entry & PTE_PRESENT) || !(entry & PTE_USER) ||
            ((required & VM_WRITE) && !(entry & PTE_WRITE)) ||
            ((required & VM_EXEC) && (entry & PTE_NX)) || (level && (entry & PTE_HUGE)))
            return VM_INVALID;
        table = entry & PTE_ADDRESS;
    }
    return vmm64_lookup(space, address, out);
}
static int allowed(const AddressSpace *space, virt_addr_t address, unsigned flags) {
    if (!space || !space->root || (address & 4095) || !canonical(address) ||
        (flags & ~(VM_WRITE|VM_USER|VM_EXEC|VM_DEVICE)) ||
        ((flags & VM_WRITE) && (flags & VM_EXEC)) ||
        ((flags & VM_DEVICE) && (flags & (VM_USER|VM_EXEC)))) return 0;
    if (address >= MM_KERNEL_START)
        return space->root == kernel_space.root && !(flags & VM_USER);
    return address >= MM_USER_START && address < MM_USER_END &&
           space->root != kernel_space.root && (flags & VM_USER);
}
static uint64_t leaf_flags(unsigned flags) {
    return PTE_PRESENT | ((flags & VM_WRITE) ? PTE_WRITE : 0) |
           ((flags & VM_USER) ? PTE_USER : 0) | ((flags & VM_EXEC) ? 0 : PTE_NX) |
           ((flags & VM_DEVICE) ? PTE_PCD|PTE_PWT : 0);
}
static void changed(const AddressSpace *space, virt_addr_t address) {
    if (space->root == active_root || address >= MM_KERNEL_START) invalidate(address);
    /* Inactive roots have no retained translations: CR3 switches use no PCID. */
}
int vmm64_init(void) {
    if (kernel_space.root) return 0;
    phys_addr_t frames[5] = {0};
    unsigned count = 0;
    for (; count < 5; ++count) {
        frames[count] = table_allocate();
        if (!frames[count]) {
            while (count) frame_release(frames[--count]);
            return 0;
        }
    }
    /* Preserve boot identity protections but replace every level with PMM frames.
     * Slot 0 is shared supervisor-only; slot 511 anchors shared kernel stacks. */
    for (unsigned i = 0; i < 510; ++i) entry_write(frames[3], i, boot_pt[i]);
    entry_write(frames[3], 510, frames[3] | PTE_PRESENT | PTE_WRITE | PTE_NX);
    entry_write(frames[2], 0, frames[3] | PTE_PRESENT | PTE_WRITE);
    entry_write(frames[1], 0, frames[2] | PTE_PRESENT | PTE_WRITE);
    entry_write(frames[0], 0, frames[1] | PTE_PRESENT | PTE_WRITE);
    entry_write(frames[0], 511, frames[4] | PTE_PRESENT | PTE_WRITE);
    kernel_space.root = frames[0];
    return vmm64_switch(&kernel_space) == VM_OK;
}
AddressSpace *vmm64_kernel(void) { return &kernel_space; }
VmResult vmm64_create(AddressSpace *space) {
    if (!space || space->root || !kernel_space.root) return VM_INVALID;
    phys_addr_t root = table_allocate();
    if (!root) return VM_NOMEM;
    uint64_t low = entry_read(kernel_space.root, 0);
    uint64_t high = entry_read(kernel_space.root, 511);
    entry_write(root, 0, low);
    entry_write(root, 511, high);
    space->root = root;
    return VM_OK;
}
VmResult vmm64_switch(const AddressSpace *space) {
    memory_context_check();
    if (!space || !space->root) return VM_INVALID;
    if (active_root == space->root) return VM_OK;
    /* PCIDE/PGE are not enabled in this target. CR3 reload drops old user TLBs. */
    hal_write_cr3((unsigned long)space->root);
    active_root = space->root;
    return VM_OK;
}

/* Attach tables transactionally. Failure removes only tables created by this
 * call, in reverse order; pre-existing ancestors and mappings stay intact. */
static VmResult install(AddressSpace *space, virt_addr_t address, phys_addr_t frame,
                         unsigned flags, int owned, int guard) {
    if (!allowed(space, address, flags) || (frame & 4095) || frame >= physical_limit()) return VM_INVALID;
    if (!guard && !frame) return VM_INVALID;
    phys_addr_t parents[3], children[3];
    unsigned indices[3], created = 0;
    phys_addr_t table = space->root;
    VmResult error = VM_OK;
    for (unsigned level = 3; level; --level) {
        unsigned index = index_at(address, level);
        uint64_t entry = entry_read(table, index);
        if (!entry) {
            phys_addr_t child = table_allocate();
            if (!child) { error = VM_NOMEM; goto rollback; }
            parents[created] = table; children[created] = child; indices[created++] = index;
            entry = child | PTE_PRESENT | PTE_WRITE | ((flags & VM_USER) ? PTE_USER : 0);
            entry_write(table, index, entry);
        } else if (!(entry & PTE_PRESENT) || (entry & PTE_HUGE) ||
                   ((flags & VM_USER) && !(entry & PTE_USER))) {
            error = VM_INVALID; goto rollback;
        }
        table = entry & PTE_ADDRESS;
    }
    unsigned index = index_at(address, 0);
    if (entry_read(table, index)) { error = VM_EXISTS; goto rollback; }
    if (owned && !pmm64_claim(frame)) { error = VM_INVALID; goto rollback; }
    entry_write(table, index, guard ? PTE_GUARD : frame | leaf_flags(flags) | (owned ? PTE_OWNED : 0));
    changed(space, address);
    return VM_OK;
rollback:
    while (created) {
        --created;
        entry_write(parents[created], indices[created], 0);
        changed(space, address);
        frame_release(children[created]);
    }
    return error;
}
VmResult vmm64_map_borrowed(AddressSpace *space, virt_addr_t address, phys_addr_t frame, unsigned flags) {
    if ((flags & VM_USER) && !pmm64_is_allocated(frame)) return VM_INVALID;
    return install(space, address, frame, flags, 0, 0);
}
VmResult vmm64_alloc_page(AddressSpace *space, virt_addr_t address, unsigned flags) {
    if (!allowed(space, address, flags) || (flags & VM_DEVICE)) return VM_INVALID;
    phys_addr_t frame = pmm64_alloc(PMM_NORMAL);
    if (!frame) return VM_NOMEM;
    physical_zero(frame);
    VmResult result = install(space, address, frame, flags, 1, 0);
    if (result != VM_OK) memory_require(pmm64_free(frame), "rollback data frame");
    return result;
}
static VmResult walk(const AddressSpace *space, virt_addr_t address, phys_addr_t tables[4], uint64_t *leaf) {
    if (!space || !space->root || !canonical(address)) return VM_INVALID;
    tables[3] = space->root;
    for (unsigned level = 3; level; --level) {
        uint64_t entry = entry_read(tables[level], index_at(address, level));
        if (!(entry & PTE_PRESENT)) return VM_MISSING;
        if (entry & PTE_HUGE) return VM_INVALID;
        tables[level-1] = entry & PTE_ADDRESS;
    }
    *leaf = entry_read(tables[0], index_at(address, 0));
    return *leaf ? VM_OK : VM_MISSING;
}
VmResult vmm64_lookup(const AddressSpace *space, virt_addr_t address, Mapping *out) {
    if (!out) return VM_INVALID;
    phys_addr_t tables[4]; uint64_t entry;
    VmResult result = walk(space, address, tables, &entry);
    if (result != VM_OK) return result;
    *out = (Mapping){entry & PTE_ADDRESS,
        ((entry & PTE_WRITE) ? VM_WRITE : 0) | ((entry & PTE_USER) ? VM_USER : 0) |
        ((entry & PTE_NX) ? 0 : VM_EXEC) | ((entry & PTE_PCD) ? VM_DEVICE : 0),
        !!(entry & PTE_OWNED), !!(entry & PTE_GUARD)};
    if (out->guard) out->flags = 0;
    return VM_OK;
}
static int table_empty(phys_addr_t table) {
    volatile uint64_t *entries = physical_view(table);
    for (unsigned i = 0; i < 512; ++i) if (entries[i]) return 0;
    return 1;
}
static VmResult remove_mapping(AddressSpace *space, virt_addr_t address, int release,
                                phys_addr_t *borrowed, int guard) {
    unsigned flags = address >= MM_KERNEL_START ? 0 : VM_USER;
    if (!allowed(space, address, flags) || (release != 0 && release != 1)) return VM_INVALID;
    phys_addr_t tables[4]; uint64_t entry;
    VmResult result = walk(space, address, tables, &entry);
    if (result != VM_OK) return result;
    if (!!(entry & PTE_GUARD) != guard) return VM_INVALID;
    if (!guard && (!!(entry & PTE_OWNED) != release)) return VM_INVALID;
    entry_write(tables[0], index_at(address, 0), 0);
    changed(space, address);
    if (release) frame_release(entry & PTE_ADDRESS);
    else if (borrowed) *borrowed = entry & PTE_ADDRESS;
    /* Keep the shared kernel PDPT permanently anchored in every root. */
    unsigned limit = address >= MM_KERNEL_START ? 2 : 3;
    for (unsigned level = 0; level < limit; ++level) {
        if (!table_empty(tables[level])) break;
        entry_write(tables[level+1], index_at(address, level+1), 0);
        changed(space, address);
        frame_release(tables[level]);
    }
    return VM_OK;
}
VmResult vmm64_unmap(AddressSpace *space, virt_addr_t address, int release, phys_addr_t *borrowed) {
    return remove_mapping(space, address, release, borrowed, 0);
}
static void destroy_table(phys_addr_t table, unsigned level) {
    for (unsigned i = 0; i < 512; ++i) {
        uint64_t entry = entry_read(table, i);
        if (!entry) continue;
        if (level) {
            memory_require((entry & PTE_PRESENT) && !(entry & PTE_HUGE), "invalid owned page table");
            destroy_table(entry & PTE_ADDRESS, level-1);
        } else if (entry & PTE_OWNED) frame_release(entry & PTE_ADDRESS);
    }
    frame_release(table);
}
VmResult vmm64_destroy(AddressSpace *space) {
    if (!space || !space->root) return VM_INVALID;
    if (space->root == active_root || space->root == kernel_space.root) return VM_BUSY;
    for (unsigned i = 1; i < 256; ++i) {
        uint64_t entry = entry_read(space->root, i);
        if (entry) destroy_table(entry & PTE_ADDRESS, 2);
    }
    frame_release(space->root);
    space->root = 0;
    return VM_OK;
}
VmResult vmm64_stack_create(AddressSpace *space, virt_addr_t base, size_t pages,
                            size_t guards, int user, GuardedStack *stack) {
    if (!stack || stack->pages || !pages || pages > 256 || !guards || guards > 16 ||
        base > UINTPTR_MAX-(pages+guards)*MM_PAGE_SIZE) return VM_INVALID;
    unsigned flags = VM_WRITE | (user ? VM_USER : 0);
    for (size_t i = 0; i < pages+guards; ++i) {
        virt_addr_t address = base+i*MM_PAGE_SIZE;
        Mapping mapping;
        if (!allowed(space, address, flags)) return VM_INVALID;
        VmResult result = vmm64_lookup(space, address, &mapping);
        if (result != VM_MISSING) return result == VM_OK ? VM_EXISTS : result;
    }
    size_t done = 0;
    VmResult result = VM_OK;
    for (; done < pages+guards; ++done) {
        virt_addr_t address = base+done*MM_PAGE_SIZE;
        result = done < guards ? install(space, address, 0, flags, 0, 1) :
                                vmm64_alloc_page(space, address, flags);
        if (result != VM_OK) break;
    }
    if (result != VM_OK) {
        while (done) {
            --done;
            memory_require(remove_mapping(space, base+done*MM_PAGE_SIZE, done >= guards,
                                          0, done < guards) == VM_OK, "stack rollback");
        }
        return result;
    }
    *stack = (GuardedStack){base, base+(pages+guards)*MM_PAGE_SIZE, pages, guards};
    return VM_OK;
}
VmResult vmm64_stack_destroy(AddressSpace *space, GuardedStack *stack) {
    if (!stack || !stack->pages || stack->pages > 256 || !stack->guards || stack->guards > 16 ||
        stack->base > UINTPTR_MAX-(stack->pages+stack->guards)*MM_PAGE_SIZE ||
        stack->top != stack->base+(stack->pages+stack->guards)*MM_PAGE_SIZE) return VM_INVALID;
    /* Caller must not destroy a stack currently used by a CPU/TSS. Preflight
     * the complete range so invalid handles cannot cause partial destruction. */
    for (size_t i = 0; i < stack->guards+stack->pages; ++i) {
        virt_addr_t address = stack->base+i*MM_PAGE_SIZE;
        Mapping mapping;
        if (!allowed(space, address, address >= MM_KERNEL_START ? 0 : VM_USER) ||
            vmm64_lookup(space, address, &mapping) != VM_OK ||
            (i < stack->guards ? !mapping.guard : !mapping.owned)) return VM_INVALID;
    }
    for (size_t i = 0; i < stack->guards+stack->pages; ++i)
        memory_require(remove_mapping(space, stack->base+i*MM_PAGE_SIZE, i >= stack->guards,
                                       0, i < stack->guards) == VM_OK, "stack destruction");
    *stack = (GuardedStack){0};
    return VM_OK;
}
