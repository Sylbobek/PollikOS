/* All allocations are coherent x86 RAM, not streaming/noncoherent mappings. */
#define dcache_clean_by_mva(p,n) ((void)(p),(void)(n))
#define dcache_invalidate_by_mva(p,n) ((void)(p),(void)(n))
#define dcache_clean_invalidate_by_mva(p,n) ((void)(p),(void)(n))
