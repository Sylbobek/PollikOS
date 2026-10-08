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
    unsigned s=256-t;return (((a>>16&255)*s+(b>>16&255)*t)>>8)<<16|
        (((a>>8&255)*s+(b>>8&255)*t)>>8)<<8|(((a&255)*s+(b&255)*t)>>8);
}
#endif
