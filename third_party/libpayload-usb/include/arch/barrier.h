/* x86 coherent DMA: compiler ordering plus a locked instruction fence. */
#define mb() __asm__ volatile("lock; addl $0,(%%esp)":::"memory","cc")
#if defined(__x86_64__)
#undef mb
#define mb() __asm__ volatile("mfence":::"memory")
#endif
#define rmb() mb()
#define wmb() mb()
