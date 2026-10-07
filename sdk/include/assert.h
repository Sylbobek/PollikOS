#ifndef POLLIKOS_ASSERT_H
#define POLLIKOS_ASSERT_H
#include <stdlib.h>
#ifdef NDEBUG
#define assert(condition) ((void)0)
#else
#define assert(condition) ((condition)?(void)0:abort())
#endif
#endif
