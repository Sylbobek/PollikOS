/* Per-process userspace heap and anonymous mappings (C2).
 * Addresses are virtual; no physical address or frame is exposed to user code.
 * All frames are owned by the process address space, so exit/fault/kill reclaim
 * them through the existing vmm64_destroy path with no extra teardown list. */
#include "heap.h"
#include "launch.h"
#include "scheduler.h"
#include "paging.h"
#include "fs_platform.h"
#include "window.h"
#include "rtc64.h"
#define check memory_require

static uint64_t page_up(uint64_t value) { return (value + MM_PAGE_SIZE-1) & ~(MM_PAGE_SIZE-1); }

void heap64_reset(Process64 *process) {
    process->heap_base = USER_HEAP_BASE;
    process->heap_break = USER_HEAP_BASE;
    process->heap_limit = USER_HEAP_LIMIT;
    process->mmap_next = USER_MMAP_BASE;
    process->thread.wake_tick = 0;
    for (unsigned i = 0; i < USER_MMAP_MAX; ++i) process->mmap[i] = (Mmap64Record){0};
}

static int heap_space(const Process64 *process) {
    return process && process->space.root && process->space.root != vmm64_kernel()->root;
}

/* Rounds the current break up to a page boundary; the final partial page is
 * retained. Growth preflights the whole new range before any mapping so a
 * mid-range collision cannot leave a partially grown heap. A failed allocation
 * rolls back exactly the pages mapped by this call and restores nothing else. */
int64_t heap64_brk(Process64 *process, uint64_t requested) {
    memory_context_check();
    if (!heap_space(process)) return -USER_EINVAL;
    if (!requested) return (int64_t)process->heap_break;
    if (requested < process->heap_base || requested > process->heap_limit) return -USER_EINVAL;
    virt_addr_t old_end = process->heap_base + page_up(process->heap_break-process->heap_base);
    virt_addr_t new_end = process->heap_base + page_up(requested-process->heap_base);
    if (new_end > old_end) {
        for (virt_addr_t address = old_end; address < new_end; address += MM_PAGE_SIZE) {
            Mapping mapping;
            VmResult found = vmm64_lookup(&process->space, address, &mapping);
            if (found != VM_MISSING) return found == VM_OK ? -USER_ENOMEM : -USER_EINVAL;
        }
        virt_addr_t mapped = old_end;
        for (; mapped < new_end; mapped += MM_PAGE_SIZE) {
            if (vmm64_alloc_page(&process->space, mapped, VM_USER|VM_WRITE) != VM_OK) {
                while (mapped > old_end) {
                    mapped -= MM_PAGE_SIZE;
                    check(vmm64_unmap(&process->space, mapped, 1, 0) == VM_OK, "heap growth rollback");
                }
                return -USER_ENOMEM;
            }
        }
    } else if (new_end < old_end) {
        /* Shrink unmaps only pages that were owned by this heap range; the
         * break may sit inside the final retained page. */
        for (virt_addr_t address = new_end; address < old_end; address += MM_PAGE_SIZE)
            check(vmm64_unmap(&process->space, address, 1, 0) == VM_OK, "heap shrink unmap");
    }
    process->heap_break = requested;
    return (int64_t)requested;
}

static unsigned mmap_slot(const Process64 *process) {
    for (unsigned i = 0; i < USER_MMAP_MAX; ++i) if (!process->mmap[i].pages) return i;
    return USER_MMAP_MAX;
}

/* Bump allocation from USER_MMAP_BASE. Exact-length munmap keeps records
 * unambiguous; the cursor never moves backwards, so freed holes are only
 * reusable by a future mapped-region allocator, not by this bounded version. */
int64_t heap64_mmap(Process64 *process, uint64_t length, uint64_t flags) {
    memory_context_check();
    if (!heap_space(process) || flags) return -USER_EINVAL;
    if (!length) return -USER_EINVAL;
    if (length > (uint64_t)USER_MMAP_MAX_PAGES*MM_PAGE_SIZE) return -USER_E2BIG;
    unsigned slot = mmap_slot(process);
    if (slot == USER_MMAP_MAX) return -USER_ENOMEM;
    virt_addr_t base = page_up(process->mmap_next);
    uint64_t bytes = page_up(length);
    if (base < USER_MMAP_BASE || base >= USER_MMAP_LIMIT ||
        bytes > USER_MMAP_LIMIT-base) return -USER_ENOMEM;
    for (virt_addr_t address = base; address < base+bytes; address += MM_PAGE_SIZE) {
        Mapping mapping;
        if (vmm64_lookup(&process->space, address, &mapping) != VM_MISSING) return -USER_ENOMEM;
    }
    uint64_t pages = bytes/MM_PAGE_SIZE;
    virt_addr_t mapped = base;
    for (; mapped < base+bytes; mapped += MM_PAGE_SIZE) {
        if (vmm64_alloc_page(&process->space, mapped, VM_USER|VM_WRITE) != VM_OK) {
            while (mapped > base) {
                mapped -= MM_PAGE_SIZE;
                check(vmm64_unmap(&process->space, mapped, 1, 0) == VM_OK, "anonymous mapping rollback");
            }
            return -USER_ENOMEM;
        }
    }
    process->mmap[slot] = (Mmap64Record){base, pages};
    process->mmap_next = base+bytes;
    return (int64_t)base;
}

