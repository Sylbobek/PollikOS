#include "elf.h"
#include "pmm.h"
#include "klog.h"

int elf_validate(const u8 *data, u32 size) {
    if (!data || size < sizeof(Elf32_Ehdr))
        return 0;

    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)data;

    if (ehdr->e_ident[0] != ELF_MAGIC0 ||
        ehdr->e_ident[1] != ELF_MAGIC1 ||
        ehdr->e_ident[2] != ELF_MAGIC2 ||
        ehdr->e_ident[3] != ELF_MAGIC3) {
        KLOG_WARN(KLOG_CAT_PROC, "ELF validation failed: bad magic");
        return 0;
    }

    if (ehdr->e_ident[4] != 1) { /* Not 32-bit */
        KLOG_WARN(KLOG_CAT_PROC, "ELF validation failed: not 32-bit");
        return 0;
    }

    if (ehdr->e_ident[5] != 1) { /* Not little-endian */
        KLOG_WARN(KLOG_CAT_PROC, "ELF validation failed: not little-endian");
        return 0;
    }

    if (ehdr->e_type != ET_EXEC) {
        KLOG_WARN(KLOG_CAT_PROC, "ELF validation failed: not ET_EXEC");
        return 0;
    }

    if (ehdr->e_machine != EM_386) {
        KLOG_WARN(KLOG_CAT_PROC, "ELF validation failed: not EM_386");
        return 0;
    }

    if (ehdr->e_version != 1) { /* EV_CURRENT */
        KLOG_WARN(KLOG_CAT_PROC, "ELF validation failed: not EV_CURRENT");
        return 0;
    }

    if (ehdr->e_ehsize != sizeof(Elf32_Ehdr)) {
        KLOG_WARN(KLOG_CAT_PROC, "ELF validation failed: bad ehsize");
        return 0;
    }

    if (ehdr->e_phentsize != sizeof(Elf32_Phdr)) {
        KLOG_WARN(KLOG_CAT_PROC, "ELF validation failed: bad phentsize");
        return 0;
    }

    if (ehdr->e_phnum == 0 || ehdr->e_phnum > 64) {
        KLOG_WARN(KLOG_CAT_PROC, "ELF validation failed: bad phnum");
        return 0;
    }

    if (ehdr->e_phoff == 0 || ehdr->e_phoff > size) {
        KLOG_WARN(KLOG_CAT_PROC, "ELF validation failed: bad phoff");
        return 0;
    }

    u32 ph_table_end = ehdr->e_phoff + (u32)ehdr->e_phnum * (u32)ehdr->e_phentsize;
    if (ph_table_end > size || ph_table_end < ehdr->e_phoff) {
        KLOG_WARN(KLOG_CAT_PROC, "ELF validation failed: program header table out of bounds");
        return 0;
    }

    for (u32 i = 0; i < ehdr->e_phnum; i++) {
        const Elf32_Phdr *phdr = (const Elf32_Phdr *)(data + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type == PT_LOAD) {
            /* Overflow checks */
            if (phdr->p_offset + phdr->p_filesz < phdr->p_offset ||
                phdr->p_offset + phdr->p_filesz > size)
                return 0;
            if (phdr->p_memsz < phdr->p_filesz)
                return 0;
            if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
                return 0;

            /* Ensure segment is strictly inside userspace boundary */
            if (phdr->p_vaddr < USER_SPACE_START || phdr->p_vaddr >= USER_SPACE_END ||
                phdr->p_vaddr + phdr->p_memsz > USER_SPACE_END) {
                klog_hex(KLOG_CAT_PROC, "ELF validation failed: PT_LOAD outside user range, vaddr ", phdr->p_vaddr);
                return 0;
            }
        }
    }

    return 1;
}

int elf_load(page_directory_t *pd, const u8 *data, u32 size, uintptr_t *entry_out, uintptr_t *heap_start_out) {
    if (!elf_validate(data, size))
        return 0;

    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)data;
    uintptr_t max_vaddr = 0;

    for (u32 i = 0; i < ehdr->e_phnum; i++) {
        const Elf32_Phdr *phdr = (const Elf32_Phdr *)(data + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type != PT_LOAD || phdr->p_memsz == 0)
            continue;

        uintptr_t seg_start = phdr->p_vaddr;
        uintptr_t seg_end = seg_start + phdr->p_memsz;
        if (seg_end > max_vaddr)
            max_vaddr = seg_end;

        uintptr_t vpage_start = seg_start & ~0xFFFu;
        uintptr_t vpage_end = (seg_end + 4095u) & ~0xFFFu;

        u32 page_flags = PAGE_PRESENT | PAGE_USER;
        if (phdr->p_flags & PF_W)
            page_flags |= PAGE_RW;

        for (uintptr_t vpage = vpage_start; vpage < vpage_end; vpage += PAGE_SIZE) {
            uintptr_t ppage = 0;
            u32 existing_flags = 0;
            if (!get_mapping_flags(pd, vpage, &ppage, &existing_flags)) {
                ppage = pmm_alloc_page();
                if (!ppage) {
                    KLOG_ERROR(KLOG_CAT_PROC, "elf_load: out of physical memory");
                    return 0;
                }
                memset((void *)ppage, 0, PAGE_SIZE);
                map_page(pd, vpage, ppage, page_flags);
            } else {
                map_page(pd, vpage, ppage, existing_flags | page_flags);
            }

            /* Copy file payload into the mapped physical page */
            uintptr_t page_offset_start = 0;
            uintptr_t page_offset_end = PAGE_SIZE;

            if (seg_start > vpage)
                page_offset_start = seg_start - vpage;

            uintptr_t file_end = seg_start + phdr->p_filesz;
            if (file_end < vpage + PAGE_SIZE) {
                if (file_end > vpage)
                    page_offset_end = file_end - vpage;
                else
                    page_offset_end = 0;
            }

            if (page_offset_end > page_offset_start) {
                u32 copy_len = page_offset_end - page_offset_start;
                u32 file_src_off = phdr->p_offset + ((vpage + page_offset_start) - seg_start);
                memcpy((u8 *)ppage + page_offset_start, data + file_src_off, copy_len);
            }
        }
    }

    if (entry_out)
        *entry_out = ehdr->e_entry;
    if (heap_start_out)
        *heap_start_out = (max_vaddr + 4095u) & ~0xFFFu;

    klog_hex(KLOG_CAT_PROC, "ELF loaded successfully. Entry point: ", ehdr->e_entry);
    return 1;
}
