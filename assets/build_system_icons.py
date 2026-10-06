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
        if name in STOCK:
            source=Image.open(ROOT/'assets'/STOCK[name][0]).convert('RGBA')
            source=source.crop(source.getchannel('A').getbbox())
            source.thumbnail((184,184),Image.Resampling.LANCZOS)
            image.alpha_composite(source,((256-source.width)//2,(256-source.height)//2))
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
