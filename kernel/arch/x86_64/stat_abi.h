#ifndef POLLIK_STAT64_ABI_H
#define POLLIK_STAT64_ABI_H
#include <stdint.h>
#include <stddef.h>
#include "user_abi.h"
/* Version 1 is permanently 64 bytes. Future layouts use a new operation or
 * explicit negotiated capacity, never a larger implicit copy by these calls. */
typedef struct {
    uint32_t version, struct_size, type, timestamp_kind;
    uint64_t size_bytes, inode, created_ticks, modified_ticks;
    uint64_t reserved[2];
} UserStat64;
_Static_assert(sizeof(UserStat64) == USER_STAT_SIZE, "stat ABI size");
_Static_assert(offsetof(UserStat64, version) == 0, "stat version");
_Static_assert(offsetof(UserStat64, struct_size) == 4, "stat size");
_Static_assert(offsetof(UserStat64, type) == 8, "stat type");
_Static_assert(offsetof(UserStat64, timestamp_kind) == 12, "stat clock");
_Static_assert(offsetof(UserStat64, size_bytes) == 16, "stat bytes");
_Static_assert(offsetof(UserStat64, inode) == 24, "stat inode");
_Static_assert(offsetof(UserStat64, created_ticks) == 32, "stat created");
_Static_assert(offsetof(UserStat64, modified_ticks) == 40, "stat modified");
_Static_assert(offsetof(UserStat64, reserved) == 48, "stat reserved");
#endif
