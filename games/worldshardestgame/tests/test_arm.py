"""Run the linked app with real key events, storage and firmware guards."""
import argparse,ctypes as C,io,json,shlex,struct,tempfile,types
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
path=REPO/'tools/emu.py';emu=types.ModuleType('whg_emu');emu.__file__=str(path);emu.io=io
source=path.read_text().replace('ELFFile(open(elf, "rb"))','ELFFile(io.BytesIO(open(elf, "rb").read()))')
source=source.replace('cmd = [nwlink, "nwa-elf",','cmd = nwlink + ["nwa-elf",').replace('chunk = CPU_HZ // 1000','chunk = CPU_HZ // 10000').replace('self.now_ms += 1\n','self.now_ms += 0.1\n')
source=source.replace('        next_frame = self.now_ms if frame_ms else None','        self.checkpoint = until_ms\n        next_frame = self.now_ms if frame_ms else None').replace('            self.slept_ms += r0\n','            self.slept_ms += r0\n            if self.now_ms >= self.checkpoint: uc.emu_stop()\n')
exec(compile(source,str(path),'exec'),emu.__dict__)
U8=C.c_uint8;U16=C.c_uint16;U32=C.c_uint32;I16=C.c_int16
class Run(C.LittleEndianStructure):_fields_=[(n,U32)for n in ['frame','elapsed','deaths']]+[(n,I16)for n in ['x','y']]+[(n,U8)for n in ['level','checkpoint','phase','timer','collected']]+[('coins',U8*9)]
class Profile(C.LittleEndianStructure):_fields_=[('run',Run)]+[(n,U32)for n in ['best_deaths','best_ticks','last_deaths','finishes']]+[('has_run',U8),('furthest',U8)]
class App(C.LittleEndianStructure):_fields_=[('profile',Profile)]+[(n,C.c_int)for n in ['screen','selected','return_to','confirm','save_return']]+[(n,C.c_bool)for n in ['dirty','quit','exit_after_save']]+[('enemy_x',I16*296),('enemy_y',I16*296)]
class Level(C.LittleEndianStructure):_fields_=[(n,U16)for n in ['wall_offset','wall_count','enemy_offset','enemy_count','coin_offset','coin_count','check_offset','check_count','goal']]+[('taunt',U32)]
assert C.sizeof(Run)==32 and C.sizeof(Profile)==52 and C.sizeof(App)==1260 and C.sizeof(Level)==24
def state(c):return App.from_buffer_copy(bytes(c.uc.mem_read(c.symbols['app'],C.sizeof(App))))
def level(c,n):return Level.from_buffer_copy(bytes(c.uc.mem_read(c.symbols['whg_levels']+C.sizeof(Level)*n,C.sizeof(Level))))
def check_rect(c,index):return struct.unpack('<6h',bytes(c.uc.mem_read(c.symbols['whg_checks']+12*index,12)))
def write_run(c,r):c.uc.mem_write(c.symbols['app']+Profile.run.offset,bytes(r))
def fresh(c,n):
 l=level(c,n);cp=check_rect(c,l.check_offset);r=Run();r.level=n;r.phase=1;r.x=cp[4];r.y=cp[5];return r
def storage(path,items):
 b=bytearray(emu.STORAGE_SIZE);at=0
 for name,value in items:
  record=name.encode()+b'\0'+value;n=2+len(record);struct.pack_into('<H',b,at,n);b[at+2:at+n]=record;at+=n
 assert at+2<=len(b);path.write_bytes(b)
def records(b):
 at=0;out={}
 while at+2<=len(b):
  n=struct.unpack_from('<H',b,at)[0]
  if not n:break
  assert n>=3 and at+n<=len(b);name,value=b[at+2:at+n].split(b'\0',1);out[name.decode()]=value;at+=n
 return out
