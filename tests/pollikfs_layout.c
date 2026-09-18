/* Compile-only ABI check against the real kernel headers (i386-none-elf). */
#include "../kernel/pollikfs.h"
_Static_assert(sizeof(PollikInode) == 60, "on-disk inode is 60 bytes, not 64");
_Static_assert(sizeof(PollikDirent) == 64, "directory entry ABI");
_Static_assert(sizeof(PollikSuperblock) == 512, "superblock ABI");
_Static_assert(POLLIK2_BLOCK_SIZE / sizeof(PollikInode) == 17, "17 slots per block");
_Static_assert((POLLIK2_INODE_COUNT + 16) / 17 == 31, "31 blocks, not floor 30");
_Static_assert(511 / 17 == 30, "inode 511 occupies table block 30");
