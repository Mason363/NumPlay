import argparse,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cc',default='cc');ap.add_argument('--suite',nargs='+',choices=['engine','gameplay','upgrade','penalties','ui','refinement','rules','revision']);a=ap.parse_args();out=ROOT/'output';out.mkdir(exist_ok=True)
 base=['src/engine.c','src/setpieces.c','src/tactics.c','src/penalties.c','src/training.c','src/progress.c','src/teams.c']
 flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter','-ffunction-sections','-fdata-sections']
 for name in a.suite or ['engine','gameplay','upgrade','penalties','refinement','rules','revision','ui']:
  objects=[]
  files=[*base,'tests/test_'+name+'.c']
  if name=='ui':files+=['src/render.c','../../launcher/src/gfx.c','../../launcher/src/font_data.c','../../launcher/src/xfont.c']
  for i,src in enumerate(files):
   obj=out/f'host-{name}-{i}.o';extra=[]
   if src=='src/render.c':extra=['-Dgfx_text=fc_checked_text','-Dgfx_text_center=fc_checked_center','-Dgfx_text_right=fc_checked_right']
   subprocess.run([a.cc,*flags,*extra,'-c',src,'-o',str(obj)],cwd=ROOT,check=True);objects.append(str(obj))
  exe=out/('test-'+name+('.exe'if os.name=='nt'else''));subprocess.run([a.cc,*objects,'-Wl,--gc-sections','-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],cwd=out,check=True)
 from PIL import Image
 for p in out.glob('*.ppm'):Image.open(p).save(p.with_suffix('.png'))
if __name__=='__main__':main()
