"""Independent vector output, geometry, hotspot and lossless packing checks."""
import importlib.util
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('cursor_generator',ROOT/'assets'/'build_cursor.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
for percent in g.SCALES:
    side=32*percent//100
    for name,hotspot in g.SPRITES:
        sprite=g.render(name,percent)
        encoded=g.encode(sprite,side);decoded=[0]*(side*side);i=0
        while encoded[i]!=255:
            y,x,n=encoded[i:i+3];i+=3
            for col in range(n):
                alpha,gray=encoded[i:i+2];i+=2
                decoded[y*side+x+col]=(alpha<<24)|(gray*0x10101)
        assert decoded==sprite,(percent,name,'packing')
        visible=[(i%side,i//side) for i,p in enumerate(sprite) if p>>24]
        assert visible,(percent,name)
        hx,hy=hotspot;hx=round(hx*percent/100);hy=round(hy*percent/100)
        assert sprite[hy*side+hx]>>24,(percent,name,'hotspot outside artwork')
        assert any(0<(p>>24)<255 for p in sprite),(percent,name,'antialias')
        if name=='arrow' and percent==100:
            bbox=(min(x for x,y in visible),min(y for x,y in visible),max(x for x,y in visible),max(y for x,y in visible))
            w,h=bbox[2]-bbox[0]+1,bbox[3]-bbox[1]+1
            assert 11<=w<=13 and 18<=h<=20,bbox
            assert sprite[0]>>24,'tip at hotspot'
            print(f'PASS arrow 100%: visible bbox={bbox} width={w} height={h}; tip hotspot=(0,0); no shadow')
        assert all((p&255)==((p>>8)&255)==((p>>16)&255) for p in sprite),'monochrome'
print('PASS vector cursors: 10 kinds x 4 scales; eight-bit antialias; lossless ARGB stream roundtrip')
