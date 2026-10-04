#!/usr/bin/env python3
"""Generate PollikOS's original antialiased 32x32 software cursor artwork."""
import argparse
import math
from pathlib import Path

S = 4
N = 32 * S
SPRITES = (
    ("arrow", (0, 0)), ("ibeam", (16, 16)), ("hand", (5, 2)),
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
    def __init__(self):
        self.p = [(0, 0, 0, 0)] * (N * N)

    def put(self, x, y, c):
        ix, iy = int(x), int(y)
        if 0 <= ix < N and 0 <= iy < N:
            self.p[iy * N + ix] = c

    def poly(self, points, color):
        pts = [(x * S, y * S) for x, y in points]
        for y in range(max(0, math.floor(min(p[1] for p in pts))), min(N, math.ceil(max(p[1] for p in pts)))):
            for x in range(max(0, math.floor(min(p[0] for p in pts))), min(N, math.ceil(max(p[0] for p in pts)))):
                if point_in_poly(x + .5, y + .5, pts): self.put(x, y, color)

    def line(self, a, b, width, color):
        ax, ay = a[0] * S, a[1] * S
        bx, by = b[0] * S, b[1] * S
        radius = width * S / 2
        dx, dy = bx - ax, by - ay
        den = dx * dx + dy * dy
        x0, x1 = max(0, int(min(ax, bx) - radius - 1)), min(N, int(max(ax, bx) + radius + 2))
        y0, y1 = max(0, int(min(ay, by) - radius - 1)), min(N, int(max(ay, by) + radius + 2))
        for y in range(y0, y1):
            for x in range(x0, x1):
                t = 0 if not den else max(0, min(1, ((x + .5 - ax) * dx + (y + .5 - ay) * dy) / den))
                if (x + .5 - ax - t * dx) ** 2 + (y + .5 - ay - t * dy) ** 2 <= radius ** 2:
                    self.put(x, y, color)

    def circle(self, cx, cy, radius, color, fill=True, width=1):
        cx, cy, radius, width = cx * S, cy * S, radius * S, width * S
        x0, x1 = max(0, int(cx-radius-1)), min(N, int(cx+radius+2))
        y0, y1 = max(0, int(cy-radius-1)), min(N, int(cy+radius+2))
        for y in range(y0, y1):
            for x in range(x0, x1):
                d = ((x+.5-cx)**2 + (y+.5-cy)**2)**.5
                if d <= radius and (fill or d >= radius-width): self.put(x,y,color)

def shape(canvas, name):
    black=(255,20,20,24); white=(255,255,255,255); blue=(255,75,118,245); gray=(255,75,75,82)
    def outlined(outer, inner): canvas.poly(outer, black); canvas.poly(inner, white)
    if name == "arrow":
        outlined([(2,1),(2,26),(8,20),(13,30),(17,28),(12,18),(21,18)],
                 [(4,5),(4,21),(8,17),(13,27),(15,26),(10,16),(17,16)])
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
        for cx,cy,sign in ((a[0],a[1],-1),(b[0],b[1],1)):
            base=(cx+ux*6*sign,cy+uy*6*sign)
            canvas.poly([(cx-ux*2,cy-uy*2),(base[0]+px*3,base[1]+py*3),(base[0]-px*3,base[1]-py*3)],black)
            tip=(cx+ux*5*sign,cy+uy*5*sign)
            canvas.poly([(tip[0],tip[1]),(base[0]+px*2,base[1]+py*2),(base[0]-px*2,base[1]-py*2)],white)
        canvas.line(a,b,3,black); canvas.line((a[0]+ux*2,a[1]+uy*2),(b[0]-ux*2,b[1]-uy*2),1.3,white)
    elif name == "move":
        canvas.circle(16,16,3,black,True)
        for a,b in [((16,3),(16,12)),((16,20),(16,29)),((3,16),(12,16)),((20,16),(29,16))]: canvas.line(a,b,3,black)
        canvas.circle(16,16,2,white,True)
        for points in [[(16,2),(12,8),(20,8)],[(16,30),(12,24),(20,24)],[(2,16),(8,12),(8,20)],[(30,16),(24,12),(24,20)]]: canvas.poly(points,black); canvas.poly([(points[0][0],points[0][1]),(points[1][0]+(points[0][0]-points[1][0])*.3,points[1][1]+(points[0][1]-points[1][1])*.3),(points[2][0]+(points[0][0]-points[2][0])*.3,points[2][1]+(points[0][1]-points[2][1])*.3)],white)
    else:
        canvas.circle(16,16,10,black,False,3); canvas.circle(16,16,10,white,False,1)
        canvas.line((9,23),(23,9),4,black); canvas.line((10,22),(22,10),2,white)

def render(name):
    c=Canvas(); shape(c,name)
    # One-pixel offset soft shadow sampled from the high-resolution silhouette.
    alpha=[pixel[0] for pixel in c.p]
    shadow=[0]*(N*N)
    for y in range(N):
        for x in range(N):
            if alpha[y*N+x]:
                for oy in range(4,9):
                    for ox in range(4,9):
                        xx,yy=x+ox,y+oy
                        if xx<N and yy<N: shadow[yy*N+xx]=max(shadow[yy*N+xx],max(0,60-(abs(ox-6)+abs(oy-6))*8))
    out=[]
    for y in range(32):
        for x in range(32):
            acc=[0,0,0,0]
            for sy in range(S):
                for sx in range(S):
                    px=(y*S+sy)*N+x*S+sx; a=shadow[px]
                    src=(0,0,0,a) if not c.p[px][0] else c.p[px]
                    for i in range(3): acc[i]+=src[i]*src[3]//255
                    acc[3]+=src[3]
            aa=acc[3]//16
            if aa:
                rgb=[min(255,(acc[i]//16)*255//max(1,aa)) for i in range(3)]
                out.append((aa<<24)|(rgb[0]<<16)|(rgb[1]<<8)|rgb[2])
            else: out.append(0)
    return out

def main():
    parser=argparse.ArgumentParser(); parser.add_argument("--output",type=Path,default=Path("kernel/cursor_sprites.h")); args=parser.parse_args()
    lines=["/* Generated by assets/build_cursor.py; original PollikOS artwork. */",
           "#ifndef POLLIK_CURSOR_SPRITES_H", "#define POLLIK_CURSOR_SPRITES_H",
           "#define CURSOR_SPRITE_WIDTH 32", "#define CURSOR_SPRITE_HEIGHT 32",
           "enum CursorSprite {\n"+",\n".join(f"    CURSOR_SPRITE_{name.upper()} = {i}" for i,(name,_) in enumerate(SPRITES))+"\n};",
           "static const u8 cursor_hotspots[][2] = {"+", ".join("{%d,%d}"%hot for _,hot in SPRITES)+"};",
           "static const u32 cursor_pixels[][CURSOR_SPRITE_WIDTH * CURSOR_SPRITE_HEIGHT] = {"]
    for name,_ in SPRITES:
        pixels=render(name); lines.append("    { /* "+name+" */")
        for row in range(32): lines.append("        "+", ".join(f"0x{p:08x}u" for p in pixels[row*32:(row+1)*32])+",")
        lines.append("    },")
    lines.extend(["};", "#endif", ""])
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_text("\n".join(lines),encoding="ascii",newline="\n")
    print(f"Generated {len(SPRITES)} cursor sprites, {len(SPRITES)*32*32} ARGB pixels: {args.output}")

if __name__ == "__main__": main()
