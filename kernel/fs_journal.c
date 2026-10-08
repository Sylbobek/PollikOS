#include "fs_journal.h"
#include <stddef.h>
#define JOURNAL_MAGIC 0x314a4b50u
#define VOLUME_BLOCKS 32768u
typedef struct {
    uint32_t magic,version,state,count,crc;
    uint32_t blocks[FS_JOURNAL_RECORDS],checksums[FS_JOURNAL_RECORDS];
    uint8_t reserved[244];
} JournalHeader;
_Static_assert(sizeof(JournalHeader)==512,"journal sector layout");
static const BlockDevice *device;
static JournalHeader header;
static uint8_t (*data)[1024];
extern void *fs_journal_alloc(size_t bytes);
static int cache_ready(void) {
    if(!data) data=fs_journal_alloc(FS_JOURNAL_RECORDS*1024u);
    return data!=0;
}
static uint32_t crc_table[256];
static unsigned readonly,active,overlay,initialized,abort_required,committed;
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
static uint32_t crc32(const void *memory,size_t length) {
    if(!crc_table[1]) for(unsigned i=0;i<256;i++) {
        uint32_t c=i;for(unsigned j=0;j<8;j++) c=(c>>1)^((c&1)?0xedb88320u:0);crc_table[i]=c;
    }
    const uint8_t *p=memory;uint32_t c=~0u;
    for(size_t i=0;i<length;i++) c=crc_table[(c^p[i])&255]^(c>>8);
    return ~c;
}
static uint32_t journal_lba(void) { return device->fs_start-64; }
static int write_header(unsigned state) {
    header.magic=JOURNAL_MAGIC;header.version=1;header.state=state;
    if(!state) header.count=0;
    header.crc=0;header.crc=crc32(&header,sizeof(header));
    return block_write(device,journal_lba(),&header) && block_flush(device);
}
static int apply(void) {
    for(unsigned i=0;i<header.count;i++) {
        uint32_t lba=device->fs_start+header.blocks[i]*2;
        if(!block_write(device,lba,data[i]) || !block_write(device,lba+1,data[i]+512)) return 0;
    }
    return block_flush(device);
}
int fs_journal_attach(const BlockDevice *d) {
    device=d;active=overlay=readonly=initialized=committed=abort_required=0;memset(&header,0,sizeof(header));
    if(!d || d->fs_start<64 || d->sector_limit<(uint64_t)d->fs_start+VOLUME_BLOCKS*2) { readonly=1;return 0; }
    if(!cache_ready()) { readonly=1;return 0; }
    uint8_t sector[512];
    if(!block_read(d,journal_lba(),&header)) { readonly=1;return 0; }
    if(!header.magic) {
        /* Only claim a truly unused prefix. A partition table or unknown
         * legacy contents are never overwritten to make room for the log. */
        for(unsigned i=0;i<64;i++) {
            if(!block_read(d,journal_lba()+i,sector)) { readonly=1;return 0; }
            for(unsigned j=0;j<512;j++) if(sector[j]) { readonly=1;return 0; }
        }
        memset(&header,0,sizeof(header));return 1;
    }
    uint32_t saved=header.crc;header.crc=0;
    if(header.magic!=JOURNAL_MAGIC || header.version!=1 || header.state>1 ||
       header.count>FS_JOURNAL_RECORDS || crc32(&header,sizeof(header))!=saved || (!header.state && header.count)) { readonly=1;return 0; }
    header.crc=saved;initialized=1;
    if(!header.state) return 1;
    if(!cache_ready()) { readonly=1;return 0; }
    for(unsigned i=0;i<header.count;i++) {
        if(header.blocks[i]>=VOLUME_BLOCKS) { readonly=1;return 0; }
        for(unsigned j=0;j<i;j++) if(header.blocks[i]==header.blocks[j]) { readonly=1;return 0; }
        uint32_t lba=journal_lba()+1+i*2;
        if(!block_read(d,lba,data[i]) || !block_read(d,lba+1,data[i]+512) ||
            crc32(data[i],1024)!=header.checksums[i]) { readonly=1;return 0; }
    }
    overlay=1;
    if(!apply() || !write_header(0)) { readonly=1;return 0; }
    overlay=0;return 1;
}
int fs_journal_begin(void) {
    if(readonly || active || !device) return 0;
    if(!cache_ready()) return 0;
    if(!initialized) {
        memset(&header,0,sizeof(header));
        if(!write_header(0)) { readonly=1;return 0; }
        initialized=1;
    }
    header.count=0;active=1;committed=abort_required=0;return 1;
}
int fs_journal_stage(uint32_t block,const void *buffer) {
    if(!active || block>=VOLUME_BLOCKS) return 0;
    unsigned slot=0;while(slot<header.count && header.blocks[slot]!=block) ++slot;
    if(slot==header.count) {
        if(slot==FS_JOURNAL_RECORDS) { abort_required=1;return 0; }
        header.blocks[slot]=block;++header.count;
    }
    memcpy(data[slot],buffer,1024);return 1;
}
int fs_journal_overlay(uint32_t block,void *buffer) {
    if(!active && !overlay) return 0;
    for(unsigned i=0;i<header.count;i++) if(header.blocks[i]==block) { memcpy(buffer,data[i],1024);return 1; }
    return 0;
}
int fs_journal_end(int commit) {
    if(!active) return 0;
    if(abort_required) { active=0;header.count=0;return 0; }
    if(!commit || !header.count) { active=0;header.count=0;return 1; }
    for(unsigned i=0;i<header.count;i++) {
        header.checksums[i]=crc32(data[i],1024);
        uint32_t lba=journal_lba()+1+i*2;
        if(!block_write(device,lba,data[i]) || !block_write(device,lba+1,data[i]+512)) { active=0;header.count=0;return 0; }
    }
    if(!block_flush(device) || !write_header(1)) { active=0;readonly=1;header.count=0;return 0; }
    active=0;overlay=committed=1;
    if(!apply()) { readonly=1;return 0; }
    if(!write_header(0)) { readonly=1;overlay=0;return 0; }
    overlay=0;return 1;
}
int fs_journal_active(void) { return active; }
int fs_journal_committed(void) { return committed; }
int fs_journal_readonly(void) { return readonly; }
void fs_journal_force_readonly(void) { readonly=1; }
void fs_journal_detach(void) {
    device=0;active=overlay=readonly=initialized=committed=abort_required=0;
    memset(&header,0,sizeof(header));
}
int fs_journal_format_done(const BlockDevice *d) {
    fs_journal_detach();device=d;
    JournalHeader previous;
    if(!d || d->fs_start<64 || !block_read(d,journal_lba(),&previous)) { readonly=1;return 0; }
    if(previous.magic!=JOURNAL_MAGIC) {
        uint8_t sector[512];
        for(unsigned i=0;i<64;i++) {
            if(!block_read(d,journal_lba()+i,sector)) { readonly=1;return 0; }
            for(unsigned j=0;j<512;j++) if(sector[j]) { readonly=1;return 0; }
        }
    }
    if(!write_header(0)) { readonly=1;return 0; }
    initialized=1;return 1;
}
