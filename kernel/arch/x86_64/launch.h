#ifndef POLLIK_LAUNCH64_H
#define POLLIK_LAUNCH64_H
#include "elf64.h"
#define LAUNCH64_MAX_IMAGE (2*1024*1024)
typedef enum { LAUNCH_OK, LAUNCH_BAD_REQUEST, LAUNCH_NOT_FOUND, LAUNCH_NOT_FILE,
    LAUNCH_DENIED, LAUNCH_IO, LAUNCH_CORRUPT_FS, LAUNCH_BAD_SIZE, LAUNCH_TOO_LARGE,
    LAUNCH_INVALID_ELF, LAUNCH_UNSUPPORTED_ELF, LAUNCH_NOMEM, LAUNCH_TABLE_FULL,
    LAUNCH_QUEUE, LAUNCH_NOT_MOUNTED } Launch64Result;
/* BSP/IF=0, kernel-owned request pointers. Copies argv/envp/name before return.
 * On success the scheduler owns the process. out may be omitted. */
Launch64Result process64_launch_path(const char *path, size_t argc, const char *const *argv,
    size_t envc, const char *const *envp, Process64 **out);
/* Shared loader: builds a READY process without inserting it into the
 * scheduler. Used by process64_launch_path and the userspace spawn syscall. */
Launch64Result launch64_create(const char *path, size_t argc, const char *const *argv,
    size_t envc, const char *const *envp, Process64 **out);
const char *launch64_error_name(Launch64Result result);
#ifdef SELFTEST
void path64_selftest(void);
void disk64_load_regression_fixtures(void);
void disk64_release_regression_fixtures(void);
extern uint8_t *hello_elf_start, *schedule_elf_start;
extern size_t hello_elf_size, schedule_elf_size;
void launch64_read_inject(unsigned chunk, int64_t eof_after);
#endif
#endif
