#ifndef POLLIKOS_ALLOCA_H
#define POLLIKOS_ALLOCA_H
#define alloca(bytes) __builtin_alloca(bytes)
#endif
