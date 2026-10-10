#include <stdint.h>
#define PRIx8 "x"
#define PRIx16 "x"
#define PRIx32 "x"
#ifdef POLLIK_X64
#define PRIx64 "lx"
#define PRIu64 "lu"
#define PRId64 "ld"
#define PRIxPTR "lx"
#define PRIuPTR "lu"
#else
#define PRIx64 "llx"
#define PRIu64 "llu"
#define PRId64 "lld"
#define PRIxPTR "x"
#define PRIuPTR "u"
#endif
#define PRIu8 "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRId32 "d"
