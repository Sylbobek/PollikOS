#include "factory_reset.h"
#include "pollikfs.h"
#include "account.h"
#include <stddef.h>
#include <string.h>
#define RESET_MARKER "/etc/factory-reset.pending"
#define RESET_TEMP "/etc/factory-reset.intent"
static const char reset_intent[]="PollikOS factory reset v1\n";
int factory_reset_pending(void) {
    vfs_stat_t st;
    if(pollikfs_stat(RESET_MARKER,&st)<0)return pollikfs_error()==VFS_NOT_FOUND?0:-1;
    if(st.type!=VFS_FILE || st.size!=sizeof(reset_intent)-1)return -1;
    vfs_file_t f;char data[sizeof(reset_intent)];
    if(pollikfs_open(RESET_MARKER,O_RDONLY,&f)<0)return -1;
    int n=pollikfs_read(&f,data,sizeof(data));int closed=pollikfs_close(&f);
    if(n!=(int)sizeof(reset_intent)-1 || closed<0)return -1;
    for(unsigned i=0;i<sizeof(reset_intent)-1;i++)if(data[i]!=reset_intent[i])return -1;
    return 1;
}
int factory_reset_mark(void) {
    if(!pollikfs_mounted() || pollikfs_readonly())return 0;
    int pending=factory_reset_pending();if(pending)return pending==1;
    vfs_file_t f;if(pollikfs_open(RESET_TEMP,O_WRONLY|O_CREAT|O_TRUNC,&f)<0)return 0;
    int n=pollikfs_write(&f,reset_intent,sizeof(reset_intent)-1);int closed=pollikfs_close(&f);
    if(n!=(int)sizeof(reset_intent)-1 || closed<0){(void)pollikfs_unlink(RESET_TEMP);return 0;}
    if(pollikfs_rename_replace(RESET_TEMP,RESET_MARKER)<0)return 0;
    return factory_reset_pending()==1;
}
static int erase_tree(const char *path,unsigned depth,int keep_root) {
    vfs_stat_t st;
    if(pollikfs_stat(path,&st)<0)return pollikfs_error()==VFS_NOT_FOUND;
    if(st.type!=VFS_DIR)return !keep_root && pollikfs_unlink(path)==0;
    if(depth>=32)return 0;
    for(;;) {
        vfs_file_t dir;vfs_dirent_t entry;int found=0,n;
        if(pollikfs_open(path,O_RDONLY,&dir)<0)return 0;
        while((n=pollikfs_readdir(&dir,&entry))>0) {
            if(entry.name[0]=='.' && (!entry.name[1] || (entry.name[1]=='.' && !entry.name[2])))continue;
            found=1;break;
        }
        if(pollikfs_close(&dir)<0 || n<0)return 0;
        if(!found)break;
        char child[VFS_MAX_PATH];size_t a=strlen(path),b=strlen(entry.name);
        if(a+1+b>=sizeof(child))return 0;
        memcpy(child,path,a);child[a]='/';memcpy(child+a+1,entry.name,b+1);
        if(!erase_tree(child,depth+1,0))return 0;
    }
    return keep_root || pollikfs_rmdir(path)==0;
}
int factory_reset_finish(void) {
    if(!pollikfs_mounted() || pollikfs_readonly() || factory_reset_pending()!=1)return 0;
    /* Personal data/preferences live here. Do not format or touch /bin, /usr,
     * another disk, or the unused tail of a larger PollikFS data image. */
    if(!erase_tree("/home",0,1) || !erase_tree("/tmp",0,1))return 0;
    const char *accounts[]={"/etc/account.db.pending",ACCOUNT_PATH,INSTALL_MARKER};
    for(unsigned i=0;i<sizeof(accounts)/sizeof(accounts[0]);i++) {
        if(pollikfs_unlink(accounts[i])<0 && pollikfs_error()!=VFS_NOT_FOUND)return 0;
    }
    return pollikfs_unlink(RESET_MARKER)==0;
}
