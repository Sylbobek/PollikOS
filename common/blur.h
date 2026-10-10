#ifndef POLLIK_BLUR_H
#define POLLIK_BLUR_H
/* In-place RGB888 box blur; caller owns equally sized scratch storage. */
static inline void pollik_box_blur(unsigned *pixels,unsigned *scratch,int width,int height,int radius){
    int taps=radius*2+1;
    for(int pass=0;pass<2;pass++){
        for(int y=0;y<height;y++)for(int x=0;x<width;x++){
            unsigned r=0,g=0,b=0;
            for(int k=-radius;k<=radius;k++){
                int sx=x+(pass==0?k:0),sy=y+(pass==1?k:0);
                if(sx<0)sx=0;if(sx>=width)sx=width-1;if(sy<0)sy=0;if(sy>=height)sy=height-1;
                unsigned color=pixels[sy*width+sx];r+=(color>>16)&255;g+=(color>>8)&255;b+=color&255;
            }
            scratch[y*width+x]=(r/taps<<16)|(g/taps<<8)|(b/taps);
        }
        unsigned *swap=pixels;pixels=scratch;scratch=swap;
    }
}
static inline unsigned pollik_color_mix(unsigned a,unsigned b,unsigned t){
    unsigned s=256-t;
    unsigned rb=(((a&0xff00ff)*s+(b&0xff00ff)*t)>>8)&0xff00ff;
    unsigned g=(((a>>8)&255)*s+((b>>8)&255)*t)&0xff00;
    return rb|g;
}
/* Grow a half-resolution RGB cache in place, backwards to preserve samples. */
static inline void pollik_expand_blur2(unsigned *pixels,unsigned width,unsigned height){
    unsigned sw=(width+1)/2,sh=(height+1)/2;
    for(int y=(int)height-1;y>=0;y--)for(int x=(int)width-1;x>=0;x--){
        unsigned sx=(unsigned)x>>1,sy=(unsigned)y>>1,nx=sx+1<sw?sx+1:sx,ny=sy+1<sh?sy+1:sy;
        unsigned a=pixels[sy*sw+sx];
        if(x&1)a=pollik_color_mix(a,pixels[sy*sw+nx],128);
        if(y&1){unsigned b=pixels[ny*sw+sx];if(x&1)b=pollik_color_mix(b,pixels[ny*sw+nx],128);a=pollik_color_mix(a,b,128);}
        pixels[(unsigned)y*width+(unsigned)x]=pollik_color_mix(a,0x100b20,112);
    }
}
#endif
