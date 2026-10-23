"""Encode the host-rendered motion sequence for visual review, not game assets."""
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
data=(ROOT/'output/motion.raw').read_bytes();size=320*240*2
frames=[Image.frombytes('RGB',(320,240),data[i:i+size],'raw','BGR;16')for i in range(0,len(data),size)]
sample=Image.new('RGB',(80*12,60))
for i in range(12):sample.paste(frames[i*len(frames)//12].resize((80,60)),(i*80,0))
palette=sample.quantize(colors=256,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
frames=[f.quantize(palette=palette,dither=Image.Dither.NONE)for f in frames]
out=ROOT/'docs/driving-v4.gif';frames[0].save(out,save_all=True,append_images=frames[1:],duration=[30,30,40]*80,loop=0,optimize=True)
print(out)
