#ifndef POLLIK_X64_NETWORK_H
#define POLLIK_X64_NETWORK_H
#include <stdint.h>
void network64_init(void);
void network64_poll(void);
int network64_connected(void);
void network64_set_airplane(int enabled);
#include "user.h"
int64_t network64_dispatch(Process64 *process,UserFrame *frame);
void network64_owner_cleanup(uint64_t owner_pid);
#endif
