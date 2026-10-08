/* Exercise the real Notes load/save functions with guest VFS, including ENOSPC. */
#define main notes_app_main
#include "../apps/notes.c"
#undef main
static int failures;
#define CHECK(x) do { if(!(x)) { printf("NOTES_IO_FAIL line=%d errno=%d\n",__LINE__,errno); ++failures; } } while(0)
static void verify(const char *content) {
    char buffer[32]={0};int fd=open(note_path,O_RDONLY);CHECK(fd>=0);
    if(fd>=0) { CHECK(read(fd,buffer,sizeof(buffer)-1)==(ssize_t)strlen(content));CHECK(!strcmp(buffer,content));CHECK(close(fd)==0); }
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"gui")) {
        strcpy(note_path,"/home/notes-gui-original.txt");verify("aą");
        strcpy(note_path,"/home/notes-gui-saved.txt");verify("aą");
        if(!failures)puts("NOTES_GUI_PASS PS2 Unicode, atomic Save As and undo/redo");
        return failures?1:0;
    }
    strcpy(note_path,"/home/notes-io-test.txt");
    int fd=open(note_path,O_WRONLY|O_CREAT|O_TRUNC);CHECK(fd>=0);
    if(fd<0) return 1;
    CHECK(write(fd,"original",8)==8);CHECK(close(fd)==0);
    CHECK(load_note() && editable && !dirty && note_length==8);
    strcpy(note,"updated");note_length=7;dirty=1;
    CHECK(save_note() && !dirty);verify("updated");
    cursor=note_length;insert_character(0x105);CHECK(note_length==9 && cursor==9);
    undo_edit(0);CHECK(!strcmp(note,"updated") && dirty);
    undo_edit(1);CHECK(!strcmp(note,"updatedą") && cursor==9);
    CHECK(pollik_utf8_previous(note,cursor)==7);CHECK(save_note());verify("updatedą");
    CHECK(load_note() && !strcmp(note,"updatedą"));
    CHECK(save_as_path("/home/notes-save-as-test.txt"));verify("updatedą");
    CHECK(!save_as_path("/home/notes-io-test.txt"));verify("updatedą");
    CHECK(unlink(note_path)==0);strcpy(note_path,"/home/notes-io-test.txt");
    canvas.width=120;make_rows();
    for(unsigned i=0;i<row_count;i++) CHECK(row_end[i]==note_length || ((unsigned char)note[row_end[i]]&0xc0)!=0x80);
    fd=open(note_path,O_WRONLY|O_TRUNC);CHECK(fd>=0);
    char block[4096];memset(block,'L',sizeof(block));
    if(fd>=0) { CHECK(write(fd,block,sizeof(block))==sizeof(block));CHECK(write(fd,block,sizeof(block))==sizeof(block));CHECK(close(fd)==0); }
    CHECK(!load_note() && !editable && !dirty);CHECK(!save_note());
    struct stat information;CHECK(stat(note_path,&information)==0 && information.st_size==8192);
    strcpy(note_path,"/etc/account.db");CHECK(!load_note() && !editable);CHECK(!save_note());
    strcpy(note_path,"/home/notes-io-test.txt");
    fd=open(note_path,O_WRONLY|O_TRUNC);CHECK(fd>=0);
    if(fd>=0) { CHECK(write(fd,"preserve",8)==8);CHECK(close(fd)==0); }
    CHECK(load_note());strcpy(note,"new document");note_length=12;dirty=1;
    int filler=open("/home/notes-io-fill",O_WRONLY|O_CREAT|O_TRUNC);CHECK(filler>=0);
    if(filler>=0) {
        unsigned blocks=0;ssize_t written;
        do { written=write(filler,block,sizeof(block));++blocks; } while(written>0 && blocks<9000);
        CHECK(written<0 && errno==ENOSPC);CHECK(close(filler)==0);
        CHECK(!save_note() && dirty);verify("preserve");
        CHECK(unlink("/home/notes-io-fill")==0);
        CHECK(save_note() && !dirty);verify("new document");
    }
    CHECK(unlink(note_path)==0);
    if(!failures) puts("NOTES_IO_PASS atomic save, oversized/unreadable refusal, ENOSPC preservation and retry");
    return failures?1:0;
}
