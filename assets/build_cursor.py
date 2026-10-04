#!/usr/bin/env python3
"""Generate PollikOS's original antialiased 32x32 software cursor artwork."""
import argparse
import math
from pathlib import Path

S = 4
N = 32 * S
SPRITES = (
    ("arrow", (0, 0)), ("ibeam", (16, 16)), ("hand", (15, 7)),
    ("busy", (16, 16)), ("resize_ns", (16, 16)), ("resize_ew", (16, 16)),
    ("resize_nwse", (16, 16)), ("resize_nesw", (16, 16)),
    ("move", (16, 16)), ("not_allowed", (16, 16)),
)

def point_in_poly(x, y, points):
    inside = False
    j = len(points) - 1
    for i, (xi, yi) in enumerate(points):
        xj, yj = points[j]
        if ((yi > y) != (yj > y)) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside

class Canvas:
    def __init__(self, percent=100):
        self.scale = percent / 100
        self.side = 32 * percent // 100
        self.n = self.side * S
        self.p = [(0, 0, 0, 0)] * (self.n * self.n)
        self.factor = 1.0

    def point(self, p):
        return tuple((16 + (v - 16) * self.factor) * self.scale for v in p)

    def put(self, x, y, c):
        ix, iy = int(x), int(y)
        if 0 <= ix < self.n and 0 <= iy < self.n:
            self.p[iy * self.n + ix] = c

    def poly(self, points, color):
        pts = [(x * S, y * S) for x, y in map(self.point, points)]
        for y in range(max(0, math.floor(min(p[1] for p in pts))), min(self.n, math.ceil(max(p[1] for p in pts)))):
            for x in range(max(0, math.floor(min(p[0] for p in pts))), min(self.n, math.ceil(max(p[0] for p in pts)))):
                if point_in_poly(x + .5, y + .5, pts): self.put(x, y, color)

    def line(self, a, b, width, color):
        a, b = self.point(a), self.point(b)
        ax, ay = a[0] * S, a[1] * S
        bx, by = b[0] * S, b[1] * S
        radius = width * self.scale * S / 2
        dx, dy = bx - ax, by - ay
        den = dx * dx + dy * dy
        x0, x1 = max(0, int(min(ax, bx) - radius - 1)), min(self.n, int(max(ax, bx) + radius + 2))
        y0, y1 = max(0, int(min(ay, by) - radius - 1)), min(self.n, int(max(ay, by) + radius + 2))
        for y in range(y0, y1):
            for x in range(x0, x1):
                t = 0 if not den else max(0, min(1, ((x + .5 - ax) * dx + (y + .5 - ay) * dy) / den))
                if (x + .5 - ax - t * dx) ** 2 + (y + .5 - ay - t * dy) ** 2 <= radius ** 2:
                    self.put(x, y, color)

    def circle(self, cx, cy, radius, color, fill=True, width=1):
        cx, cy = self.point((cx,cy))
        cx, cy, radius, width = cx * S, cy * S, radius * self.factor * self.scale * S, width * self.scale * S
        x0, x1 = max(0, int(cx-radius-1)), min(self.n, int(cx+radius+2))
        y0, y1 = max(0, int(cy-radius-1)), min(self.n, int(cy+radius+2))
        for y in range(y0, y1):
            for x in range(x0, x1):
                d = ((x+.5-cx)**2 + (y+.5-cy)**2)**.5
                if d <= radius and (fill or d >= radius-width): self.put(x,y,color)

def shape(canvas, name):
    black=(0,0,0,255); white=(255,255,255,255); blue=white; gray=(82,82,82,255)
    canvas.factor = 1.0 if name == "arrow" else .625
    def outlined(outer, inner): canvas.poly(outer, black); canvas.poly(inner, white)
    if name == "arrow":
        points=[(.5,.5),(.5,15.5),(3.5,12.5),(6.5,18.5),(8.5,17.5),(5.5,11.5),(11.5,11.5)]
        canvas.poly(points,white)
        for i,a in enumerate(points): canvas.line(a,points[(i+1)%len(points)],1,black)
    elif name == "ibeam":
        canvas.line((10,7),(22,7),3,black); canvas.line((10,25),(22,25),3,black)
        canvas.line((16,8),(16,24),4,black); canvas.line((16,9),(16,23),2,white)
        canvas.line((11,7),(21,7),1,white); canvas.line((11,25),(21,25),1,white)
    elif name == "hand":
        outlined([(7,17),(7,9),(8,7),(10,7),(11,9),(11,14),(12,4),(13,2),(15,2),(16,4),(16,14),(17,6),(19,5),(21,7),(20,15),(22,12),(24,12),(25,14),(23,24),(19,29),(12,28),(9,24)],
                 [(9,17),(9,9),(10,9),(10,17),(12,17),(13,4),(14,4),(14,17),(16,17),(17,8),(18,7),(18,17),(20,16),(22,14),(23,14),(21,23),(18,27),(13,26),(11,23)])
    elif name == "busy":
        canvas.circle(16,16,10,black,False,3); canvas.circle(16,16,10,blue,False,1.5)
        canvas.line((16,5),(16,11),2,blue); canvas.line((16,16),(21,19),2,black); canvas.line((16,16),(20,18),1,white)
        canvas.circle(16,16,1.5,white)
    elif name in ("resize_ns","resize_ew","resize_nwse","resize_nesw"):
        vectors={"resize_ns":((16,4),(16,28)),"resize_ew":((4,16),(28,16)),
                 "resize_nwse":((5,5),(27,27)),"resize_nesw":((27,5),(5,27))}
        a,b=vectors[name]; dx,dy=b[0]-a[0],b[1]-a[1]
        length=(dx*dx+dy*dy)**.5; ux,uy=dx/length,dy/length; px,py=-uy,ux
        canvas.line(a,b,3,black)
        canvas.line((a[0]+ux*3,a[1]+uy*3),(b[0]-ux*3,b[1]-uy*3),1,white)
        for cx,cy,sign in ((a[0],a[1],-1),(b[0],b[1],1)):
            base=(cx-ux*6*sign,cy-uy*6*sign)
            canvas.poly([(cx,cy),(base[0]+px*4,base[1]+py*4),(base[0]-px*4,base[1]-py*4)],black)
            tip=(cx-ux*2*sign,cy-uy*2*sign)
            inner=(base[0]+ux*sign,base[1]+uy*sign)
            canvas.poly([tip,(inner[0]+px*2,inner[1]+py*2),(inner[0]-px*2,inner[1]-py*2)],white)
    elif name == "move":
        canvas.circle(16,16,3,black,True)
        for a,b in [((16,3),(16,12)),((16,20),(16,29)),((3,16),(12,16)),((20,16),(29,16))]: canvas.line(a,b,3,black)
        canvas.circle(16,16,2,white,True)
        for points in [[(16,2),(12,8),(20,8)],[(16,30),(12,24),(20,24)],[(2,16),(8,12),(8,20)],[(30,16),(24,12),(24,20)]]: canvas.poly(points,black); canvas.poly([(points[0][0],points[0][1]),(points[1][0]+(points[0][0]-points[1][0])*.3,points[1][1]+(points[0][1]-points[1][1])*.3),(points[2][0]+(points[0][0]-points[2][0])*.3,points[2][1]+(points[0][1]-points[2][1])*.3)],white)
    else:
        canvas.circle(16,16,10,black,False,3); canvas.circle(16,16,10,white,False,1)
        canvas.line((9,23),(23,9),4,black); canvas.line((10,22),(22,10),2,white)

