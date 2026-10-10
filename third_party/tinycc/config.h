#ifndef TCC_CONFIG_H
#define TCC_CONFIG_H
#define TCC_VERSION "0.9.27-pollikos"
#define TCC_GITHASH "d348a9a51d32cece842b7885d27a411436d7887b"
#define CONFIG_TCCDIR "/usr/lib/tcc"
#define CONFIG_TCC_SYSINCLUDEPATHS "/usr/include:{B}/include"
#define CONFIG_TCC_LIBPATHS "/usr/lib:{B}/lib"
#define CONFIG_TCC_CRTPREFIX "/usr/lib"
#define CONFIG_TCC_ELFINTERP "-"
#define CONFIG_TCC_STATIC 1
#define TCC_TARGET_X86_64 1
#define TCC_LIBTCC1 "libtcc1.a"
#define CONFIG_TCC_POLLIKOS 1

#ifdef __clang__
# pragma clang diagnostic ignored "-Wsign-compare"
# pragma clang diagnostic ignored "-Wstring-plus-int"
# pragma clang diagnostic ignored "-Wunused-parameter"
# pragma clang diagnostic ignored "-Wunused-variable"
# pragma clang diagnostic ignored "-Wunused-label"
# pragma clang diagnostic ignored "-Wunused-function"
# pragma clang diagnostic ignored "-Wpointer-sign"
# pragma clang diagnostic ignored "-Wunterminated-string-initialization"
#endif
#endif
