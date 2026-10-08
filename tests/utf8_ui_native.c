#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../sdk/apps/window_ui.h"
int main(void) {
    size_t used;
    const char *polish="Zażółć gęślą jaźń";
    unsigned count=0;
    for(size_t at=0,length=strlen(polish);at<length;count++) {
        uint32_t codepoint=pollik_utf8_next(polish+at,length-at,&used);
        assert(used && (codepoint<128 || pollik_ui_glyph(codepoint)!=pollik_ui_glyph('?')));
        char encoded[4];size_t bytes=pollik_utf8_encode(codepoint,encoded);
        assert(bytes==used && !memcmp(encoded,polish+at,used));at+=used;
    }
    assert(count==17);
    const unsigned char invalid[][4]={{0xc0,0xaf,0,0},{0xed,0xa0,0x80,0},
        {0xf4,0x90,0x80,0x80},{0xe2,0x82,0,0},{0xf0,0x80,0x80,0x80}};
    const size_t lengths[]={2,3,4,2,4};
    for(unsigned i=0;i<5;i++) { assert(pollik_utf8_next((const char *)invalid[i],lengths[i],&used)=='?');assert(used==1); }
    assert(pollik_utf8_previous("aą",3)==1 && pollik_utf8_previous("aą",1)==0);
    char encoded[4];assert(!pollik_utf8_encode(0xd800,encoded));assert(!pollik_utf8_encode(0x110000,encoded));
    uint32_t storage[162*32+2]={0},ascii[162*32]={0};storage[0]=0x12345678;storage[162*32+1]=0xabcdef01;
    PollikCanvas canvas={storage+1,162,32},baseline={ascii,162,32};
    pollik_ui_text(&canvas,0,0,"ĄąĆćĘęŁłŃńÓóŚśŹźŻż",1);
    pollik_ui_text(&baseline,0,0,"AaCcEeLlNnOoSsZzZz",1);
    assert(memcmp(storage+1,ascii,sizeof(ascii))!=0);
    pollik_ui_text(&canvas,-12,-9,polish,1);
    assert(storage[0]==0x12345678 && storage[162*32+1]==0xabcdef01);
    assert(pollik_ui_text_width("ą")==pollik_ui_glyph(0x105)->advance+2);
    puts("PASS UTF-8 bounds, malformed sequences, Polish glyphs, width and clipped raster guards");
}
