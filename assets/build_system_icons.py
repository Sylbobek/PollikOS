"""Reproducible PNG system icons: uniform 64px canvas / 56px visible tile."""
from pathlib import Path
from PIL import Image,ImageDraw

ROOT=Path(__file__).resolve().parents[1]
OUTPUT=ROOT/'assets/system-icons'
STOCK={
    'welcome':('AboutSystemIcon.png','#7369c3'),
    'files':('FilesIcon.png','#62a8ee'),
    'terminal':('TerminalIcon.png','#292b30'),
    'notes':('NotepadIcon.png','#f5e5ad'),
    'settings':('SettingsIcon.png','#dfe2e8'),
    'browser':('BrowserIcon.png','#ebedf2'),
    'folder':('FolderIcon.png','#80b8ee'),
    'folder-blue':('FolderBlueIcon.png','#62a8ee'),
    'file':('TextFileIcon.png','#eceef5'),
    'trash':('TrashIcon.png','#dfe3ea'),
}
def build():
    OUTPUT.mkdir(parents=True,exist_ok=True)
    names=list(STOCK)+['calculator','pollikmark']
    for name in names:
        scale=4;image=Image.new('RGBA',(256,256));d=ImageDraw.Draw(image)
        color=STOCK[name][1] if name in STOCK else '#393641' if name=='calculator' else '#5140b5'
        d.rounded_rectangle((16,16,239,239),radius=52,fill=color)
        if name in ('files','folder','folder-blue'):
            d.rounded_rectangle((40,69,123,118),radius=12,fill='#257bd0')
            d.rounded_rectangle((40,91,215,200),radius=16,fill='#2b82d7')
            d.rounded_rectangle((40,112,215,200),radius=16,fill='#c1e2ff')
        elif name=='terminal':
            d.polygon(((57,82),(112,127),(57,172),(57,147),(81,127),(57,107)),fill='#f0f3fa')
            d.rounded_rectangle((124,152,196,169),radius=4,fill='#f0f3fa')
        elif name in ('notes','file'):
            d.rounded_rectangle((58,37,198,218),radius=12,fill='#fffdf3')
            d.rectangle((58,37,198,65),fill='#d5b661' if name=='notes' else '#8b99b2')
            for row in range(5):d.rounded_rectangle((77,89+row*23,177,96+row*23),radius=3,fill='#b7b1a2' if name=='notes' else '#a5aec0')
        elif name=='settings':
            d.ellipse((69,69,187,187),fill='#566174')
            for box in ((112,42,144,83),(112,173,144,214),(42,112,83,144),(173,112,214,144)):
                d.rounded_rectangle(box,radius=7,fill='#566174')
            for polygon in (((64,47),(94,72),(72,94),(47,64)),((162,72),(192,47),(209,64),(184,94)),
                            ((47,192),(72,162),(94,184),(64,209)),((162,184),(184,162),(209,192),(192,209))):
                d.polygon(polygon,fill='#566174')
            d.ellipse((101,101,155,155),fill='#dfe2e8')
        elif name=='browser':
            d.ellipse((39,39,217,217),fill='#348dec')
            d.polygon(((74,181),(112,112),(181,74),(143,143)),fill='#f6f8ff')
            d.polygon(((112,112),(181,74),(143,143),(128,128)),fill='#ee5c75')
        elif name=='welcome':
            d.ellipse((52,52,204,204),fill='#f2efff')
            d.ellipse((117,79,139,101),fill='#7369c3')
            d.rounded_rectangle((117,114,139,177),radius=6,fill='#7369c3')
        elif name=='trash':
            d.rounded_rectangle((75,85,181,212),radius=12,fill='#637088')
            d.rounded_rectangle((61,61,195,78),radius=5,fill='#637088')
            d.rounded_rectangle((102,43,154,61),radius=5,fill='#637088')
            for x in (100,145):d.rounded_rectangle((x,107,x+11,189),radius=4,fill='#dfe3ea')
        elif name=='calculator':
            d.rounded_rectangle((40,40,215,87),radius=12,fill='#e4dfec')
            for row in range(3):
                for col in range(4):
                    x=40+col*46;y=112+row*34
                    d.rounded_rectangle((x,y,x+30,y+23),radius=5,fill='#ee902c' if col==3 else '#aca3b8')
        else:
            for col,color in enumerate(('#b9aaf7','#83dbd0','#f4f0ff')):
                x=48+col*55;height=48+col*35
                d.rounded_rectangle((x,196-height,x+35,196),radius=9,fill=color)
            d.rounded_rectangle((48,208,199,215),radius=3,fill='#afa0eb')
        image=image.resize((64,64),Image.Resampling.LANCZOS)
        # Explicit zero-alpha rim makes optical dimensions deterministic.
        alpha=image.getchannel('A');a=alpha.load()
        for y in range(64):
            for x in range(64):
                if x<4 or y<4 or x>=60 or y>=60:a[x,y]=0
        image.putalpha(alpha)
        path=OUTPUT/(name+'.png');image.save(path,optimize=False,compress_level=9)
        assert image.getchannel('A').getbbox()==(4,4,60,60),(name,image.getbbox())
        print(f'ICON {name}: canvas=64x64 bbox=56x56 bytes={path.stat().st_size}')
    return names
if __name__=='__main__':build()
