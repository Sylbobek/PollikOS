#include "memory.h"
extern void probe_read(void *), probe_write(void *), probe_execute(void *);
extern char probe_read_instruction[], probe_write_instruction[];
#define check memory_require
static void pass(const char *name) {
    memory_log("[MM64] PASS: "); memory_log(name); memory_log("\n");
}
static page_count_t free_pages(void) { return pmm64_stats().free; }
static void balanced(page_count_t before, const char *name) {
    check(free_pages() == before, name);
}
static void expect(VmResult result, VmResult wanted, const char *name) { check(result == wanted, name); }

void memory_selftest(void) {
    page_count_t baseline = free_pages();
    phys_addr_t normal = pmm64_alloc(PMM_NORMAL), dma = pmm64_alloc(PMM_DMA32);
    check(normal && dma && normal != dma && dma < MM_DMA32_END, "PMM zones");
    check(!pmm64_free(0x100000) && !pmm64_free(0xfec00000) && !pmm64_free(normal+1),
          "reserved/unaligned free rejection");
    physical_view(normal)[0] = UINT64_C(0x0123456789abcdef);
    physical_view(dma)[0] = UINT64_C(0xfedcba9876543210);
    check(physical_view(normal)[0] == UINT64_C(0x0123456789abcdef), "physical address alias");
    if (pmm64_stats().above4g) {
        check(normal >= MM_DMA32_END && vmm64_kernel()->root >= MM_DMA32_END,
              "high data and page-table allocation");
        memory_log("[MM64] high frame="); memory_hex(normal);
        memory_log(" high CR3="); memory_hex(vmm64_kernel()->root); memory_log("\n");
        pass("E820 above 4 GiB allocation and access");
    } else memory_log("[MM64] no E820 RAM above 4 GiB on this boot\n");
    check(pmm64_free(normal) && !pmm64_free(normal) && pmm64_free(dma), "PMM free/double-free");
    balanced(baseline, "PMM allocation balance");
    pass("PMM balance and DMA32");

    /* Contiguous multi-frame allocation for DMA descriptor lists. */
    phys_addr_t run = pmm64_alloc_contiguous(8, PMM_DMA32);
    check(run && (run & 4095) == 0, "contiguous DMA32 allocation");
    check(free_pages() == baseline - 8, "contiguous allocation consumed eight frames");
    for (page_count_t i = 0; i < 8; ++i)
        check(pmm64_is_allocated(run + i*4096), "contiguous frame owned");
    check(!pmm64_free_contiguous(run + 1, 4), "unaligned contiguous free rejection");
    check(pmm64_free_contiguous(run, 8), "contiguous free");
    balanced(baseline, "contiguous allocation balance");
    run = pmm64_alloc_contiguous(8, PMM_DMA32);
    check(run && pmm64_free_contiguous(run, 8), "contiguous free again");
    check(!pmm64_free_contiguous(run, 8), "double contiguous free rejection");
    check(!pmm64_alloc_contiguous(0, PMM_DMA32), "zero-count contiguous rejection");
    check(!pmm64_free_contiguous(run, 0), "zero-count contiguous free rejection");
    pass("contiguous DMA allocation and free");

    AddressSpace a = {0}, b = {0};
    expect(vmm64_create(&a), VM_OK, "create a");
    expect(vmm64_create(&b), VM_OK, "create b");
    virt_addr_t address = MM_USER_START;
    Mapping mapping;
    page_count_t before = free_pages();
    expect(vmm64_alloc_page(&a, address, VM_USER|VM_WRITE), VM_OK, "dynamic map");
    check(before-free_pages() == 4, "three intermediate tables and data frame");
    expect(vmm64_alloc_page(&b, address, VM_USER|VM_WRITE), VM_OK, "second independent map");
    expect(vmm64_switch(&a), VM_OK, "switch a");
    check(*(volatile uint64_t *)address == 0, "zeroed allocation");
    *(volatile uint64_t *)address = 0x12345678;
    expect(vmm64_lookup(&a, address, &mapping), VM_OK, "lookup owned");
    check(mapping.owned && mapping.flags == (VM_USER|VM_WRITE) &&
          !pmm64_free(mapping.physical), "VMM ownership and permissions");
    expect(vmm64_unmap(&a, address, 0, 0), VM_INVALID, "owned frame cannot detach");
    expect(vmm64_destroy(&a), VM_BUSY, "active root cannot be freed");
    expect(vmm64_switch(&b), VM_OK, "switch b");
    check(*(volatile uint64_t *)address == 0, "distinct address space data");
    *(volatile uint64_t *)address = 0x87654321;
    expect(vmm64_switch(&a), VM_OK, "switch back");
    check(*(volatile uint64_t *)address == 0x12345678, "CR3 TLB isolation");
    expect(vmm64_unmap(&a, address, 1, 0), VM_OK, "unmap owned");
    memory_fault_test(probe_read, address, (uintptr_t)probe_read_instruction, 0, "unmapped dynamic page");
    expect(vmm64_lookup(&a, address, &mapping), VM_MISSING, "missing after unmap");
    expect(vmm64_unmap(&a, address, 1, 0), VM_MISSING, "double unmap");
    pass("dynamic tables, access, unmap and CR3 isolation");

    expect(vmm64_alloc_page(&a, address, VM_USER), VM_OK, "map readonly");
    memory_fault_test(probe_write, address, (uintptr_t)probe_write_instruction, 3, "dynamic read-only page");
    memory_fault_test(probe_execute, address, address, 17, "dynamic NX page");
    before = free_pages();
    expect(vmm64_alloc_page(&a, address, VM_USER|VM_WRITE), VM_EXISTS, "duplicate mapping");
    expect(vmm64_alloc_page(&a, address+4096, VM_USER|VM_WRITE|VM_EXEC), VM_INVALID, "W^X rejection");
    expect(vmm64_alloc_page(&a, MM_USER_END, VM_USER), VM_INVALID, "noncanonical rejection");
    expect(vmm64_alloc_page(&a, MM_KERNEL_START, VM_USER), VM_INVALID, "shared kernel rejection");
    expect(vmm64_map_borrowed(&a, address+4096, 0x100000, VM_USER), VM_INVALID, "no user kernel alias");
    expect(vmm64_alloc_page(&a, address+1, VM_USER), VM_INVALID, "unaligned rejection");
    expect(vmm64_map_borrowed(vmm64_kernel(), MM_KERNEL_START+0x400000,
                              physical_limit(), VM_WRITE), VM_INVALID, "physical width rejection");
    expect(vmm64_alloc_page(&a, address+4096, VM_USER|0x100), VM_INVALID, "unknown flags rejection");
    balanced(before, "invalid operation balance");
    expect(vmm64_unmap(&a, address, 1, 0), VM_OK, "release readonly");
    pass("dynamic protections and invalid requests");

    phys_addr_t external = pmm64_alloc(PMM_NORMAL);
    check(external != 0, "external page");
    physical_zero(external);
    physical_view(external)[0] = 0xc3; /* RET: test explicit RX permission */
    expect(vmm64_map_borrowed(&a, address, external, VM_USER|VM_EXEC), VM_OK, "borrow RX page");
    ((void (*)(void))address)();
    expect(vmm64_unmap(&a, address, 1, 0), VM_INVALID, "cannot free borrowed page");
    phys_addr_t returned = 0;
    expect(vmm64_unmap(&a, address, 0, &returned), VM_OK, "borrowed detach");
    check(returned == external && pmm64_is_allocated(external), "external lifetime retained");
    expect(vmm64_map_borrowed(&a, address, external, VM_USER), VM_OK, "borrow for destroy");
    GuardedStack stack = {0};
    expect(vmm64_stack_create(&a, address+0x20000, 3, 2, 1, &stack), VM_OK, "guarded user stack mapping");
    *(volatile uint64_t *)(stack.top-8) = 0x11223344;
    memory_fault_test(probe_read, stack.base, (uintptr_t)probe_read_instruction, 0, "dynamic stack guard");
    memory_fault_test(probe_read, stack.base+4096, (uintptr_t)probe_read_instruction, 0, "second dynamic guard");
    expect(vmm64_alloc_page(&a, stack.base, VM_USER|VM_WRITE), VM_EXISTS, "guard reservation");
    expect(vmm64_stack_destroy(&a, &stack), VM_OK, "stack destruction");
    expect(vmm64_stack_destroy(&a, &stack), VM_INVALID, "double stack destruction");
    /* Leave another stack behind: address-space destruction must release it. */
    expect(vmm64_stack_create(&a, address+0x20000, 3, 2, 1, &stack), VM_OK, "stack owned by space");
    expect(vmm64_switch(vmm64_kernel()), VM_OK, "switch kernel");
    expect(vmm64_destroy(&a), VM_OK, "destroy a with stack and borrowed frame");
    check(pmm64_is_allocated(external) && pmm64_free(external), "borrow survives destruction");
    expect(vmm64_destroy(&b), VM_OK, "destroy b with owned frame");
    expect(vmm64_destroy(&b), VM_INVALID, "double destroy");
    balanced(baseline, "address space destruction balance");
    pass("borrowed ownership, executable page and guarded stack destruction");

    /* Allocation failures after every possible successful prefix of a new walk. */
    pmm64_fail_after(0);
    expect(vmm64_create(&a), VM_NOMEM, "root allocation failure");
    pmm64_fail_after(-1);
    expect(vmm64_create(&a), VM_OK, "failure test space");
    before = free_pages();
    for (int64_t budget = 0; budget < 4; ++budget) {
        pmm64_fail_after(budget);
        expect(vmm64_alloc_page(&a, address, VM_USER|VM_WRITE), VM_NOMEM, "map injected failure");
        pmm64_fail_after(-1);
        expect(vmm64_lookup(&a, address, &mapping), VM_MISSING, "no partial leaf");
        balanced(before, "partial page table rollback");
    }
    stack = (GuardedStack){0};
    for (int64_t budget = 0; budget < 6; ++budget) {
        pmm64_fail_after(budget);
        expect(vmm64_stack_create(&a, address, 3, 2, 1, &stack), VM_NOMEM, "stack injected failure");
        pmm64_fail_after(-1);
        check(stack.pages == 0, "failed stack has no ownership");
        balanced(before, "partial stack rollback");
    }
    /* Failure under an existing ancestor must not remove that ancestor. */
    expect(vmm64_alloc_page(&a, address, VM_USER|VM_WRITE), VM_OK, "existing mapping");
    before = free_pages();
    for (int64_t budget = 0; budget < 3; ++budget) {
        pmm64_fail_after(budget);
        expect(vmm64_alloc_page(&a, address+(UINT64_C(1)<<30), VM_USER|VM_WRITE), VM_NOMEM,
               "rollback beneath existing PDPT");
        pmm64_fail_after(-1);
        expect(vmm64_lookup(&a, address, &mapping), VM_OK, "original mapping survives");
        balanced(before, "existing ancestor rollback balance");
    }
    expect(vmm64_destroy(&a), VM_OK, "destroy failure test space");
    balanced(baseline, "failure test final balance");
    pass("allocation failure rollback");

    /* Shared-kernel mutations work from an active secondary address space;
     * no root destruction may release the shared tables or MMIO frame. */
    expect(vmm64_create(&a), VM_OK, "shared kernel space");
    expect(vmm64_switch(&a), VM_OK, "shared kernel switch");
    virt_addr_t shared = MM_KERNEL_START+0x400000;
    expect(vmm64_alloc_page(vmm64_kernel(), shared, VM_WRITE), VM_OK, "shared kernel map");
    *(volatile uint64_t *)shared = 0xabcdef;
    expect(vmm64_lookup(&a, shared, &mapping), VM_OK, "kernel sharing lookup");
    check(!(mapping.flags & VM_USER), "kernel supervisor permission");
    expect(vmm64_switch(vmm64_kernel()), VM_OK, "shared return kernel");
    expect(vmm64_destroy(&a), VM_OK, "destroy sharing space");
    check(*(volatile uint64_t *)shared == 0xabcdef, "shared kernel survives");
    expect(vmm64_unmap(vmm64_kernel(), shared, 1, 0), VM_OK, "shared kernel release");
    expect(vmm64_map_borrowed(vmm64_kernel(), shared, 0xfec00000, VM_DEVICE|VM_WRITE), VM_OK, "MMIO map");
    expect(vmm64_unmap(vmm64_kernel(), shared, 1, 0), VM_INVALID, "MMIO cannot be freed");
    expect(vmm64_unmap(vmm64_kernel(), shared, 0, &returned), VM_OK, "MMIO detach");
    check(returned == 0xfec00000, "MMIO address preserved");
    balanced(baseline, "shared mapping balance");
    pass("shared kernel and external MMIO lifetime");

    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        expect(vmm64_create(&a), VM_OK, "cycle create");
        expect(vmm64_alloc_page(&a, address, VM_USER|VM_WRITE), VM_OK, "cycle map");
        expect(vmm64_alloc_page(&a, address+(UINT64_C(1)<<30), VM_USER), VM_OK, "cycle distant map");
        stack = (GuardedStack){0};
        expect(vmm64_stack_create(&a, address+0x10000, 4, 1, 1, &stack), VM_OK, "cycle stack");
        expect(vmm64_switch(&a), VM_OK, "cycle switch");
        *(volatile uint64_t *)address = cycle;
        expect(vmm64_unmap(&a, address, 1, 0), VM_OK, "cycle unmap");
        expect(vmm64_stack_destroy(&a, &stack), VM_OK, "cycle stack destroy");
        expect(vmm64_switch(vmm64_kernel()), VM_OK, "cycle kernel switch");
        expect(vmm64_destroy(&a), VM_OK, "cycle destroy");
        balanced(baseline, "lifecycle PMM leak");
    }
    memory_log("[MM64] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(free_pages()); memory_log("\n");
    pass("100 mapping lifecycles without PMM leak");
}
