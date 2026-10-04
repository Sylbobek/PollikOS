#ifndef POLLIK_PATH64_H
#define POLLIK_PATH64_H
#include "user.h"
int64_t path64_resolve(const char *cwd, const char *request, char out[USER_PATH_MAX]);
int64_t path64_user(Process64 *p, uint64_t address, char out[USER_PATH_MAX]);
#endif
