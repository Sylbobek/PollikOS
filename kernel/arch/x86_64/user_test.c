#include "user.h"
#include "paging.h"
#define check memory_require
static void pass(const char *name) { memory_log("[USER64] PASS: "); memory_log(name); memory_log("\n"); }
static void expect(UserCopyResult result, UserCopyResult wanted) { check(result == wanted, "safe-copy result"); }
void usercopy_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    AddressSpace space = {0};
    check(vmm64_create(&space) == VM_OK, "copy test space");
    check(vmm64_alloc_page(&space, USER_DATA, VM_USER|VM_WRITE) == VM_OK &&
          vmm64_alloc_page(&space, USER_DATA+4096, VM_USER|VM_WRITE) == VM_OK &&
          vmm64_alloc_page(&space, USER_CODE, VM_USER|VM_EXEC) == VM_OK, "copy test mappings");
    char from[32], to[32];
    for (unsigned i = 0; i < sizeof(from); ++i) { from[i] = (char)('A'+i%26); to[i] = 0; }
    expect(copy_to_user64(&space, USER_DATA, from, sizeof(from)), USER_COPY_OK);
    expect(copy_from_user64(&space, to, USER_DATA, sizeof(to)), USER_COPY_OK);
    for (unsigned i = 0; i < sizeof(to); ++i) check(to[i] == from[i], "one-page user copy");
    expect(copy_to_user64(&space, USER_DATA+4090, from, sizeof(from)), USER_COPY_OK);
    expect(copy_from_user64(&space, to, USER_DATA+4090, sizeof(to)), USER_COPY_OK);
    for (unsigned i = 0; i < sizeof(to); ++i) check(to[i] == from[i], "cross-page user copy");
    for (unsigned i = 0; i < sizeof(to); ++i) to[i] = '!';
    expect(copy_from_user64(&space, to, USER_DATA+8190, sizeof(to)), USER_COPY_FAULT);
    for (unsigned i = 0; i < sizeof(to); ++i) check(to[i] == '!', "failed read leaves destination intact");
    expect(copy_to_user64(&space, USER_DATA+8190, from, sizeof(from)), USER_COPY_FAULT);
    expect(copy_from_user64(&space, to, USER_DATA+8190, 2), USER_COPY_OK);
    check(to[0] == 0 && to[1] == 0, "failed write leaves destination intact");
    virt_addr_t bad[] = {0x100000, MM_KERNEL_START, MM_USER_END, UINT64_MAX-7};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        expect(copy_from_user64(&space, to, bad[i], 16), USER_COPY_FAULT);
        expect(copy_to_user64(&space, bad[i], from, 16), USER_COPY_FAULT);
    }
    expect(copy_from_user64(&space, to, USER_DATA, SIZE_MAX), USER_COPY_FAULT);
    expect(copy_to_user64(&space, USER_DATA, from, SIZE_MAX), USER_COPY_FAULT);
    expect(copy_to_user64(&space, USER_CODE, from, 1), USER_COPY_FAULT);
    expect(copy_from_user64(&space, to, USER_CODE, 1), USER_COPY_OK);
    expect(copy_from_user64(&space, 0, USER_PRIVATE, 0), USER_COPY_OK);
    expect(copy_to_user64(&space, USER_PRIVATE, 0, 0), USER_COPY_OK);
    expect(copy_from_user64(&space, 0, MM_USER_END, 0), USER_COPY_FAULT);
    expect(copy_from_user64(&space, to, MM_USER_END-1, 2), USER_COPY_FAULT);
    expect(copy_string_from_user64(&space, to, USER_DATA, sizeof(to)), USER_COPY_TOO_LONG);
    check(to[0] == 0, "unterminated destination cleared");
    from[31] = 0;
    expect(copy_to_user64(&space, USER_DATA+4090, from, sizeof(from)), USER_COPY_OK);
    expect(copy_string_from_user64(&space, to, USER_DATA+4090, sizeof(to)), USER_COPY_OK);
    for (unsigned i = 0; i < sizeof(to); ++i) check(to[i] == from[i], "maximum-length string");
    expect(copy_to_user64(&space, USER_DATA+8190, from, 2), USER_COPY_OK);
    expect(copy_string_from_user64(&space, to, USER_DATA+8190, sizeof(to)), USER_COPY_FAULT);
    expect(copy_string_from_user64(&space, to, UINT64_MAX, sizeof(to)), USER_COPY_FAULT);
    expect(copy_string_from_user64(&space, to, USER_DATA, 0), USER_COPY_TOO_LONG);
    /* Parent permissions matter even when the leaf permits user writes. */
    unsigned root_index = (unsigned)(USER_DATA >> 39);
    uint64_t root_entry = physical_view(space.root)[root_index];
    physical_view(space.root)[root_index] = root_entry & ~PTE_USER;
    expect(copy_from_user64(&space, to, USER_DATA, 1), USER_COPY_FAULT);
    physical_view(space.root)[root_index] = root_entry & ~PTE_WRITE;
    expect(copy_to_user64(&space, USER_DATA, from, 1), USER_COPY_FAULT);
    expect(copy_from_user64(&space, to, USER_DATA, 1), USER_COPY_OK);
    physical_view(space.root)[root_index] = root_entry;
    check(vmm64_destroy(&space) == VM_OK && pmm64_stats().free == baseline, "copy test balance");
    pass("safe copies: ranges, parent permissions, cross-page, strings and failures");
}
static void run(Process64 *process, Process64Result *result) {
    if (!process || !process64_run(process, result)) memory_panic("run Ring 3 process");
}
void process64_selftest(void) {
    page_count_t baseline = pmm64_stats().free;
    Process64 *a = process64_create(10, 37), *b = process64_create(10, 73);
    check(a && b && a->pid != b->pid && a->kernel_stack.top != b->kernel_stack.top, "independent processes");
    Mapping ma, mb;
    check(vmm64_lookup(&a->space, USER_DATA, &ma) == VM_OK &&
          vmm64_lookup(&b->space, USER_DATA, &mb) == VM_OK && ma.physical != mb.physical,
          "distinct private frames at same VA");
    Process64Result result;
    run(a, &result);
    check(result.state == PROCESS_EXITED && result.exit_status == 37 && (result.frame.cs & 3) == 3,
          "process A real CPL3 data");
    run(b, &result);
    check(result.state == PROCESS_EXITED && result.exit_status == 73, "process B private data");
    a = process64_create(6, 0); b = process64_create(10, 17);
    check(a && b && vmm64_alloc_page(&b->space, USER_PRIVATE, VM_USER|VM_WRITE) == VM_OK, "B-only private mapping");
    run(a, &result);
    check(result.state == PROCESS_FAULTED && result.frame.vector == 14 && result.fault_address == USER_PRIVATE &&
          result.frame.error == 4, "A cannot read B-only data");
    run(b, &result);
    check(result.state == PROCESS_EXITED && result.exit_status == 17, "B survives A fault");
    pass("independent CPL3 spaces and per-process kernel stacks");
    const uint64_t errors[] = {0, 5, 7, 21, 4, 4};
    const uint64_t addresses[] = {0, 0x100000, USER_CODE, USER_DATA, 0, USER_STACK_BASE};
    for (unsigned mode = 1; mode <= 5; ++mode) {
        run(process64_create(mode, 0), &result);
        check(result.state == PROCESS_FAULTED && result.frame.vector == 14 &&
              result.frame.error == errors[mode] && result.fault_address == addresses[mode], "CPL3 protection fault");
        check(pmm64_stats().free == baseline, "fault teardown balance");
    }
    for (unsigned mode = 8; mode <= 9; ++mode) {
        run(process64_create(mode, 0), &result);
        check(result.state == PROCESS_FAULTED && result.frame.vector == 13, "privileged operation rejected");
    }
    run(process64_create(7, 91), &result);
    check(result.state == PROCESS_EXITED && result.exit_status == 91, "real ABI errors and register preservation");
    /* Invalid initial return state is rejected before executing IRETQ. */
    a = process64_create(10, 0); check(a != 0, "invalid entry fixture");
    a->frame.rip = MM_USER_END;
    run(a, &result);
    check(result.state == PROCESS_FAULTED, "noncanonical RIP rejected");
    a = process64_create(10, 0); check(a != 0, "invalid stack fixture");
    a->frame.rsp = MM_USER_END;
    run(a, &result);
    check(result.state == PROCESS_FAULTED, "noncanonical RSP rejected");
    run(process64_create(11, 0), &result);
    check(result.state == PROCESS_FAULTED && result.frame.vector == USER_GATE && result.frame.rsp == USER_LIMIT,
          "unsafe user RSP rejected on real interrupt return");
    pass("CPL3 protection faults, ABI errors and safe return validation");
    /* Exhaust each successful allocation prefix, including control-page,
     * address-space, intermediate table, code, and both stack allocations. */
    int reached_success = 0;
    unsigned failure_points = 0;
    for (int64_t budget = 0; budget < 192; ++budget) {
        pmm64_fail_after(budget);
        a = process64_create(10, 0);
        pmm64_fail_after(-1);
        if (a) { check(process64_destroy(a), "successful fixture destroy"); reached_success = 1; }
        else ++failure_points;
        check(pmm64_stats().free == baseline, "partial process rollback balance");
        if (reached_success) break;
    }
    check(reached_success && failure_points > 15, "all construction failure points exercised");
    pass("process construction allocation-failure rollback");
    for (unsigned i = 0; i < 100; ++i) {
        run(process64_create(7, i), &result);
        check(result.state == PROCESS_EXITED && result.exit_status == (int)i &&
              result.frame.vector == USER_GATE, "100 real CPL3 lifecycles");
        check(pmm64_stats().free == baseline, "100-process PMM balance");
    }
    memory_log("[USER64] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(pmm64_stats().free); memory_log("\n");
    pass("100 CPL3 processes without PMM leak");
}
