#include "system.h"
static volatile u8 *address;
static int stride, bytes, screen_w, screen_h;
static u32 presents, transferred;
/* QEMU fw_cfg carries the launcher's monitor choice. Missing configuration
 * retains the BIOS mode so older launchers and non-QEMU machines still boot. */
static void requested_mode(int *w, int *h) {
    outw(0x510, 0);
    char sig[4]; for (int i=0;i<4;i++) sig[i]=inb(0x511);
    if (memcmp(sig,"QEMU",4)) return;
    outw(0x510,0x19);
    u32 count=0; for(int i=0;i<4;i++)count=(count<<8)|inb(0x511);
    if(count>256)return;
    for(u32 i=0;i<count;i++) {
        u8 entry[64];for(int j=0;j<64;j++)entry[j]=inb(0x511);
        if(memcmp(entry+8,"opt/pollikos/display",20))continue;
        outw(0x510,((u16)entry[4]<<8)|entry[5]);
        char s[24];for(int j=0;j<23;j++)s[j]=inb(0x511);s[23]=0;
        int x=0,y=0,j=0;
        while(j<5 && s[j]>='0'&&s[j]<='9')x=x*10+s[j++]-'0';
        if(s[j++]!='x')return;
        while(j<12 && s[j]>='0'&&s[j]<='9')y=y*10+s[j++]-'0';
        if(s[j] || x<1024 || x>3440 || y<720 || y>1440 || x%8)return;
        *w=x;*h=y;return;
    }
}
static void dispi(u16 index,u16 value){outw(0x1ce,index);outw(0x1cf,value);}
int framebuffer_width(void){return screen_w;}
int framebuffer_height(void){return screen_h;}
int framebuffer_bpp(void){return bytes*8;}
int framebuffer_init(const u8 *v) {
    u16 attributes = *(const u16 *)v;
    screen_w = *(const u16 *)(v + 18);
    screen_h = *(const u16 *)(v + 20);
    stride = *(const u16 *)(v + 16);
    bytes = v[25] / 8;
    address = (volatile u8 *)*(const u32 *)(v + 40);
    if ((attributes & 0x91) != 0x91 || screen_w != 1024 || screen_h != 768 || !address ||
        (v[25] != 24 && v[25] != 32) || stride < screen_w * bytes || v[27] != 6 || v[31] != 8 ||
        v[32] != 16 || v[33] != 8 || v[34] != 8 || v[35] != 8 || v[36] != 0)
        return 0;
    serial("GFX validated VBE direct-color framebuffer; partial updates ready\n");
    int w=screen_w,h=screen_h;
    requested_mode(&w,&h);
    outw(0x1ce,0);u16 id=inw(0x1cf);
    if(id>=0xb0c0 && id<=0xb0c5) {
        dispi(4,0);dispi(1,w);dispi(2,h);dispi(3,32);dispi(6,w);dispi(8,0);dispi(9,0);dispi(4,0x41);
        outw(0x1ce,1);screen_w=inw(0x1cf);
        outw(0x1ce,2);screen_h=inw(0x1cf);
        bytes=4;stride=screen_w*4;
    }
    char dimensions[16];
    serial("GFX resolution: ");
    number(dimensions,screen_w);serial(dimensions);serial("x");
    number(dimensions,screen_h);serial(dimensions);
    serial(" pitch=");number(dimensions,stride);serial(dimensions);
    serial(" bpp=");number(dimensions,bytes*8);serial(dimensions);serial("\n");
    return 1;
}
void framebuffer_present(const u32 *source, int x, int y, int w, int h) {
    if (!source || !address) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > screen_w)
        w = screen_w - x;
    if (y + h > screen_h)
        h = screen_h - y;
    if (w <= 0 || h <= 0)
        return;

    if (bytes == 4) {
        for (int row = 0; row < h; row++) {
            int line = y + row;
            /* Init accepts only 8-bit B/G/R at bits 0/8/16 (also the
             * DISPI 32-bpp layout), matching our packed 0x00RRGGBB scene.
             * Copy each row separately: LFB pitch need not equal width*4.
             * Linear framebuffer memory, not register MMIO; no atomic flip. */
            void *dest = (void *)(address + line * stride + x * 4);
            const u32 *s = source + line * screen_w + x;
            memcpy(dest, s, (u32)w * sizeof(u32));
        }
    } else {
        for (int row = 0; row < h; row++) {
            int line = y + row;
            volatile u8 *dest = address + line * stride + x * bytes;
            const u32 *s = source + line * screen_w + x;
            for (int col = 0; col < w; col++) {
                u32 c = s[col];
                dest[col * 3] = (u8)c;
                dest[col * 3 + 1] = (u8)(c >> 8);
                dest[col * 3 + 2] = (u8)(c >> 16);
            }
        }
    }
    presents++;
    transferred += (u32)w * h;
}
void framebuffer_info(char *out) {
    const char *s = "VBE / partial updates\nPixel depth: ";
    char *p = out;
    while (*s)
        *p++ = *s++;
    number(p, bytes * 8);
    while (*p)
        p++;
    s = " bits\nPresent calls: ";
    while (*s)
        *p++ = *s++;
    number(p, presents);
    while (*p)
        p++;
    s = "\nPixels copied: ";
    while (*s)
        *p++ = *s++;
    number(p, transferred);
    while (*p)
        p++;
    s = "\nSoftware rendering; no GPU acceleration.";
    while ((*p++ = *s++))
        ;
}
