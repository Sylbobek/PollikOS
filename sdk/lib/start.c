/* crt0 entry: consume the PollikOS startup ABI v1 registers, validate the
 * version, publish environ, run main and turn its value into the exit status.
 * Applications only define main; _start lives in crt0.asm. */
#include <stdlib.h>
#include <stdint.h>
#include <pollikos/syscall.h>
extern int main(int argc, char **argv);
char **environ;
void __libc_start(long argc, char **argv, char **envp, long version) {
    environ = envp;
    if (version != (long)STARTUP_VERSION || argc < 0 || argc > 4096 || !argv || !envp) {
        static const char message[] = "pollikc: unsupported startup ABI\n";
        (void)__pollikos_syscall3(USER_WRITE, 2, (uint64_t)(uintptr_t)message, sizeof(message)-1);
        _exit(127);
    }
    exit(main((int)argc, argv));
}
