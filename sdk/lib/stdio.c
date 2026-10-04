/* PollikOS libc stdio: a small deterministic FILE layer over descriptors plus
 * the shared integer-only formatter. fopen'd streams use a fixed 512-byte
 * buffer; the standard streams are unbuffered so console output ordering is
 * unchanged. All state is userspace and process-owned. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pollikos/syscall.h>
#include <pollikos/fs.h>
#define STREAM_BUFFER 512
/* Kernel read syscalls cap at USER_READ_MAX (4096); larger requests are
 * split so callers can read or write arbitrary sizes through one FILE. */
#define STREAM_IO_CHUNK 4096
static FILE standard_input = {0, POLLIK_F_READ, 0, 0, -1, 0, 0, 0, 0, 0, 0};
static FILE standard_output = {1, POLLIK_F_WRITE, 0, 0, -1, 0, 0, 0, 0, 0, 0};
static FILE standard_error = {2, POLLIK_F_WRITE, 0, 0, -1, 0, 0, 0, 0, 0, 0};
FILE *stdout = &standard_output;
FILE *stderr = &standard_error;
FILE *stdin = &standard_input;
/* Open fopen'd streams are registered so fflush(NULL) and exit can flush
 * buffered writers even when the program forgets fclose. */
static FILE *stream_list;
static void registry_add(FILE *stream) {
    stream->next = stream_list;
    stream_list = stream;
}
static void registry_remove(FILE *stream) {
    FILE **link = &stream_list;
    while (*link && *link != stream) link = &(*link)->next;
    if (*link) *link = stream->next;
}
FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode) { errno = EINVAL; return NULL; }
    int flags;
    unsigned stream_flags;
    if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
        flags = O_RDONLY; stream_flags = POLLIK_F_READ;
    } else if (strcmp(mode, "r+") == 0 || strcmp(mode, "rb+") == 0 || strcmp(mode, "r+b") == 0) {
        flags = O_RDWR; stream_flags = POLLIK_F_READ|POLLIK_F_WRITE;
    } else if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
        flags = O_WRONLY|O_CREAT|O_TRUNC; stream_flags = POLLIK_F_WRITE;
    } else if (strcmp(mode, "w+") == 0 || strcmp(mode, "wb+") == 0 || strcmp(mode, "w+b") == 0) {
        flags = O_RDWR|O_CREAT|O_TRUNC; stream_flags = POLLIK_F_READ|POLLIK_F_WRITE;
    } else if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
        flags = O_WRONLY|O_CREAT|O_APPEND; stream_flags = POLLIK_F_WRITE|POLLIK_F_APPEND;
    } else if (strcmp(mode, "a+") == 0 || strcmp(mode, "ab+") == 0 || strcmp(mode, "a+b") == 0) {
        flags = O_RDWR|O_CREAT|O_APPEND;
        stream_flags = POLLIK_F_READ|POLLIK_F_WRITE|POLLIK_F_APPEND;
    } else { errno = EINVAL; return NULL; }
    int fd = open(path, flags);
    if (fd < 0) return NULL;
    FILE *stream = malloc(sizeof(FILE));
    unsigned char *buffer = malloc(STREAM_BUFFER);
    if (!stream || !buffer) {
        if (stream) free(stream);
        if (buffer) free(buffer);
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    stream->fd = fd;
    stream->flags = stream_flags;
    stream->error = 0;
    stream->eof = 0;
    stream->pushback = -1;
    stream->buffer = buffer;
    stream->capacity = STREAM_BUFFER;
    stream->pending = 0;
    stream->position = 0;
    stream->limit = 0;
    stream->next = 0;
    registry_add(stream);
    return stream;
}
FILE *fdopen(int fd, const char *mode) {
    if (fd < 0 || !mode) { errno = EINVAL; return NULL; }
    unsigned flags;
    if (!strcmp(mode, "r") || !strcmp(mode, "rb")) flags = POLLIK_F_READ;
    else if (!strcmp(mode, "w") || !strcmp(mode, "wb")) flags = POLLIK_F_WRITE;
    else if (!strcmp(mode, "r+") || !strcmp(mode, "w+") ||
             !strcmp(mode, "rb+") || !strcmp(mode, "wb+")) flags = POLLIK_F_READ|POLLIK_F_WRITE;
    else { errno = EINVAL; return NULL; }
    FILE *stream = malloc(sizeof(FILE));
    unsigned char *buffer = malloc(STREAM_BUFFER);
    if (!stream || !buffer) { free(stream); free(buffer); errno = ENOMEM; return NULL; }
    *stream = (FILE){fd, flags, 0, 0, -1, buffer, STREAM_BUFFER, 0, 0, 0, 0};
    registry_add(stream);
    return stream;
}
int fileno(FILE *stream) { if (!stream) { errno = EBADF; return -1; } return stream->fd; }
static int flush_output(FILE *stream) {
    if (!(stream->flags & POLLIK_F_WRITE) || !stream->pending) return 0;
    size_t offset = 0;
    while (offset < stream->pending) {
        long written = pollikos_write(stream->fd, stream->buffer+offset, stream->pending-offset);
        if (written <= 0) {
            stream->error = 1;
            if (written < 0) (void)__pollikos_fail(written);
            else errno = EIO;
            return -1;
        }
        offset += (size_t)written;
    }
    stream->pending = 0;
    return 0;
}
static int fill_input(FILE *stream) {
    if (!stream->capacity) return 0;
    long got = pollikos_read(stream->fd, stream->buffer, stream->capacity);
    if (got < 0) {
        stream->error = 1;
        (void)__pollikos_fail(got);
        return 0;
    }
    stream->position = 0;
    stream->limit = (size_t)got;
    if (!got) stream->eof = 1;
    return got > 0;
}
int fflush(FILE *stream) {
    if (!stream) {
        int failure = flush_output(&standard_output) | flush_output(&standard_error);
        for (FILE *entry = stream_list; entry; entry = entry->next) {
            if (flush_output(entry)) failure = -1;
        }
        return failure;
    }
    return flush_output(stream);
}
int fclose(FILE *stream) {
    if (!stream) { errno = EBADF; return EOF; }
    int result = flush_output(stream);
    if (close(stream->fd) != 0) result = EOF;
    registry_remove(stream);
    free(stream->buffer);
    free(stream);
    return result;
}
size_t fread(void *buffer, size_t size, size_t count, FILE *stream) {
    if (!stream) { errno = EBADF; return 0; }
    if (!(stream->flags & POLLIK_F_READ)) { stream->error = 1; errno = EBADF; return 0; }
    if (!size || !count) return 0;
    if (count > SIZE_MAX/size) { errno = EOVERFLOW; stream->error = 1; return 0; }
    size_t total = size*count, done = 0;
    unsigned char *out = buffer;
    while (done < total) {
        if (stream->pushback >= 0) {
            out[done++] = (unsigned char)stream->pushback;
            stream->pushback = -1;
            continue;
        }
        if (stream->position < stream->limit) {
            out[done++] = stream->buffer[stream->position++];
            continue;
        }
        if (stream->eof) break;
        size_t remaining = total-done;
        if (!stream->capacity || remaining >= stream->capacity) {
            size_t request = remaining > STREAM_IO_CHUNK ? STREAM_IO_CHUNK : remaining;
            long got = pollikos_read(stream->fd, out+done, request);
            if (got < 0) { stream->error = 1; (void)__pollikos_fail(got); break; }
            if (!got) { stream->eof = 1; break; }
            done += (size_t)got;
            continue;
        }
        if (!fill_input(stream)) break;
    }
    return done/size;
}
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream) {
    if (!stream) { errno = EBADF; return 0; }
    if (!(stream->flags & POLLIK_F_WRITE)) { stream->error = 1; errno = EBADF; return 0; }
    if (!size || !count) return 0;
    if (count > SIZE_MAX/size) { errno = EOVERFLOW; stream->error = 1; return 0; }
    size_t total = size*count, done = 0;
    const unsigned char *in = buffer;
    while (done < total) {
        if (!stream->capacity || total-done >= stream->capacity) {
            if (flush_output(stream)) break;
            size_t request = total-done > STREAM_IO_CHUNK ? STREAM_IO_CHUNK : total-done;
            long written = pollikos_write(stream->fd, in+done, request);
            if (written <= 0) {
                stream->error = 1;
                if (written < 0) (void)__pollikos_fail(written);
                else errno = EIO;
                break;
            }
            done += (size_t)written;
            continue;
        }
        size_t space = stream->capacity-stream->pending;
        size_t chunk = total-done < space ? total-done : space;
        memcpy(stream->buffer+stream->pending, in+done, chunk);
        stream->pending += chunk;
        done += chunk;
        if (stream->pending == stream->capacity && flush_output(stream)) break;
    }
    return done/size;
}
int fseek(FILE *stream, long offset, int whence) {
    if (!stream) { errno = EBADF; return -1; }
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) { errno = EINVAL; return -1; }
    if (flush_output(stream)) return -1;
    stream->position = stream->limit = 0;
    stream->pushback = -1;
    long result = pollikos_seek(stream->fd, offset, whence);
    if (result < 0) { (void)__pollikos_fail(result); return -1; }
    stream->eof = 0;
    return 0;
}
long ftell(FILE *stream) {
    if (!stream) { errno = EBADF; return -1; }
    long base = pollikos_seek(stream->fd, 0, SEEK_CUR);
    if (base < 0) { (void)__pollikos_fail(base); return -1; }
    long logical = base - (long)(stream->limit-stream->position);
    if (stream->pushback >= 0) --logical;
    return logical + (long)stream->pending;
}
void rewind(FILE *stream) {
    if (!stream) return;
    (void)fseek(stream, 0, SEEK_SET);
    stream->error = 0;
    stream->eof = 0;
}
int feof(FILE *stream) { return stream ? stream->eof : EOF; }
int ferror(FILE *stream) { return stream ? stream->error : EOF; }
void clearerr(FILE *stream) { if (stream) { stream->eof = 0; stream->error = 0; } }
static int get_byte(FILE *stream) {
    unsigned char byte;
    if (fread(&byte, 1, 1, stream) != 1) return EOF;
    return (int)byte;
}
int fgetc(FILE *stream) { return stream ? get_byte(stream) : EOF; }
int getc(FILE *stream) { return fgetc(stream); }
int getchar(void) { return fgetc(stdin); }
int ungetc(int character, FILE *stream) {
    if (!stream || character == EOF) return EOF;
    if (stream->pushback >= 0) return EOF;
    stream->pushback = (unsigned char)character;
    stream->eof = 0;
    return (unsigned char)character;
}
int fputc(int character, FILE *stream) {
    if (!stream) { errno = EBADF; return EOF; }
    unsigned char byte = (unsigned char)character;
    if (fwrite(&byte, 1, 1, stream) != 1) return EOF;
    return (int)byte;
}
int putc(int character, FILE *stream) { return fputc(character, stream); }
int putchar(int character) { return fputc(character, stdout); }
char *fgets(char *buffer, int size, FILE *stream) {
    if (!buffer || size <= 0 || !stream) { errno = EINVAL; return NULL; }
    int index = 0;
    while (index < size-1) {
        int character = fgetc(stream);
        if (character == EOF) break;
        buffer[index++] = (char)character;
        if (character == '\n') break;
    }
    buffer[index] = 0;
    return index ? buffer : NULL;
}
int fputs(const char *text, FILE *stream) {
    if (!text || !stream) { errno = EINVAL; return EOF; }
    size_t length = strlen(text);
    if (length && fwrite(text, 1, length, stream) != length) return EOF;
    return 0;
}
int puts(const char *text) {
    if (fputs(text, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}
void perror(const char *prefix) {
    int error = errno;
    if (prefix && *prefix) fprintf(stderr, "%s: %s\n", prefix, strerror(error));
    else fprintf(stderr, "%s\n", strerror(error));
}
int remove(const char *path) {
    pollikos_stat_t info;
    long result = pollikos_stat(path, &info);
    if (result < 0) { (void)__pollikos_fail(result); return -1; }
    long outcome = info.type == POLLIKOS_TYPE_DIRECTORY ? pollikos_rmdir(path) : pollikos_unlink(path);
    return outcome < 0 ? (int)__pollikos_fail(outcome) : 0;
}
/* ----------------------------------------------------------- formatter -- */
typedef void (*emit_fn)(void *context, const char *data, size_t length);
typedef struct {
    char *buffer;
    size_t capacity;
    size_t written;
} buffer_sink;
static void emit_file(void *context, const char *data, size_t length) {
    FILE *stream = context;
    if (!length || stream->error) return;
    if (fwrite(data, 1, length, stream) != length) stream->error = 1;
}
static void emit_buffer(void *context, const char *data, size_t length) {
    buffer_sink *sink = context;
    for (size_t i = 0; i < length; ++i) {
        if (sink->capacity && sink->written+1 < sink->capacity) sink->buffer[sink->written] = data[i];
        sink->written++;
    }
}
static void emit_fill(emit_fn emit, void *context, char character, int count) {
    char block[32];
    memset(block, character, sizeof(block));
    while (count > 0) {
        int chunk = count > (int)sizeof(block) ? (int)sizeof(block) : count;
        emit(context, block, (size_t)chunk);
        count -= chunk;
    }
}
static int emit_number(emit_fn emit, void *context, uint64_t value, unsigned base,
                       int uppercase, int negative, int width, int zero, int left,
                       const char *prefix) {
    char digits[24];
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int count = 0;
    do { digits[count++] = alphabet[value % base]; value /= base; } while (value);
    int total = count + (negative ? 1 : 0) + (int)strlen(prefix);
    int padding = width > total ? width-total : 0;
    if (!left && !zero) emit_fill(emit, context, ' ', padding);
    emit(context, prefix, strlen(prefix));
    if (negative) emit(context, "-", 1);
    if (!left && zero) emit_fill(emit, context, '0', padding);
    while (count) emit(context, &digits[--count], 1);
    if (left) emit_fill(emit, context, ' ', padding);
    return total+padding;
}
static int format_core(emit_fn emit, void *context, const char *format, va_list arguments) {
    int written = 0;
    while (*format) {
        if (*format != '%') {
            const char *start = format;
            while (*format && *format != '%') ++format;
            emit(context, start, (size_t)(format-start));
            written += (int)(format-start);
            continue;
        }
        ++format;
        int left = 0, zero = 0;
        for (;;) {
            if (*format == '-') left = 1;
            else if (*format == '0') zero = 1;
            else break;
            ++format;
        }
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            if (width < 1000) width = width*10 + (*format-'0');
            ++format;
        }
        int wide = 0;
        if (*format == 'l') { wide = 1; ++format; if (*format == 'l') ++format; }
        else if (*format == 'z') { wide = 1; ++format; }
        switch (*format) {
        case '%':
            emit(context, "%", 1); ++written; break;
        case 'c': {
            char character = (char)va_arg(arguments, int);
            int padding = width > 1 ? width-1 : 0;
            if (!left) emit_fill(emit, context, ' ', padding);
            emit(context, &character, 1);
            if (left) emit_fill(emit, context, ' ', padding);
            written += 1+padding; break;
        }
        case 's': {
            const char *text = va_arg(arguments, const char *);
            if (!text) text = "(null)";
            size_t length = strlen(text);
            int padding = (size_t)width > length ? width-(int)length : 0;
            if (!left) emit_fill(emit, context, ' ', padding);
            emit(context, text, length);
            if (left) emit_fill(emit, context, ' ', padding);
            written += (int)length+padding; break;
        }
        case 'd': case 'i': {
            long long value = wide ? va_arg(arguments, long long) : (long long)va_arg(arguments, int);
            int negative = value < 0;
            uint64_t magnitude = negative ? (uint64_t)(-(value+1))+1u : (uint64_t)value;
            written += emit_number(emit, context, magnitude, 10, 0, negative, width, zero, left, "");
            break;
        }
        case 'u': case 'x': case 'X': {
            unsigned long long value = wide ? va_arg(arguments, unsigned long long)
                                            : (unsigned long long)va_arg(arguments, unsigned int);
            written += emit_number(emit, context, (uint64_t)value, (*format == 'u') ? 10 : 16,
                                   *format == 'X', 0, width, zero, left, "");
            break;
        }
        case 'p': {
            void *pointer = va_arg(arguments, void *);
            written += emit_number(emit, context, (uint64_t)(uintptr_t)pointer, 16, 0, 0,
                                   width, 0, left, "0x");
            break;
        }
        default:
            emit(context, "%", 1); ++written;
            if (*format) { emit(context, format, 1); ++written; }
            break;
        }
        if (*format) ++format;
    }
    return written;
}
int vfprintf(FILE *stream, const char *format, va_list arguments) {
    if (!stream) { errno = EBADF; return -1; }
    int result = format_core(emit_file, stream, format, arguments);
    return stream->error ? -1 : result;
}
int fprintf(FILE *stream, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}
int printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}
int vprintf(const char *format, va_list arguments) { return vfprintf(stdout, format, arguments); }
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments) {
    buffer_sink sink = {buffer, size, 0};
    int result = format_core(emit_buffer, &sink, format, arguments);
    if (size) {
        size_t end = sink.written < size ? sink.written : size-1;
        buffer[end] = 0;
    }
    return result > INT_MAX ? -1 : result;
}
int snprintf(char *buffer, size_t size, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}
int sprintf(char *buffer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, (size_t)-1, format, arguments);
    va_end(arguments);
    return result;
}
void __stdio_flush_all(void) {
    (void)fflush(NULL);
}
