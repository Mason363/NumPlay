"""Portable NumWorks build. Requires ARM GCC, nwlink 1.0.0 and pyelftools."""
import argparse, json, os, shlex, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--toolchain',type=Path);ap.add_argument('--nwlink',default='nwlink');a=ap.parse_args()
    out=ROOT/'output';out.mkdir(exist_ok=True)
    # Root make can request the standalone app and module simultaneously.
    # Serialize their shared compiler outputs; OS locks are released on exit.
    with (out/'.build.lock').open('a+b') as lock:
        if os.name=='nt':
            import msvcrt
            if lock.seek(0,2)==0:lock.write(b'\0');lock.flush()
            lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_LOCK,1)
        else:
            import fcntl
            fcntl.flock(lock.fileno(),fcntl.LOCK_EX)
        try:build(a,out)
        finally:
            if os.name=='nt':lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)
            else:fcntl.flock(lock.fileno(),fcntl.LOCK_UN)

def build(a,out):
    def run(*args,capture=False):
        return subprocess.run(list(map(str,args)),cwd=ROOT,check=True,capture_output=capture,text=capture)
    def tool(n):
        name='arm-none-eabi-'+n+('.exe'if os.name=='nt'else'');return a.toolchain/name if a.toolchain else name
    nw=['node',a.nwlink]if Path(a.nwlink).is_file()and not a.nwlink.endswith(('.exe','.cmd'))else shlex.split(a.nwlink)
    flags=['-DEPSILON_XL=1','-std=c11','-Os','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fno-math-errno','-ffunction-sections','-fdata-sections','-flto','-fstack-usage',
           '-fno-tree-loop-distribute-patterns','-fvisibility=internal']
    flags+=shlex.split(run(*nw,'eadk-cflags-device',capture=True).stdout)
    objects=[]
    for src in [ROOT/'src/main.c',ROOT/'src/race.c',ROOT/'src/render.c',ROOT/'src/road.c',REPO/'launcher/src/gfx.c',REPO/'launcher/src/font_data.c',REPO/'launcher/src/xfont.c']:
        obj=out/(src.stem+'.o');run(tool('gcc'),*flags,'-c',src,'-o',obj);objects.append(obj)
    run(*nw,'png-icon-o',ROOT/'src/icon.png',out/'icon.o')
    link=[tool('gcc'),*flags,'-nostartfiles','--specs=nano.specs','-Wl,--relocatable,--gc-sections,-e,main','-flinker-output=nolto-rel']
    run(*link,*objects,out/'icon.o','-Wl,-u,eadk_app_name,-u,eadk_app_icon,-u,eadk_api_level','-lm','-o',out/'turborush-debug.nwa')
    shutil.copyfile(out/'turborush-debug.nwa',out/'TurboRush.nwa');run(tool('strip'),'--strip-unneeded',out/'TurboRush.nwa')
    # NumPlay's conventional standalone target reads output/<id>.nwa.
    if os.name!='nt':shutil.copyfile(out/'TurboRush.nwa',out/'turborush.nwa')
    # Module build leaves graphics, fonts and storage to the NumPlay launcher.
    mod=[]
    for source in ['main','race','render','road']:
        obj=out/('module-'+source+'.o');run(tool('gcc'),*flags,'-DNP_MODULE=1','-c',ROOT/'src'/(source+'.c'),'-o',obj);mod.append(obj)
    run(*link,*mod,'-nostdlib','-o',out/'module.o')
    from elftools.elf.elffile import ELFFile
    profiles=[]
    for name,base,ram in [('n0120',0x24000000,153676),('n0110',0x20000000,148928),('n0115',0x20000000,133652),('upsilon',0x20000000,107674)]:
        opts=['--ram-start',hex(base+0x37000-ram),'--ram-length',str(ram)];elf=out/(name+'.elf');binary=out/(name+'.bin')
        run(*nw,'nwa-elf',out/'TurboRush.nwa',elf,*opts);run(*nw,'nwa-bin',out/'TurboRush.nwa',binary,*opts)
        debug=out/(name+'-debug.bin');run(*nw,'nwa-bin',out/'turborush-debug.nwa',debug,*opts)
        assert debug.read_bytes()==binary.read_bytes()
        with elf.open('rb')as f:
            e=ELFFile(f);sym={s.name:s.entry.st_value for s in e.get_section_by_name('.symtab').iter_symbols()};static_ram=sym['_heap_start']-sym['_data_section_start_ram']
        assert static_ram<24*1024,(name,static_ram)
        profiles.append(dict(profile=name,static_ram=static_ram,installed_flash=binary.stat().st_size,available_ram=ram))
    result=dict(nwa_bytes=(out/'TurboRush.nwa').stat().st_size,save_bytes=80,profiles=profiles)
    (out/'sizes.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
