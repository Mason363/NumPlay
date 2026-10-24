import argparse,subprocess,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cc',default='cc');a=ap.parse_args();out=ROOT/'output';out.mkdir(exist_ok=True)
 flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter','-ffunction-sections','-fdata-sections']
 for name in ['game','ui']:
  files=['src/game.c','src/save.c','src/levels.c','tests/test_'+name+'.c'];objects=[]
  if name=='ui':files+=['src/render.c','src/title.c','../../launcher/src/gfx.c','../../launcher/src/font_data.c','../../launcher/src/xfont.c']
  for i,src in enumerate(files):
   obj=out/f'host-{name}-{i}.o';extra=[]
   if src=='src/render.c':extra=['-Dgfx_text=whg_checked_text','-Dgfx_text_center=whg_checked_center','-Dgfx_text_right=whg_checked_right']
   subprocess.run([a.cc,*flags,*extra,'-c',src,'-o',str(obj)],cwd=ROOT,check=True);objects.append(str(obj))
  exe=out/('test-'+name+('.exe'if os.name=='nt'else''));subprocess.run([a.cc,*objects,'-Wl,--gc-sections','-lm','-o',str(exe)],check=True);subprocess.run([str(exe)],cwd=out,check=True)
 from PIL import Image
 for p in out.glob('*.ppm'):Image.open(p).save(p.with_suffix('.png'))
if __name__=='__main__':main()
