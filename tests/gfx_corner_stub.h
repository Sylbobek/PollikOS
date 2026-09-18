/* Native-only stand-in for the public graphics LUT accessor. No target state.
 * Independent 4x4 sample oracle matching production's 0..64 coverage scale. */
int graphics_corner_coverage(int r,int x,int y) {
    if(x<0 || y<0)return 0;
    if(r<=0 || x>=r || y>=r)return 64;
    int n=0;
    for(int sy=1;sy<8;sy+=2)for(int sx=1;sx<8;sx+=2) {
        int dx=8*(r-x)-sx,dy=8*(r-y)-sy;
        if(dx*dx+dy*dy<=64*r*r)n+=4;
    }
    return n;
}
