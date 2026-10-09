#ifndef POLLIK_SYSTEM_INFO_H
#define POLLIK_SYSTEM_INFO_H
#include <stdint.h>
#define POLLIK_SYSTEM_VERSION 1u
#define POLLIK_QUERY_SYSTEM 1u
#define POLLIK_QUERY_PROCESS 2u
typedef struct {
    uint32_t version,size,uid,rights;
    uint64_t session,uptime_ms,ram_bytes,ram_free_bytes,disk_bytes;
    uint32_t fs_block_size,fs_blocks,fs_free_blocks,fs_free_inodes;
    uint32_t process_count,process_limit,fs_readonly,reserved;
    char username[32];
} PollikSystemInfo;
typedef struct {
    uint32_t version,size,next_index,state;
    uint64_t pid,parent_pid,pgid,session,heap_bytes;
    uint32_t uid,rights;
    char name[56];
} PollikProcessInfo;
_Static_assert(sizeof(PollikSystemInfo)==120,"system snapshot ABI");
_Static_assert(sizeof(PollikProcessInfo)==120,"process snapshot ABI");
#endif
