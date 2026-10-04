/* C7 compiler-like orchestration driver: multi-stage tools, concurrent
 * helpers, large argv and temporary-file isolation. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <pollikos/process.h>
static int failures;
#define CHECK(condition, name) \
    do { if (!(condition)) { printf("[C7] FAIL: %s (errno=%s)\n", name, strerror(errno)); ++failures; } } while (0)
#define WORK "/tmp/c7"
static int write_text(const char *path, const char *text) {
    FILE *stream = fopen(path, "wb");
    if (!stream) return -1;
    size_t length = strlen(text);
    if (fwrite(text, 1, length, stream) != length) { fclose(stream); return -1; }
    return fclose(stream) == 0 ? 0 : -1;
}
static long read_text(const char *path, char *buffer, size_t capacity) {
    FILE *stream = fopen(path, "rb");
    if (!stream) return -1;
    size_t got = fread(buffer, 1, capacity-1, stream);
    fclose(stream);
    buffer[got] = 0;
    return (long)got;
}
static int wait_tool(long child) {
    pollikos_wait_t status;
    long result = pollikos_waitpid(child, &status);
    if (result != child) return -1;
    if (status.kind != POLLIKOS_WAIT_EXITED || status.code != 42) return -2;
    return 0;
}
static int mode_chain(void) {
    CHECK(write_text(WORK "/src.txt", "hello compiler\n") == 0, "write source");
    const char *stage1[] = {"tool_stage_c", "stage1", WORK "/src.txt", WORK "/mid.txt", 0};
    long child = pollikos_spawn("/bin/tool_stage_c", stage1, NULL);
    CHECK(child > 0, "spawn stage1");
    if (child < 0) return 1;
    CHECK(wait_tool(child) == 0, "stage1 exit 42");
    const char *stage2[] = {"tool_stage_c", "stage2", WORK "/mid.txt", WORK "/final.txt", 0};
    child = pollikos_spawn("/bin/tool_stage_c", stage2, NULL);
    CHECK(child > 0, "spawn stage2");
    if (child < 0) return 1;
    CHECK(wait_tool(child) == 0, "stage2 exit 42");
    char buffer[256];
    long length = read_text(WORK "/final.txt", buffer, sizeof(buffer));
    CHECK(length > 0 && strncmp(buffer, "FINAL:", 6) == 0 &&
          strstr(buffer, "HELLO COMPILER") != NULL, "final output transformed");
    printf("[c7] chain produced %ld bytes\n", length);
    remove(WORK "/src.txt"); remove(WORK "/mid.txt"); remove(WORK "/final.txt");
    return failures ? 1 : 42;
}
static int mode_parallel(void) {
    long children[8];
    for (unsigned index = 0; index < 8; ++index) {
        char input[64], output[64], text[64];
        snprintf(input, sizeof(input), WORK "/p%u.in", index);
        snprintf(output, sizeof(output), WORK "/p%u.out", index);
        snprintf(text, sizeof(text), "batch %u payload\n", index);
        CHECK(write_text(input, text) == 0, "parallel input");
        const char *arguments[5] = {"tool_stage_c", "stage1", input, output, 0};
        children[index] = pollikos_spawn("/bin/tool_stage_c", arguments, NULL);
        CHECK(children[index] > 0, "parallel spawn");
    }
    for (unsigned index = 0; index < 8; ++index) {
        if (children[index] < 0) continue;
        CHECK(wait_tool(children[index]) == 0, "parallel helper exit 42");
        char input[64], output[64], buffer[64];
        snprintf(input, sizeof(input), WORK "/p%u.in", index);
        snprintf(output, sizeof(output), WORK "/p%u.out", index);
        snprintf(buffer, sizeof(buffer), "BATCH %u PAYLOAD\n", index);
        char actual[64];
        long length = read_text(output, actual, sizeof(actual));
        CHECK(length == (long)strlen(buffer) && strcmp(actual, buffer) == 0,
              "parallel output exact");
        remove(input); remove(output);
    }
    return failures ? 1 : 42;
}
static int mode_temp(void) {
    long children[4];
    for (unsigned index = 0; index < 4; ++index) {
        const char *arguments[2] = {"tool_stage_c", "tempfile"};
        children[index] = pollikos_spawn("/bin/tool_stage_c", arguments, NULL);
        CHECK(children[index] > 0, "temp helper spawn");
    }
    for (unsigned index = 0; index < 4; ++index)
        if (children[index] > 0) CHECK(wait_tool(children[index]) == 0, "temp helper exit 42");
    DIR *directory = opendir(WORK);
    CHECK(directory != NULL, "temp dir open");
    int leftovers = 0;
    if (directory) {
        struct dirent *entry;
        while ((entry = readdir(directory)) != NULL)
            if (strncmp(entry->d_name, "tmp-", 4) == 0) ++leftovers;
        closedir(directory);
    }
    CHECK(leftovers == 0, "temporary files cleaned up");
    return failures ? 1 : 42;
}
static int mode_args(void) {
    const char *arguments[62];
    arguments[0] = "tool_stage_c";
    arguments[1] = "argcheck";
    char storage[60][8];
    for (unsigned index = 2; index < 62; ++index) {
        snprintf(storage[index-2], sizeof(storage[index-2]), "arg%02u", index);
        arguments[index] = storage[index-2];
    }
    arguments[61] = 0;
    /* argc = 61 (program, argcheck, arg02..arg60). */
    long child = pollikos_spawn("/bin/tool_stage_c", arguments, NULL);
    CHECK(child > 0, "large argv spawn");
    if (child < 0) return 1;
    CHECK(wait_tool(child) == 0, "large argv exit 42");
    return failures ? 1 : 42;
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "chain";
    if (mkdir(WORK, 0755) != 0 && errno != EEXIST) return 1;
    if (strcmp(mode, "chain") == 0) return mode_chain();
    if (strcmp(mode, "parallel") == 0) return mode_parallel();
    if (strcmp(mode, "temp") == 0) return mode_temp();
    if (strcmp(mode, "args") == 0) return mode_args();
    printf("[C7] unknown driver mode %s\n", mode);
    return 1;
}
