"""Encode native host-rendered frames as an inspection GIF."""
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'docs';out.mkdir(exist_ok=True)
data=(ROOT/'output/motion.raw').read_bytes();size=320*240*2
frames=[Image.frombytes('RGB',(320,240),data[i:i+size],'raw','BGR;16')for i in range(0,len(data),size)]
sample=Image.new('RGB',(320*4,240*3))
for i in range(12):sample.paste(frames[i*len(frames)//12],(i%4*320,i//4*240))
palette=sample.quantize(colors=256,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
frames=[im.quantize(palette=palette,dither=Image.Dither.NONE)for im in frames]
frames[0].save(out/'gameplay.gif',save_all=True,append_images=frames[1:],duration=[30,30,40]*(len(frames)//3),loop=0,optimize=True)
sheet=Image.new('RGB',(320*4,240*2))
for i in range(8):sheet.paste(frames[i*len(frames)//8],(i%4*320,i//4*240))
sheet.save(ROOT/'output/motion-sheet.png');print(out/'gameplay.gif')

pen=ROOT/'output/penalty-motion.raw'
if pen.exists():
 data=pen.read_bytes()
 frames=[Image.frombytes('RGB',(320,240),data[i:i+size],'raw','BGR;16')for i in range(0,len(data),size)]
 sheet=Image.new('RGB',(320*3,240*2))
 for i,frame in enumerate([0,35,48,60,125,140]):sheet.paste(frames[frame],(i%3*320,i//3*240))
 sheet.save(ROOT/'output/penalty-sheet.png')
 palette=sheet.quantize(colors=256,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
 frames=[im.quantize(palette=palette,dither=Image.Dither.NONE)for im in frames]
 frames[0].save(out/'penalties-v9.gif',save_all=True,append_images=frames[1:],duration=[30,30,40]*(len(frames)//3),loop=0,optimize=True)
 print(out/'penalties-v9.gif')

free_motion=ROOT/'output/free-motion.raw'
if free_motion.exists():
 data=free_motion.read_bytes()
 frames=[Image.frombytes('RGB',(320,240),data[i:i+size],'raw','BGR;16')for i in range(0,len(data),size)]
 frames[0].save(out/'free-kicks-v9.gif',save_all=True,append_images=frames[1:],duration=33,loop=0,optimize=True)
 print(out/'free-kicks-v9.gif')
