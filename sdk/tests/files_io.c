/* Invoke the real Files operations over guest syscalls, with no GUI mocks. */
#define main files_app_main
#include "../apps/files.c"
#undef main
static int failures;
#define CHECK(x) do { if(!(x)) { printf("FILES_IO_FAIL line=%d errno=%d\n",__LINE__,errno); ++failures; } } while(0)
static void write_fixture(const char *path,const char *text) {
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC);CHECK(fd>=0);
    if(fd>=0){CHECK(write(fd,text,strlen(text))==(ssize_t)strlen(text));CHECK(close(fd)==0);}
}
static void verify_fixture(const char *path,const char *text) {
    char buffer[32]={0};int fd=open(path,O_RDONLY);CHECK(fd>=0);
    if(fd>=0){CHECK(read(fd,buffer,sizeof(buffer)-1)==(ssize_t)strlen(text));CHECK(!strcmp(buffer,text));CHECK(close(fd)==0);}
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"gui")) {
        verify_fixture("/home/gui-files/source.txt","GUI copy test");
        verify_fixture("/home/gui-files/renamed.txt","GUI copy test");
        struct stat st;CHECK(stat("/home/gui-files/dest/renamed.txt",&st)<0 && errno==ENOENT);
        if(!failures)puts("FILES_GUI_PASS keyboard folder/copy/trash/restore/rename/cut/paste");
        return failures?1:0;
    }
    CHECK(mkdir("/home/files-io-test",0)==0);CHECK(chdir("/home/files-io-test")==0);
    for(unsigned i=0;i<110;i++){char name[16];snprintf(name,sizeof(name),"f%03u",i);write_fixture(name,"preserve");}
    applications_only=0;load_entries();CHECK(entry_count==110 && entry_capacity>=110);
    CHECK(copy_file("f000","copy.txt")==0);verify_fixture("copy.txt","preserve");
    CHECK(copy_file("f001","copy.txt")<0 && errno==EEXIST);verify_fixture("copy.txt","preserve");
    name_prompt=1;strcpy(entered_name,"created");entered_length=7;finish_prompt();CHECK(!name_prompt);
    struct stat st;CHECK(stat("created",&st)==0 && S_ISDIR(st.st_mode));
    CHECK(copy_file("created","folder-copy")<0 && errno==ENOTSUP);
    for(selected=0;selected<entry_count;selected++)if(!strcmp(entries[selected].name,"copy.txt"))break;
    CHECK(selected<entry_count);name_prompt=2;strcpy(entered_name,"renamed.txt");entered_length=11;finish_prompt();CHECK(!name_prompt);
    verify_fixture("renamed.txt","preserve");
    char item[USER_PATH_MAX];CHECK(trash_path("/home/files-io-test/renamed.txt",item)==0);
    CHECK(stat("renamed.txt",&st)<0 && errno==ENOENT);verify_fixture(item,"preserve");
    write_fixture("renamed.txt","new original");CHECK(restore_path(item)<0 && errno==EEXIST);
    verify_fixture(item,"preserve");verify_fixture("renamed.txt","new original");
    CHECK(unlink("renamed.txt")==0);CHECK(restore_path(item)==0);verify_fixture("renamed.txt","preserve");
    char metadata[USER_PATH_MAX];TrashInfo info;CHECK(trash_information(item,&info,metadata)<0);
    CHECK(trash_path("/home/files-io-test/created",item)==0);CHECK(restore_path(item)==0);
    CHECK(stat("created",&st)==0 && S_ISDIR(st.st_mode));
    strcpy(clipboard,"/home/files-io-test/renamed.txt");clipboard_move=1;
    CHECK(chdir("created")==0);load_entries();paste_clipboard();CHECK(!clipboard[0]);verify_fixture("renamed.txt","preserve");
    CHECK(unlink("renamed.txt")==0);CHECK(chdir("..")==0);CHECK(rmdir("created")==0);
    for(unsigned i=0;i<110;i++){char name[16];snprintf(name,sizeof(name),"f%03u",i);CHECK(unlink(name)==0);}
    CHECK(chdir("/")==0);CHECK(rmdir("/home/files-io-test")==0);
    free(entries);entries=0;entry_capacity=0;
    if(!failures)puts("FILES_IO_PASS complete listing, atomic copy, rename, folders, trash/conflict/restore and move");
    return failures?1:0;
}
