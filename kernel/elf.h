#ifndef POLLIK_ELF_H
#define POLLIK_ELF_H

#include "system.h"
#include "vmm.h"

#define ELF_MAGIC0 0x7Fu
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define ET_EXEC 2
#define EM_386  3

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6

#define PF_X 0x1u
#define PF_W 0x2u
#define PF_R 0x4u

typedef struct __attribute__((packed)) {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u32 e_entry;
    u32 e_phoff;
    u32 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} Elf32_Ehdr;

typedef struct __attribute__((packed)) {
    u32 p_type;
    u32 p_offset;
    u32 p_vaddr;
    u32 p_paddr;
    u32 p_filesz;
    u32 p_memsz;
    u32 p_flags;
    u32 p_align;
} Elf32_Phdr;

int elf_validate(const u8 *data, u32 size);
int elf_load(page_directory_t *pd, const u8 *data, u32 size, uintptr_t *entry_out, uintptr_t *heap_start_out);

#endif
