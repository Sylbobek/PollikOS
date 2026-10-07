#ifndef POLLIK_X64_NETWORK_H
#define POLLIK_X64_NETWORK_H
#include <stdint.h>
void network64_init(void);
void network64_poll(void);
int network64_connected(void);
void network64_set_airplane(int enabled);
int64_t network64_http_open(uint64_t owner_pid, const char *url);
int64_t network64_http_read(uint64_t owner_pid, uint64_t handle, uint8_t *buffer, int capacity);
int64_t network64_http_close(uint64_t owner_pid, uint64_t handle);
void network64_http_owner_cleanup(uint64_t owner_pid);
#endif
