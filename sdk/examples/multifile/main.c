/* Multi-file SDK example. Build:
 *   sdk/tools/pollikcc.ps1 sdk/examples/multifile/main.c \
 *       sdk/examples/multifile/utils.c sdk/examples/multifile/parser.c -o multifile.elf */
#include <stdio.h>
#include "utils.h"
#include "parser.h"
int main(void) {
    int ok = 0;
    long parsed = example_parse_decimal("12345", &ok);
    if (!ok || parsed != 12345) return 1;
    (void)example_parse_decimal("12x", &ok);
    if (ok) return 2;
    if (example_checksum("pollikos") != 877) return 3;
    if (example_clamp(9, 0, 5) != 5 || example_clamp(-3, 0, 5) != 0) return 4;
    printf("[sdk] multifile: parsed=%ld checksum=%lu\n", parsed, example_checksum("pollikos"));
    return 0;
}
