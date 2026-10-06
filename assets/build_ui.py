"""Regenerate filesystem icons and a preview; no kernel bitmap tables."""
from pathlib import Path
from PIL import Image
from build_system_icons import build,OUTPUT
names=build()
preview=Image.new('RGBA',(len(names)*76,80),'#111725')
for i,name in enumerate(names):preview.alpha_composite(Image.open(OUTPUT/(name+'.png')),(i*76+6,8))
preview.save(Path(__file__).resolve().parent/'icons.png')
print('Generated filesystem icons and preview; legacy sprite tables are test fixtures only.')
