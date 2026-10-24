"""Build the native app and verify the four firmware/RAM profiles."""
import argparse,json,shlex,shutil,subprocess,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--toolchain',type=Path);ap.add_argument('--nwlink',default='nwlink');a=ap.parse_args()
 out=ROOT/'output';out.mkdir(exist_ok=True)
 def run(*args,capture=False):return subprocess.run(list(map(str,args)),cwd=ROOT,check=True,capture_output=capture,text=capture)
 def tool(n):return a.toolchain/('arm-none-eabi-'+n+('.exe'if os.name=='nt'else''))if a.toolchain else 'arm-none-eabi-'+n
 nw=['node',a.nwlink]if Path(a.nwlink).is_file()and not a.nwlink.endswith(('.exe','.cmd'))else shlex.split(a.nwlink)
 flags=['-std=c11','-Os','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fno-math-errno','-ffunction-sections','-fdata-sections','-flto','-fstack-usage','-fno-tree-loop-distribute-patterns','-fvisibility=internal','-DEPSILON_XL=1']
 flags+=shlex.split(run(*nw,'eadk-cflags-device',capture=True).stdout)
 objects=[]
 for src in [*(ROOT/'src'/s for s in ['main.c','game.c','save.c','levels.c','title.c','render.c']),*(REPO/'launcher/src'/s for s in ['gfx.c','font_data.c','xfont.c'])]:
  obj=out/(src.stem+'.o');run(tool('gcc'),*flags,'-c',src,'-o',obj);objects.append(obj)
 run(*nw,'png-icon-o',ROOT/'src/icon.png',out/'icon.o')
 run(tool('gcc'),*flags,'-nostartfiles','--specs=nano.specs','-Wl,--relocatable,--gc-sections,-e,main','-flinker-output=nolto-rel',*objects,out/'icon.o','-Wl,-u,eadk_app_name,-u,eadk_app_icon,-u,eadk_api_level','-lm','-o',out/'worldshardestgame-debug.nwa')
 shutil.copyfile(out/'worldshardestgame-debug.nwa',out/'WorldsHardestGame.nwa');run(tool('strip'),'--strip-unneeded',out/'WorldsHardestGame.nwa')
 if os.name!='nt':shutil.copyfile(out/'WorldsHardestGame.nwa',out/'worldshardestgame.nwa')
 from elftools.elf.elffile import ELFFile
 report=[]
 for name,base,ram in [('n0120',0x24000000,153676),('n0110',0x20000000,148928),('n0115',0x20000000,133652),('upsilon',0x20000000,107674)]:
  opts=['--ram-start',hex(base+0x37000-ram),'--ram-length',str(ram)]
  run(*nw,'nwa-elf',out/'WorldsHardestGame.nwa',out/(name+'.elf'),*opts);run(*nw,'nwa-bin',out/'WorldsHardestGame.nwa',out/(name+'.bin'),*opts)
  with (out/(name+'.elf')).open('rb')as f:
   e=ELFFile(f);s={x.name:x.entry.st_value for x in e.get_section_by_name('.symtab').iter_symbols()};used=s['_heap_start']-s['_data_section_start_ram']
  size=(out/(name+'.bin')).stat().st_size;assert used<24*1024 and size<2031616
  report.append(dict(profile=name,static_ram=used,installed_flash=size,available_ram=ram))
 result=dict(nwa_bytes=(out/'WorldsHardestGame.nwa').stat().st_size,save_bytes=72,profiles=report);(out/'sizes.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
