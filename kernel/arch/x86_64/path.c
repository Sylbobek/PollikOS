#include "path.h"
/* Lexical normalization, then one VFS lookup. Fixed process-owned cwd is already
 * canonical. No filesystem mutation or symlinks exist in the current target. */
int64_t path64_resolve(const char *cwd, const char *request, char out[USER_PATH_MAX]) {
    if (!cwd || !request || !request[0]) return -USER_EINVAL;
    size_t length = 1;
    out[0] = '/'; out[1] = 0;
    if (request[0] != '/') {
        while (length < USER_PATH_MAX && cwd[length]) { out[length] = cwd[length]; ++length; }
        if (length == USER_PATH_MAX) return -USER_ENAMETOOLONG;
        out[length] = 0;
    }
    size_t cursor = 0;
    while (request[cursor]) {
        if (cursor >= USER_PATH_MAX-1) return -USER_ENAMETOOLONG;
        if (request[cursor] == '/') { ++cursor; continue; }
        size_t start = cursor;
        while (cursor < USER_PATH_MAX && request[cursor] && request[cursor] != '/') ++cursor;
        if (cursor == USER_PATH_MAX) return -USER_ENAMETOOLONG;
        size_t count = cursor-start;
        if (count == 1 && request[start] == '.') continue;
        if (count == 2 && request[start] == '.' && request[start+1] == '.') {
            while (length > 1 && out[length-1] != '/') --length;
            if (length > 1) --length;
            out[length] = 0;
            continue;
        }
        if (count > USER_DIRENT_NAME_MAX || count+(length>1)+length >= USER_PATH_MAX)
            return -USER_ENAMETOOLONG;
        if (length > 1) out[length++] = '/';
        for (size_t i = start; i < cursor; ++i) out[length++] = request[i];
        out[length] = 0;
    }
    return 0;
}
int64_t path64_user(Process64 *p, uint64_t address, char out[USER_PATH_MAX]) {
    char request[USER_PATH_MAX];
    UserCopyResult copied = copy_string_from_user64(&p->space, request, address, sizeof(request));
    if (copied != USER_COPY_OK)
        return copied == USER_COPY_TOO_LONG ? -USER_ENAMETOOLONG : -USER_EFAULT;
    return path64_resolve(p->cwd, request, out);
}
