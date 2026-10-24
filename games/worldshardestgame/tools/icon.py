"""Small calculator icon drawn from the original square/circle/maze motifs."""
from pathlib import Path
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1]
im=Image.new('RGBA',(220,224));d=ImageDraw.Draw(im)
d.rounded_rectangle((0,0,219,223),radius=34,fill='#b4b5fe')
d.rectangle((15,61,204,161),fill='black');d.rectangle((21,67,198,155),fill='#f7f7ff')
for y in range(67,156,22):
 for x in range(21,199,22):
  if ((x-21)//22+(y-67)//22)%2:d.rectangle((x,y,min(x+21,198),min(y+21,155)),fill='#e6e6ff')
d.rectangle((21,67,59,155),fill='#b5feb4');d.rectangle((160,67,198,155),fill='#b5feb4')
d.rectangle((31,95,56,120),fill='black');d.rectangle((35,99,52,116),fill='red')
for x,y in [(90,81),(135,133),(138,83),(87,133)]:d.ellipse((x-10,y-10,x+10,y+10),fill='black');d.ellipse((x-5,y-5,x+5,y+5),fill='blue')
im.resize((55,56),Image.Resampling.LANCZOS).save(root/'src/icon.png')
