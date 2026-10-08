#include "elf64.h"
#include "test_memory.h"
#define check memory_require
#include "launch.h"
static uint8_t *damaged;
static const char *arguments[] = {"hello.elf", "first", "second"};
static const char *environment[] = {"TEST=pollikos"};
static size_t image_size(void) { return hello_elf_size; }
static page_count_t available(void) { return pmm64_stats().free; }
static void pass(const char *text) { memory_log("[ELF64] PASS: "); memory_log(text); memory_log("\n"); }
static Process64 *load(void) {
    Elf64Result error;
    Process64 *process = process64_create_elf(hello_elf_start, image_size(), 3, arguments, 1, environment, &error);
    check(process && error == ELF64_OK, "load valid ELF");
    return process;
}
static void execute(Process64 *process, int fault, uint64_t error) {
    Process64Result result;
    check(process64_run(process, &result), "execute ELF");
    if (fault) check(result.state == PROCESS_FAULTED && result.frame.vector == 14 && result.frame.error == error,
                     "ELF CPL3 protection");
    else check(result.state == PROCESS_EXITED && result.exit_status == 42 && (result.frame.cs & 3) == 3,
               "ELF startup/data/BSS/exit");
}
void elf64_selftest(void) {
    damaged = kernel_test_buffer_alloc(65536);
    check(damaged != 0, "ELF mutation buffer allocated after boot");
    page_count_t baseline = available();
    check(image_size() <= 65536, "ELF mutation fixture capacity");
    for (unsigned test = 0; test < 26; ++test) {
        for (size_t i = 0; i < image_size(); ++i) damaged[i] = hello_elf_start[i];
        Elf64Header *h = (Elf64Header *)damaged;
        Elf64ProgramHeader *p = (Elf64ProgramHeader *)(damaged+h->phoff);
        check(h->phnum == 2, "two-segment ELF fixture");
        size_t size = image_size();
        switch (test) {
        case 0: h->ident[0] = 0; break;
        case 1: h->ident[4] = 1; break;
        case 2: h->machine = 3; break;
        case 3: size = 63; break;
        case 4: h->phoff = UINT64_MAX; break;
        case 5: size = h->phoff+h->phnum*56-1; break;
        case 6: p[0].filesz = p[0].memsz+1; break;
        case 7: p[0].offset = image_size(); break;
        case 8: p[0].vaddr = 0x100000; break;
        case 9: p[0].vaddr = MM_USER_END; break;
        case 10: p[0].memsz = UINT64_MAX; break;
        case 11: p[1].vaddr = p[0].vaddr; break;
        case 12: h->entry = USER_PRIVATE; break;
        case 13: h->entry = p[1].vaddr; break;
        case 14: p[1].type = 3; break;
        case 15: h->type = 3; break;
        case 16: p[1].type = 2; break;
        case 17: p[0].align = 3; break;
        case 18: p[0].align = UINT64_C(1)<<63; break;
        case 19: ++p[0].offset; break;
        case 20: p[0].flags = 7; break;
        case 21: h->phnum = 65535; break;
        case 22: h->phentsize = 55; break;
        case 23: h->ident[5] = 2; break;
        case 24: h->version = 0; break;
        case 25: p[1].vaddr = USER_STACK_BASE; break;
        }
        Elf64Result error;
        check(elf64_validate(damaged, size) != ELF64_OK, "reject malformed ELF");
        Process64 *invalid = process64_create_elf(damaged, size, 3, arguments, 1, environment, &error);
        check(!invalid && error != ELF64_OK && available() == baseline, "malformed ELF clean failure");
    }
    pass("26 malformed and unsupported ELF cases without leaks");
    Process64 *a = load(), *b = load();
    Mapping ma, mb;
    check(vmm64_lookup(&a->space, USER_CODE, &ma) == VM_OK && ma.flags == (VM_USER|VM_EXEC), "ELF text RX");
    check(vmm64_lookup(&a->space, USER_DATA, &ma) == VM_OK && ma.flags == (VM_USER|VM_WRITE), "ELF data RW NX");
    check(vmm64_lookup(&b->space, USER_DATA, &mb) == VM_OK && ma.physical != mb.physical &&
          a->thread.user_stack.top == b->thread.user_stack.top && a->thread.kernel_stack.top != b->thread.kernel_stack.top, "ELF independent data/stacks");
    check(vmm64_lookup(&a->space, USER_STACK_BASE+4096, &ma) == VM_OK &&
          vmm64_lookup(&b->space, USER_STACK_BASE+4096, &mb) == VM_OK && ma.physical != mb.physical,
          "ELF distinct stack frames at same VA");
    uint64_t mode = 1;
    check(copy_to_user64(&a->space, USER_DATA, &mode, 8) == USER_COPY_OK, "set private fault mode");
    execute(a, 1, 7);
    execute(b, 0, 0); /* B retains initialized mode and zero BSS despite A's fault. */
    for (mode = 2; mode <= 3; ++mode) {
        a = load();
        check(copy_to_user64(&a->space, USER_DATA, &mode, 8) == USER_COPY_OK, "ELF fault mode");
        execute(a, 1, mode == 2 ? 21 : 5);
    }
    check(available() == baseline, "ELF fault balance");
    pass("ELF RX/RW/NX protections and independent faulting processes");

    Elf64Result error;
    const char *invalid_args[] = {0};
    const char *too_many[STARTUP_MAX_ARGS+1];
    for (size_t i = 0; i <= STARTUP_MAX_ARGS; ++i) too_many[i] = "arg";
    check(!process64_create_elf(hello_elf_start, image_size(), STARTUP_MAX_ARGS+1, too_many, 0, 0, &error) &&
          error == ELF64_ARGUMENTS, "argument count cap");
    check(!process64_create_elf(hello_elf_start, image_size(), 1, invalid_args, 0, 0, &error) &&
          error == ELF64_ARGUMENTS, "null argument rejection");
    char long_string[STARTUP_MAX_STRING];
    for (size_t i = 0; i < sizeof(long_string); ++i) long_string[i] = 'x';
    const char *long_args[] = {long_string};
    check(!process64_create_elf(hello_elf_start, image_size(), 1, long_args, 0, 0, &error) &&
          error == ELF64_ARGUMENTS, "unterminated kernel argument cap");
    long_string[STARTUP_MAX_STRING-1] = 0;
    const char *full_args[STARTUP_MAX_ARGS];
    for (size_t i = 0; i < STARTUP_MAX_ARGS; ++i) full_args[i] = long_string;
    check(!process64_create_elf(hello_elf_start, image_size(), STARTUP_MAX_ARGS, full_args, 1, environment, &error) &&
          error == ELF64_ARGUMENTS, "total argument/environment byte cap");
    a = process64_create_elf(hello_elf_start, image_size(), 0, 0, 0, 0, &error);
    check(a && error == ELF64_OK, "empty argument/environment vectors");
    uint64_t header[5], null_pointer = 1;
    check(copy_from_user64(&a->space, header, a->thread.frame.rsp, sizeof(header)) == USER_COPY_OK &&
          header[0] == STARTUP_VERSION && !header[1] && !header[3] && !(a->thread.frame.rsp & 15), "startup header");
    check(copy_from_user64(&a->space, &null_pointer, header[2], 8) == USER_COPY_OK && !null_pointer, "empty argv null");
    check(copy_from_user64(&a->space, &null_pointer, header[4], 8) == USER_COPY_OK && !null_pointer, "empty envp null");
    check(process64_destroy(a) && available() == baseline, "startup failure cleanup");
    pass("bounded versioned argc/argv/envp stack construction");

    int succeeded = 0;
    for (int64_t budget = 0; budget < 96; ++budget) {
        pmm64_fail_after(budget);
        a = process64_create_elf(hello_elf_start, image_size(), 3, arguments, 1, environment, &error);
        pmm64_fail_after(-1);
        if (a) { check(error == ELF64_OK && process64_destroy(a), "ELF failure sweep success"); succeeded = 1; }
        else check(error == ELF64_NOMEM, "ELF injected allocation failure");
        check(available() == baseline, "ELF load rollback balance");
        if (succeeded) break;
    }
    check(succeeded, "ELF allocation prefixes exercised");
    /* Standalone loader rollback must preserve mappings it did not create. */
    AddressSpace space = {0};
    check(vmm64_create(&space) == VM_OK && vmm64_alloc_page(&space, USER_PRIVATE, VM_USER|VM_WRITE) == VM_OK,
          "transaction test space");
    page_count_t before_load = available();
    virt_addr_t entry = 0;
    for (int64_t budget = 0; budget < 4; ++budget) {
        pmm64_fail_after(budget);
        error = elf64_load(&space, hello_elf_start, image_size(), &entry);
        pmm64_fail_after(-1);
        check(error == ELF64_NOMEM && available() == before_load &&
              vmm64_lookup(&space, USER_PRIVATE, &ma) == VM_OK, "standalone loader rollback");
    }
    check(elf64_load(&space, hello_elf_start, image_size(), &entry) == ELF64_OK && entry == USER_CODE,
          "standalone load entry");
    before_load = available();
    check(elf64_load(&space, hello_elf_start, image_size(), &entry) == ELF64_CONFLICT && available() == before_load,
          "do not overwrite existing image");
    check(vmm64_destroy(&space) == VM_OK && available() == baseline, "standalone image teardown");
    pass("ELF allocation rollback and pre-existing mapping preservation");
    for (unsigned i = 0; i < 100; ++i) {
        execute(load(), 0, 0);
        check(available() == baseline, "100 ELF lifecycle PMM balance");
    }
    memory_log("[ELF64] balance before="); memory_hex(baseline);
    memory_log(" after="); memory_hex(available()); memory_log("\n");
    pass("100 real ELF64 CPL3 lifecycles without PMM leak");
    kernel_test_buffer_free(damaged);
    damaged = 0;
}
