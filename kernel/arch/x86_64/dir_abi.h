#ifndef POLLIK_DIR64_ABI_H
#define POLLIK_DIR64_ABI_H
#include <stdint.h>
#include <stddef.h>
#include "user_abi.h"
/* These operations permanently copy 96 bytes; larger future layouts require
 * a new operation or explicit caller capacity/version negotiation. */
typedef struct {
    uint32_t version, struct_size, type, name_length;
    uint64_t inode, reserved;
    char name[USER_DIRENT_NAME_CAPACITY];
} UserDirent64;
_Static_assert(sizeof(UserDirent64) == USER_DIRENT_SIZE, "dirent ABI size");
_Static_assert(offsetof(UserDirent64, version) == 0, "dirent version");
_Static_assert(offsetof(UserDirent64, struct_size) == 4, "dirent size");
_Static_assert(offsetof(UserDirent64, type) == 8, "dirent type");
_Static_assert(offsetof(UserDirent64, name_length) == 12, "dirent length");
_Static_assert(offsetof(UserDirent64, inode) == 16, "dirent inode");
_Static_assert(offsetof(UserDirent64, reserved) == 24, "dirent reserved");
_Static_assert(offsetof(UserDirent64, name) == 32, "dirent name");
#endif
