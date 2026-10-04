#include "elf64.h"
typedef struct {
    Elf64ProgramHeader segments[ELF64_MAX_HEADERS];
    size_t count;
    virt_addr_t entry;
} LoadPlan;
static void read_bytes(void *out, const void *in, size_t bytes) {
    uint8_t *to = out; const uint8_t *from = in;
    for (size_t i = 0; i < bytes; ++i) ((volatile uint8_t *)to)[i] = from[i];
}
static uint64_t page_start(uint64_t value) { return value & ~(MM_PAGE_SIZE-1); }
static uint64_t page_end(const Elf64ProgramHeader *p) { return (p->vaddr+p->memsz+4095) & ~UINT64_C(4095); }
static Elf64Result plan_image(const void *image, size_t size, LoadPlan *plan) {
    if (!image || size < sizeof(Elf64Header)) return ELF64_FORMAT;
    if (size > ELF64_MAX_IMAGE) return ELF64_LIMIT;
    Elf64Header header;
    read_bytes(&header, image, sizeof(header));
    if (header.ident[0] != 0x7f || header.ident[1] != 'E' || header.ident[2] != 'L' || header.ident[3] != 'F' ||
        header.ident[4] != 2 || header.ident[5] != 1 || header.ident[6] != 1 ||
        header.version != 1 || header.machine != 62 || header.ehsize != 64 || header.phentsize != 56)
        return ELF64_FORMAT;
    if (header.type != 2 || header.ident[7] != 0 || header.ident[8] != 0 || header.flags) return ELF64_UNSUPPORTED;
    if (!header.phnum || header.phnum > ELF64_MAX_HEADERS) return ELF64_LIMIT;
    if (header.phoff < sizeof(header) || header.phoff > size ||
        (size_t)header.phnum*56 > size-header.phoff) return ELF64_RANGE;
    plan->count = 0; plan->entry = header.entry;
    uint64_t pages = 0;
    int executable_entry = 0;
    for (size_t i = 0; i < header.phnum; ++i) {
        Elf64ProgramHeader segment;
        read_bytes(&segment, (const uint8_t *)image+header.phoff+i*56, sizeof(segment));
        if (segment.type == 0) continue;
        if (segment.type != 1) return ELF64_UNSUPPORTED; /* fail closed: no INTERP/DYNAMIC/TLS */
        if ((segment.flags & ~7u) || !(segment.flags & 4) || (segment.flags & 3) == 3)
            return ELF64_UNSUPPORTED; /* readable required; W^X mandatory */
        if (!segment.memsz || segment.filesz > segment.memsz || segment.offset > size ||
            segment.filesz > size-segment.offset) return ELF64_RANGE;
        /* Executable image arena ends before the entire reserved stack region.
         * Subtraction bounds both memsz addition and later page rounding. */
        if (segment.vaddr < MM_USER_START || segment.vaddr >= USER_STACK_BASE ||
            segment.memsz > USER_STACK_BASE-segment.vaddr) return ELF64_RANGE;
        if (segment.align > 0x200000 ||
            (segment.align > 1 && ((segment.align & (segment.align-1)) ||
             (segment.vaddr & (segment.align-1)) != (segment.offset & (segment.align-1)))) ||
            (segment.vaddr & 4095) != (segment.offset & 4095)) return ELF64_ALIGNMENT;
        uint64_t first = page_start(segment.vaddr), last = page_end(&segment);
        /* Reserved runtime regions: heap and anonymous mappings are process
         * owned and must never be preempted by an ELF segment. */
        if (last > USER_MMAP_BASE && first < USER_HEAP_LIMIT) return ELF64_RANGE;
        pages += (last-first)/4096;
        if (pages > ELF64_MAX_PAGES) return ELF64_LIMIT;
        for (size_t j = 0; j < plan->count; ++j) {
            if (first < page_end(&plan->segments[j]) && page_start(plan->segments[j].vaddr) < last)
                return ELF64_OVERLAP; /* even shared partial pages are rejected */
        }
        if ((segment.flags & 1) && header.entry >= segment.vaddr &&
            header.entry-segment.vaddr < segment.filesz) executable_entry = 1;
        read_bytes(&plan->segments[plan->count++], &segment, sizeof(segment));
    }
    return plan->count && executable_entry ? ELF64_OK : ELF64_ENTRY;
}
Elf64Result elf64_validate(const void *image, size_t size) {
    LoadPlan plan;
    return plan_image(image, size, &plan);
}
Elf64Result elf64_load(AddressSpace *space, const void *image, size_t size, virt_addr_t *entry) {
    if (!space || !space->root || !entry || space->root == vmm64_kernel()->root) return ELF64_FORMAT;
    LoadPlan plan;
    Elf64Result result = plan_image(image, size, &plan);
    if (result != ELF64_OK) return result;
    for (size_t i = 0; i < plan.count; ++i)
        for (uint64_t address = page_start(plan.segments[i].vaddr); address < page_end(&plan.segments[i]); address += 4096) {
            Mapping mapping;
            if (vmm64_lookup(space, address, &mapping) != VM_MISSING) return ELF64_CONFLICT;
        }
    size_t installed[ELF64_MAX_HEADERS] = {0};
    for (size_t i = 0; i < plan.count; ++i) {
        Elf64ProgramHeader *p = &plan.segments[i];
        unsigned flags = VM_USER | ((p->flags & 2) ? VM_WRITE : 0) | ((p->flags & 1) ? VM_EXEC : 0);
        for (uint64_t address = page_start(p->vaddr); address < page_end(p); address += 4096) {
            VmResult mapped = vmm64_alloc_page(space, address, flags);
            if (mapped != VM_OK) { result = mapped == VM_NOMEM ? ELF64_NOMEM : ELF64_CONFLICT; goto rollback; }
            ++installed[i];
            /* Allocation zeros the full page (BSS and padding). The user PTE
             * already has final permissions; loading writes only via the
             * trusted supervisor aperture, never via a writable user alias. */
            uint64_t begin = address > p->vaddr ? address : p->vaddr;
            uint64_t end = address+4096 < p->vaddr+p->filesz ? address+4096 : p->vaddr+p->filesz;
            if (end > begin) {
                Mapping mapping;
                memory_require(vmm64_lookup(space, address, &mapping) == VM_OK, "ELF owned mapping");
                volatile uint8_t *destination = (volatile uint8_t *)physical_view(mapping.physical);
                const uint8_t *source = (const uint8_t *)image+p->offset+(begin-p->vaddr);
                for (size_t j = 0; j < end-begin; ++j) destination[begin-address+j] = source[j];
            }
        }
    }
    *entry = plan.entry;
    return ELF64_OK;
rollback:
    for (size_t i = 0; i < plan.count; ++i)
        for (size_t j = 0; j < installed[i]; ++j)
            memory_require(vmm64_unmap(space, page_start(plan.segments[i].vaddr)+j*4096, 1, 0) == VM_OK,
                           "ELF allocation rollback");
    return result;
}
