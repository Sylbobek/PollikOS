/* Directory enumeration example over the public PollikOS directory API. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pollikos/fs.h>
int main(void) {
    DIR *directory = opendir("/bin");
    if (!directory) {
        printf("[sdk] dirs: opendir failed: %s\n", strerror(errno));
        return 1;
    }
    int entries = 0, executables = 0;
    pollikos_dirent_t *entry;
    while ((entry = readdir(directory)) != NULL) {
        ++entries;
        if (entry->type == POLLIKOS_TYPE_REGULAR) ++executables;
        printf("[sdk] dir %s%s\n", entry->name,
               entry->type == POLLIKOS_TYPE_DIRECTORY ? "/" : "");
    }
    if (closedir(directory) != 0) return 2;
    printf("[sdk] dirs: %d entries, %d executables\n", entries, executables);
    return entries > 0 ? 0 : 3;
}
