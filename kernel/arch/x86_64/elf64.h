#ifndef POLLIK_ELF64_H
#define POLLIK_ELF64_H
#include "user.h"
#define ELF64_MAX_HEADERS 16
#define ELF64_MAX_PAGES 1024
#define ELF64_MAX_IMAGE (2*1024*1024)
typedef enum {
    ELF64_OK, ELF64_FORMAT, ELF64_UNSUPPORTED, ELF64_RANGE, ELF64_ALIGNMENT,
    ELF64_OVERLAP, ELF64_ENTRY, ELF64_LIMIT, ELF64_NOMEM, ELF64_CONFLICT, ELF64_ARGUMENTS
} Elf64Result;
typedef struct __attribute__((packed)) {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} Elf64Header;
typedef struct __attribute__((packed)) {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} Elf64ProgramHeader;
_Static_assert(sizeof(Elf64Header) == 64 && sizeof(Elf64ProgramHeader) == 56, "ELF64 wire format");
Elf64Result elf64_validate(const void *image, size_t size);
/* Immutable bounded kernel byte image; never a raw user pointer. Maps owned
 * pages transactionally and leaves all pre-existing mappings untouched. */
Elf64Result elf64_load(AddressSpace *space, const void *image, size_t size, virt_addr_t *entry);
Process64 *process64_create_elf(const void *image, size_t size, size_t argc,
                               const char *const *argv, size_t envc, const char *const *envp,
                               Elf64Result *error);
int elf64_demo(void);
#ifdef SELFTEST
void elf64_selftest(void);
#endif
#endif