int64_t heap64_munmap(Process64 *process, uint64_t address, uint64_t length) {
    memory_context_check();
    if (!heap_space(process) || (address & (MM_PAGE_SIZE-1)) || !length ||
        length > (uint64_t)USER_MMAP_MAX_PAGES*MM_PAGE_SIZE) return -USER_EINVAL;
    if (window64_mapping_busy(process,address)) return -USER_EINVAL;
    uint64_t pages = page_up(length)/MM_PAGE_SIZE;
    unsigned slot = USER_MMAP_MAX;
    for (unsigned i = 0; i < USER_MMAP_MAX; ++i)
        if (process->mmap[i].pages == pages && process->mmap[i].base == address) { slot = i; break; }
    if (slot == USER_MMAP_MAX) return -USER_EINVAL;
    /* Preflight ownership so an inconsistent range cannot cause partial unmap. */
    for (virt_addr_t page = address; page < address+pages*MM_PAGE_SIZE; page += MM_PAGE_SIZE) {
        Mapping mapping;
        if (vmm64_lookup(&process->space, page, &mapping) != VM_OK || mapping.guard || !mapping.owned ||
            !(mapping.flags & VM_USER) || !(mapping.flags & VM_WRITE)) return -USER_EINVAL;
    }
    for (virt_addr_t page = address; page < address+pages*MM_PAGE_SIZE; page += MM_PAGE_SIZE)
        check(vmm64_unmap(&process->space, page, 1, 0) == VM_OK, "anonymous unmap");
    process->mmap[slot] = (Mmap64Record){0};
    return 0;
}

static int64_t clock_value(uint64_t kind) {
    if (kind == USER_CLOCK_TICKS) return (int64_t)scheduler64_ticks();
    if (kind == USER_CLOCK_MS) return (int64_t)(scheduler64_ticks()*(1000/TIMER64_HZ));
    return -USER_EINVAL;
}

int heap64_dispatch(Process64 *process, UserFrame *frame) {
    if (frame->rax == USER_CLOCK_REALTIME) {
        memory_context_check();
        int64_t seconds;
        frame->rax = rtc64_read_unix_seconds(&seconds)
            ? (uint64_t)seconds : (uint64_t)-(int64_t)USER_EIO;
        return 1;
    }
    if (frame->rax < USER_BRK || frame->rax > USER_MUNMAP) return 0;
    memory_context_check();
    int64_t result;
    switch (frame->rax) {
    case USER_BRK: result = heap64_brk(process, frame->rdi); break;
    case USER_MMAP: result = heap64_mmap(process, frame->rdi, frame->rsi); break;
    case USER_MUNMAP: result = heap64_munmap(process, frame->rdi, frame->rsi); break;
    case USER_GETPID: result = (int64_t)process->pid; break;
    case USER_CLOCK: result = clock_value(frame->rdi); break;
    case USER_SLEEP: return 0; /* handled in the process trap (needs suspension) */
    default: result = -USER_ENOSYS; break;
    }
    frame->rax = (uint64_t)result;
    return 1;
}

static int demo_completed;
static void demo_complete(const Process64 *process) {
    if (process->state != PROCESS_EXITED || process->exit_status != 42) {
        memory_log("[C2] demo user state="); memory_hex(process->state);
        memory_log(" status="); memory_hex((uint64_t)process->exit_status);
        memory_log(" vector="); memory_hex(process->thread.frame.vector);
        memory_log(" error="); memory_hex(process->thread.frame.error);
        memory_log(" line="); memory_hex(process->thread.frame.r15);
        uint64_t failure = 0;
        if (copy_from_user64(&process->space, &failure, USER_DATA+24, 8) == USER_COPY_OK) {
            memory_log(" rax="); memory_hex(failure);
        }
        memory_log(" break="); memory_hex(process->heap_break);
        memory_log("\n");
    }
    check(process->state == PROCESS_EXITED && process->exit_status == 42, "userspace memory demo");
    demo_completed = 1;
}
void heap64_demo(void) {
    page_count_t baseline = pmm64_stats().free;
    Process64 *process;
    Launch64Result result = process64_launch_path("/bin/memtest", 0, 0, 0, 0, &process);
    if (result != LAUNCH_OK) {
        memory_log("[C2] /bin/memtest: "); memory_log(launch64_error_name(result)); memory_log("\n");
        return;
    }
    uint64_t mode = 0;
    check(copy_to_user64(&process->space, USER_DATA, &mode, sizeof(mode)) == USER_COPY_OK, "memory demo mode");
    demo_completed = 0;
    check(process64_tick_limit(process->pid, 300) && scheduler64_run(400, 0, demo_complete) &&
          demo_completed && !scheduler64_count() && !vfs_debug_handles() &&
          pmm64_stats().free == baseline, "memory demo ownership balance");
    memory_log("[C2] demo PASS: VFS-launched ELF brk/mmap/getpid/clock/sleep exits 42\n");
}
