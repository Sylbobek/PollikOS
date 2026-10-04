#ifndef POLLIKOS_STDIO_H
#define POLLIKOS_STDIO_H
#include <stddef.h>
#include <stdarg.h>
#include <pollikos/types.h>
#define EOF (-1)
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
/* Small deterministic FILE layer over PollikOS descriptors. fopen'd streams
 * use a fixed 512-byte buffer; stdin/stdout/stderr stay unbuffered so console
 * output ordering is unchanged. No host stdio state exists. */
typedef struct pollikos_file {
    int fd;
    unsigned flags;            /* POLLIK_F_* */
    int error, eof;
    int pushback;              /* one-byte ungetc slot, -1 when empty */
    unsigned char *buffer;     /* owned 512-byte buffer, NULL for std streams */
    size_t capacity;
    size_t pending;            /* buffered output bytes awaiting flush */
    size_t position, limit;    /* input cursor and filled length */
    struct pollikos_file *next; /* libc flush registry, managed by fopen/fclose */
} FILE;
#define POLLIK_F_READ  1u
#define POLLIK_F_WRITE 2u
#define POLLIK_F_APPEND 4u
extern FILE *stdout;
extern FILE *stderr;
extern FILE *stdin;
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fileno(FILE *stream);
int fclose(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fflush(FILE *stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
int getchar(void);
int ungetc(int character, FILE *stream);
int fputc(int character, FILE *stream);
int putc(int character, FILE *stream);
int putchar(int character);
char *fgets(char *buffer, int size, FILE *stream);
int fputs(const char *text, FILE *stream);
int puts(const char *text);
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list arguments);
int vfprintf(FILE *stream, const char *format, va_list arguments);
int snprintf(char *buffer, size_t size, const char *format, ...);
int sprintf(char *buffer, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);
int remove(const char *path);
void perror(const char *prefix);
void __stdio_flush_all(void);
#endif
