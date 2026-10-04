/* Heap example: malloc, calloc, realloc and free with content checks. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pollikos/memory.h>
int main(void) {
    unsigned char *block = malloc(64);
    if (!block) return 1;
    memset(block, 0xab, 64);
    unsigned *zeroed = calloc(128, sizeof(unsigned));
    if (!zeroed) return 2;
    for (int index = 0; index < 128; ++index) if (zeroed[index]) return 3;
    char *text = malloc(32);
    if (!text) return 4;
    strcpy(text, "pollikos");
    char *grown = realloc(text, 256);
    if (!grown || strcmp(grown, "pollikos") != 0) return 5;
    memset(grown + 16, 'z', 240);
    char *shrunk = realloc(grown, 24);
    if (!shrunk || strcmp(shrunk, "pollikos") != 0) return 6;
    free(shrunk);
    free(zeroed);
    free(block);
    unsigned char *large = malloc(256 * 1024);
    if (!large) return 7;
    memset(large, 0x5a, 256 * 1024);
    if (large[0] != 0x5a || large[256 * 1024 - 1] != 0x5a) return 8;
    free(large);
    printf("[sdk] memory: heap bytes=%lu\n", (unsigned long)pollikos_heap_bytes());
    return 0;
}
