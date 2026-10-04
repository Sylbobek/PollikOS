/* Long-running optimized integer workload used for scheduler stress. The
 * kernel kills it at a tick limit; globals, heap contents and PID must stay
 * correct until then. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pollikos/process.h>
static volatile uint64_t counter;
static uint64_t *heap_block;
int main(void) {
    heap_block = malloc(4096);
    if (!heap_block) return 1;
    heap_block[0] = 0x5a5a5a5a5a5a5a5aul;
    for (unsigned index = 1; index < 512; ++index) heap_block[index] = index;
    uint64_t pid = (uint64_t)getpid();
    for (;;) {
        counter++;
        if ((counter & 0xfffff) == 0) {
            if (heap_block[0] != 0x5a5a5a5a5a5a5a5aul) return 2;
            if (heap_block[511] != 511) return 3;
            if ((uint64_t)getpid() != pid) return 4;
        }
    }
}