def metric(c,n):return struct.unpack('<I',bytes(c.uc.mem_read(c.symbols[n],4)))[0]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--nwlink',default='nwlink');ap.add_argument('--quick',action='store_true');a=ap.parse_args()
 nw=['node',a.nwlink]if Path(a.nwlink).is_file()and not a.nwlink.endswith(('.exe','.cmd'))else shlex.split(a.nwlink)
 out=ROOT/'output/arm';out.mkdir(parents=True,exist_ok=True);tempfile.tempdir=str(out);app=str(ROOT/'output/worldshardestgame-debug.nwa');report=[]
 def key(c,k,duration=75):
  t=int(c.now_ms)+5;c.keys.append((t,t+duration,emu.KEYS[k]));c.run(t+duration+100)
 def check(c):assert not c.violations,sorted(set(c.violations));assert not c.firmware_ram_changes()
 def exit_check(c,ups=False):assert c.exited;check(c);assert c.app_ram_clean();assert c.locks==0 and c.max_locks==(0 if ups else 1)
 sentinel=b'print(42)\n';saved=None
 profiles=[('n0120','n0120','epsilon',153676),('n0110','n0110','epsilon',148928),('n0115','n0110','epsilon',133652),('upsilon','n0110','upsilon',107674)]
 for profile,model,system,ram in profiles[:1]if a.quick else profiles:
  emu.configure(model,system,ram);emu.CPU_HZ=216_000_000 if model=='n0120'else 86_400_000
  path=out/(profile+'.storage');storage(path,[('keep.py',sentinel)]);c=emu.Calculator(app,storage_file=str(path),nwlink=nw);c.enable_checks();c.run(180);assert state(c).screen==0;c.save_png(str(out/(profile+'-menu.png')))
  key(c,'right');key(c,'ok');assert state(c).screen==4;key(c,'back');key(c,'right');key(c,'ok');assert state(c).screen==3;c.save_png(str(out/(profile+'-help.png')));key(c,'back');key(c,'right');key(c,'ok');assert state(c).screen==5;key(c,'back');key(c,'right');key(c,'ok');assert state(c).screen==1
  key(c,'ok');assert state(c).profile.run.phase==1
  x=state(c).profile.run.x;key(c,'right',160);assert state(c).profile.run.x>x
  key(c,'back');assert state(c).screen==2;frame=state(c).profile.run.frame;c.run(c.now_ms+700);assert state(c).profile.run.frame==frame
  key(c,'down');key(c,'down');key(c,'ok');assert state(c).screen==3;key(c,'back');assert state(c).screen==2
  key(c,'down');key(c,'ok');assert state(c).screen==6;key(c,'left');key(c,'ok');assert state(c).screen==0 and state(c).profile.has_run
  key(c,'ok');assert state(c).screen==1
  for n in range(30):
   write_run(c,fresh(c,n));c.run(c.now_ms+160);check(c);assert state(c).profile.run.level==n
   if n in [0,5,11,17,19,25,29]:c.save_png(str(out/(profile+f'-level-{n+1:02d}.png')))
  # Original advice interstitials wait for input; the announcement then runs.
  r=fresh(c,19);r.phase=4;write_run(c,r);c.run(c.now_ms+300);assert state(c).profile.run.phase==4 and state(c).profile.run.frame==0
  key(c,'7');c.save_png(str(out/(profile+'-tip.png')));key(c,'ok');assert state(c).profile.run.phase==0;key(c,'ok');assert state(c).profile.run.phase==1
  # A real enemy collision starts the fade; obstacles continue during death.
  write_run(c,fresh(c,0));c.run(c.now_ms+80);s=state(c);r=s.profile.run;r.x=s.enemy_x[0];r.y=s.enemy_y[0];write_run(c,r);c.run(c.now_ms+80);assert state(c).profile.run.phase==2
  before=state(c).profile.run.frame;c.run(c.now_ms+1100);assert state(c).profile.run.deaths>=1 and state(c).profile.run.frame>before
  # The centre coin of stage 2 is collectible through the native engine.
  r=fresh(c,1);r.x=5500;r.y=4000;write_run(c,r);c.run(c.now_ms+45);assert state(c).profile.run.collected==1
  r=state(c).profile.run;l=level(c,1);cp=check_rect(c,l.check_offset+l.goal);r.x=(cp[0]+cp[2])//2;r.y=(cp[1]+cp[3])//2;write_run(c,r);c.run(c.now_ms+60);assert state(c).profile.run.level==2
  # Normal Home exit saves a live stage, including its checkpoint and phase.
  max_live=metric(c,'perf_play_max');max_all=metric(c,'perf_max');key(c,'home');c.run(c.now_ms+80);exit_check(c,system=='upsilon');saved=records(path.read_bytes());assert saved['keep.py']==sentinel and len(saved['whg.sav'])==72
  # A website reinstall keeps scripts only; the inert backup restores progress.
  restored=out/(profile+'-restore.storage');storage(restored,[(k,v)for k,v in saved.items()if k.endswith('.py')]);d=emu.Calculator(app,storage_file=str(restored),nwlink=nw);d.enable_checks();d.run(180);assert state(d).profile.has_run and state(d).profile.run.level==2
  key(d,'ok');assert state(d).screen==1
  r=fresh(d,29);l=level(d,29);cp=check_rect(d,l.check_offset+l.goal);r.x=(cp[0]+cp[2])//2;r.y=(cp[1]+cp[3])//2;r.collected=4;r.coins[0]=15;write_run(d,r);d.run(d.now_ms+100);assert state(d).screen==8 and state(d).profile.finishes==1 and not state(d).profile.has_run
  d.run(d.now_ms+400);assert state(d).profile.finishes==1;key(d,'onoff');d.run(d.now_ms+80);exit_check(d,system=='upsilon')
  report.append(dict(profile=profile,max_play_render_ms=max_live,max_render_ms=max_all,peak_stack=max(c.stack_used(),d.stack_used()),all_30_levels=True,controls=True,pause_freezes=True,real_enemy_collision=True,coins=True,completion_once=True,script_backup_restore=True,firmware_preserved=True,clean_exit=True));print(json.dumps(report[-1]),flush=True)
 emu.configure();emu.CPU_HZ=216_000_000
 path=out/'full.storage';filler=b'x'*(emu.STORAGE_SIZE-24);storage(path,[('full.py',filler)]);c=emu.Calculator(app,storage_file=str(path),nwlink=nw);c.enable_checks();c.run(180);key(c,'ok');key(c,'home');assert state(c).screen==7;key(c,'ok');assert state(c).screen==7;key(c,'down');key(c,'down');key(c,'ok');c.run(c.now_ms+80);exit_check(c);assert records(path.read_bytes())=={'full.py':filler}
 path=out/'corrupt.storage';bad=bytearray(saved['whg.sav']);bad[30]^=128;storage(path,[('whg.sav',bytes(bad)),('keep.py',sentinel)]);c=emu.Calculator(app,storage_file=str(path),nwlink=nw);c.enable_checks();c.run(180);assert not state(c).profile.has_run;key(c,'home');c.run(c.now_ms+80);exit_check(c)
 c=emu.Calculator(app,nwlink=nw);c.enable_checks();c.keys=[(0,350,emu.KEYS['ok'])];c.run(450);assert state(c).screen==0;key(c,'home');c.run(c.now_ms+80);exit_check(c)
 report.append(dict(full_storage=True,corrupt_save=True,held_launch_key=True));(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('ARM tests passed',flush=True)
if __name__=='__main__':main()
