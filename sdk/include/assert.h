#ifndef POLLIKOS_ASSERT_H
#define POLLIKOS_ASSERT_H
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
void __pollikos_assert_fail(const char *expression, const char *file, int line)
    __attribute__((noreturn));
#define assert(expression) \
    ((expression) ? (void)0 : __pollikos_assert_fail(#expression, __FILE__, __LINE__))
#endif
#endif
