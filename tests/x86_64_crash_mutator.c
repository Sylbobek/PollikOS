/* Guest workload for tests/x86_64_crash_consistency.py.
 * Each CRASH_STEP is written only after the named PollikFS mutation returned.
 * The short pause lets the host stop QEMU at deterministic operation
 * boundaries while each trial's target boundary is chosen by a fixed seed.
 */
#include <pollikos/fs.h>
#include <pollikos/time.h>
#include <unistd.h>

static unsigned int step;
static unsigned int step_limit;

static int report_step(const char *name) {
    char line[64];
    char digits[12];
    unsigned int value = ++step;
    unsigned int count = 0, length = 0;
    static const char prefix[] = "CRASH_STEP ";
    while (count < sizeof(prefix)-1) {
        line[length++] = prefix[count++];
    }
    count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < sizeof(digits));
    while (count) line[length++] = digits[--count];
    line[length++] = ' ';
    while (*name && length + 3 < sizeof(line)) line[length++] = *name++;
    line[length++] = '\r';
    line[length++] = '\n';
    if (write(1, line, length) != (ssize_t)length) return -1;
    sleep_ms(180);
    return 0;
}

static int failed(void) {
    static const char text[] = "CRASH_MUTATOR_FAIL\r\n";
    (void)write(1, text, sizeof(text)-1);
    return 1;
}

#define MUTATION(name) do { \
    if (report_step(name) != 0) return failed(); \
    if (step_limit && step >= step_limit) return 0; \
} while (0)

int main(int argc, char **argv) {
    static const char path_a[] = "/home/crash-a";
    static const char path_b[] = "/home/crash-b";
    static const char path_dir[] = "/home/crash-dir";
    static const char path_inner[] = "/home/crash-dir/inner";
    static const char path_target[] = "/home/crash-target";
    static const char append_data[] = "append-payload";
    static const char old_target[] = "old-target";
    char payload[1700];
    if (argc > 1) {
        for (const char *p = argv[1]; *p; ++p) {
            if (*p < '0' || *p > '9' || step_limit > (0xffffffffu-9u)/10u)
                return failed();
            step_limit = step_limit*10u + (unsigned)(*p-'0');
        }
        if (!step_limit) return failed();
    }
    for (unsigned int i = 0; i < sizeof(payload); ++i)
        payload[i] = (char)('A' + i % 26);

    for (;;) {
        int fd = open(path_a, O_WRONLY|O_CREAT|O_TRUNC);
        if (fd < 0) return failed();
        MUTATION("create");
        if (write(fd, payload, sizeof(payload)) != (ssize_t)sizeof(payload))
            return failed();
        MUTATION("write");
        if (close(fd) != 0) return failed();

        fd = open(path_a, O_WRONLY|O_APPEND);
        if (fd < 0) return failed();
        if (write(fd, append_data, sizeof(append_data)-1) !=
                (ssize_t)(sizeof(append_data)-1)) return failed();
        MUTATION("append");
        if (close(fd) != 0) return failed();

        if (mkdir(path_dir, 0755) != 0) return failed();
        MUTATION("mkdir");
        fd = open(path_inner, O_WRONLY|O_CREAT|O_TRUNC);
        if (fd < 0) return failed();
        MUTATION("create_inner");
        if (write(fd, payload, sizeof(payload)) != (ssize_t)sizeof(payload))
            return failed();
        MUTATION("write_inner");
        if (close(fd) != 0) return failed();

        fd = open(path_target, O_WRONLY|O_CREAT|O_TRUNC);
        if (fd < 0) return failed();
        MUTATION("create_target");
        if (write(fd, old_target, sizeof(old_target)-1) !=
                (ssize_t)(sizeof(old_target)-1)) return failed();
        MUTATION("write_target");
        if (close(fd) != 0) return failed();

        if (rename(path_a, path_b) != 0) return failed();
        MUTATION("rename");
        if (rename_replace(path_b, path_target) != 0) return failed();
        MUTATION("rename_replace");
        if (unlink(path_target) != 0) return failed();
        MUTATION("unlink");
        if (unlink(path_inner) != 0) return failed();
        MUTATION("unlink_inner");
        if (rmdir(path_dir) != 0) return failed();
        MUTATION("rmdir");
    }
}