SCALES = (100,125,150,200)

def render(name, percent=100):
    c=Canvas(percent); shape(c,name)
    out=[]
    for y in range(c.side):
        for x in range(c.side):
            acc=[0,0,0,0]
            for sy in range(S):
                for sx in range(S):
                    src=c.p[(y*S+sy)*c.n+x*S+sx]
                    for i in range(3): acc[i]+=src[i]*src[3]
                    acc[3]+=src[3]
            alpha=(acc[3]+S*S//2)//(S*S)
            if alpha:
                rgb=[(acc[i]+acc[3]//2)//acc[3] for i in range(3)]
                out.append((alpha<<24)|(rgb[0]<<16)|(rgb[1]<<8)|rgb[2])
            else: out.append(0)
    return out

def encode(pixels, side):
    data=[]
    for y in range(side):
        row=pixels[y*side:(y+1)*side]
        visible=[x for x,p in enumerate(row) if p>>24]
        if not visible: continue
        left,right=min(visible),max(visible)+1
        data.extend((y,left,right-left))
        for p in row[left:right]:
            assert (p&255)==((p>>8)&255)==((p>>16)&255)
            data.extend((p>>24,p&255))
    return data+[255]

def main():
    parser=argparse.ArgumentParser(); parser.add_argument("--output",type=Path,default=Path("kernel/cursor_sprites.h")); args=parser.parse_args()
    stream=[]; offsets=[]; hotspots=[]
    for percent in SCALES:
        group=[]; hot=[]
        for name,point in SPRITES:
            group.append(len(stream))
            stream.extend(encode(render(name,percent),32*percent//100))
            hot.append(tuple(round(v*percent/100) for v in point))
        offsets.append(group); hotspots.append(hot)
    lines=["/* Generated by assets/build_cursor.py; original vector artwork, 4x supersampling. */",
           "#ifndef POLLIK_CURSOR_SPRITES_H", "#define POLLIK_CURSOR_SPRITES_H",
           "#define CURSOR_SPRITE_WIDTH 32", "#define CURSOR_SPRITE_HEIGHT 32",
           "#define CURSOR_MAX_SIDE 64",
           "enum CursorSprite {\n"+",\n".join(f"    CURSOR_SPRITE_{name.upper()} = {i}" for i,(name,_) in enumerate(SPRITES))+"\n};",
           "static const u8 cursor_sides[4] = {32,40,48,64};",
           "static const u16 cursor_percents[4] = {100,125,150,200};",
           "static const u8 cursor_scaled_hotspots[4][10][2] = {"]
    for group in hotspots: lines.append("    {"+", ".join("{%d,%d}"%hot for hot in group)+"},")
    lines+= ["};", "static const u32 cursor_stream_offsets[4][10] = {"]
    for group in offsets: lines.append("    {"+", ".join(map(str,group))+"},")
    lines += ["};", "/* Row runs: y,x,count,(alpha,intensity)*count; y=255 ends sprite. */",
              "static const u8 cursor_stream[] = {"]
    for start in range(0,len(stream),32): lines.append("    "+", ".join(map(str,stream[start:start+32]))+",")
    lines += ["};", "/* Full ARGB reference for the existing 100% asset test; never linked. */",
              "#ifdef POLLIK_CURSOR_REFERENCE_PIXELS",
              "static const u32 cursor_pixels[10][32 * 32] = {"]
    for name,_ in SPRITES:
        pixels=render(name); lines.append("    { /* "+name+" */")
        for row in range(32): lines.append("        "+", ".join(f"0x{p:08x}u" for p in pixels[row*32:(row+1)*32])+",")
        lines.append("    },")
    lines.extend(["};", "#endif", "#endif", ""])
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_text("\n".join(lines),encoding="ascii",newline="\n")
    print(f"Generated {len(SPRITES)} cursors at {SCALES}; packed={len(stream)} bytes; shadow=off: {args.output}")

if __name__ == "__main__": main()
