#ifndef POLLIK_TRASH_H
#define POLLIK_TRASH_H

#include "system.h"

#define TRASH_MAX_RECORDS 32

typedef struct {
    char original_path[128];
    char trash_name[64];
    u32 deletion_time;
} TrashRecord;

void trash_init(void);
int trash_move_item(const char *src_path);
int trash_restore_item(const char *trash_name);
int trash_delete_permanent(const char *trash_name);
int trash_empty(void);
int trash_has_items(void);
int trash_count(void);
const TrashRecord *trash_get_record(int idx);

#endif
