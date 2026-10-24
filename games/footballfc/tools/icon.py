"""Original procedural app icon; no external game artwork."""
from pathlib import Path
from PIL import Image,ImageDraw
import math
root=Path(__file__).resolve().parents[1]
im=Image.new('RGBA',(220,224));d=ImageDraw.Draw(im);d.rounded_rectangle((0,0,219,223),radius=35,fill='#0c2730')
for i in range(5):d.polygon([(i*65-60,224),(i*65+10,224),(i*40+110,0),(i*40+78,0)],fill='#19483e')
d.ellipse((31,34,189,192),fill='#e7ede2',outline='#ffd34b',width=5)
def polygon(cx,cy,r,n,rotation):return [(cx+math.cos(rotation+2*math.pi*i/n)*r,cy+math.sin(rotation+2*math.pi*i/n)*r)for i in range(n)]
center=polygon(110,113,29,5,-math.pi/2);d.polygon(center,fill='#17343b')
for x,y in center:
 dx=x-110;dy=y-113;ox=110+dx*2.6;oy=113+dy*2.6;d.line((x,y,ox,oy),fill='#17343b',width=5);d.polygon(polygon(ox,oy,19,5,-math.pi/2),fill='#17343b')
im.resize((55,56),Image.Resampling.LANCZOS).save(root/'src/icon.png')
if __name__=='__main__':print('Original Football FC icon generated.')
