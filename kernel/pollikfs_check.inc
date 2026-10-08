/* Read-only consistency pass. It never repairs or formats a user's volume. */
static u8 check_seen[POLLIK2_TOTAL_BLOCKS/8],check_bitmap[POLLIK2_TOTAL_BLOCKS/8];
static u8 check_modes[POLLIK2_INODE_COUNT];
static u32 check_parent[POLLIK2_INODE_COUNT];
static int check_mark(u32 block) {
    if(!data_block(block) || (check_seen[block/8]&(1u<<(block%8)))) return 0;
    check_seen[block/8]|=(u8)(1u<<(block%8));return 1;
}
static int check_indirect(u32 block,int double_level) {
    u8 table[POLLIK2_BLOCK_SIZE];
    if(!check_mark(block) || !read_block(block,table)) return 0;
    for(unsigned i=0;i<POLLIK2_INDIRECT_ENTRIES;i++) {
        u32 entry=le32(table+i*4);if(!entry) continue;
        if(double_level ? !check_indirect(entry,0) : !check_mark(entry)) return 0;
    }
    return 1;
}
static int filesystem_check(void) {
    memset(check_seen,0,sizeof(check_seen));memset(check_modes,0,sizeof(check_modes));
    memset(check_parent,0,sizeof(check_parent));
    for(u32 b=0;b<g_sb.data_blocks_start;b++) check_seen[b/8]|=(u8)(1u<<(b%8));
    for(u32 b=0;b<4;b++) if(!read_block(1+b,check_bitmap+b*1024)) return 0;
    u32 free_inodes=0;
    for(u32 ino=1;ino<POLLIK2_INODE_COUNT;ino++) {
        PollikInode node;if(!read_inode(ino,&node)) return 0;
        check_modes[ino]=(u8)node.mode;
        if(!node.mode) { ++free_inodes;continue; }
        for(unsigned j=0;j<8;j++) if(node.direct[j] && !check_mark(node.direct[j])) return 0;
        if(node.indirect && !check_indirect(node.indirect,0)) return 0;
        if(node.double_indirect && !check_indirect(node.double_indirect,1)) return 0;
    }
    /* Both legacy writers exist: the kernel includes reserved inode zero,
     * while the offline SDK counts only usable slots. Accept that one-slot
     * convention difference without rewriting existing volumes. */
    if(free_inodes!=g_sb.free_inodes && free_inodes+1!=g_sb.free_inodes) return 0;
    u32 free_blocks=0;
    for(u32 b=0;b<POLLIK2_TOTAL_BLOCKS;b++) {
        if(check_seen[b/8]!=(check_bitmap[b/8])) return 0;
        if(!(check_bitmap[b/8]&(1u<<(b%8)))) ++free_blocks;
    }
    if(free_blocks!=g_sb.free_blocks) return 0;
    for(u32 ino=1;ino<POLLIK2_INODE_COUNT;ino++) if(check_modes[ino]==VFS_DIR) {
        PollikInode node;if(!read_inode(ino,&node)) return 0;
        u8 buffer[1024];
        for(unsigned b=0;b<8;b++) if(node.direct[b]) {
            if(!read_block(node.direct[b],buffer)) return 0;
            for(unsigned i=0;i<16;i++) {
                const PollikDirent *e=(const PollikDirent *)(buffer+i*64);
                if(!entry_valid(e)) return 0;
                u32 child=le32(&e->inode);if(!child) continue;
                if(!check_modes[child] || check_modes[child]!=e->file_type || child==1 || check_parent[child]) return 0;
                check_parent[child]=ino;
                for(unsigned j=0;j<i;j++) {
                    const PollikDirent *other=(const PollikDirent *)(buffer+j*64);
                    if(other->inode && other->name_len==e->name_len && !memcmp(other->name,e->name,e->name_len)) return 0;
                }
            }
        }
    }
    for(u32 ino=2;ino<POLLIK2_INODE_COUNT;ino++) if(check_modes[ino]) {
        u32 at=ino;unsigned depth=0;
        while(at!=1 && at && depth<POLLIK2_INODE_COUNT) { at=check_parent[at];++depth; }
        if(at!=1 || depth==POLLIK2_INODE_COUNT) return 0;
    }
    return 1;
}
