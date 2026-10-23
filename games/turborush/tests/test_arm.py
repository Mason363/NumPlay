"""Execute the shipped ARM app, including file-system and firmware RAM guards.
Race-end fixtures skip driving three laps, already covered by test_engine.c.
"""
import argparse,ctypes as C,io,json,shlex,struct,tempfile,types
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
emu_path=REPO/'tools/emu.py';emu=types.ModuleType('turbo_emu');emu.__file__=str(emu_path);emu.io=io
source=emu_path.read_text().replace('ELFFile(open(elf, "rb"))','ELFFile(io.BytesIO(open(elf, "rb").read()))')
source=source.replace('cmd = [nwlink, "nwa-elf",','cmd = nwlink + ["nwa-elf",')
source=source.replace('chunk = CPU_HZ // 1000','chunk = CPU_HZ // 10000').replace('self.now_ms += 1\n','self.now_ms += 0.1\n')
source=source.replace('        next_frame = self.now_ms if frame_ms else None','        self.checkpoint = until_ms\n        next_frame = self.now_ms if frame_ms else None')
source=source.replace('            self.slept_ms += r0\n','            self.slept_ms += r0\n            if self.now_ms >= self.checkpoint: uc.emu_stop()\n')
exec(compile(source,str(emu_path),'exec'),emu.__dict__)
class Car(C.LittleEndianStructure):_fields_=[('distance',C.c_int32),('speed',C.c_int32),('x',C.c_int16),('lateral',C.c_int16),('finished',C.c_uint32),('lane',C.c_int16),('style',C.c_uint8),('skill',C.c_uint8)]
class Race(C.LittleEndianStructure):
 _fields_=[('cars',Car*8),('seed',C.c_uint32),('ticks',C.c_uint32),('time',C.c_uint32),('lap_time',C.c_uint32),('best_lap',C.c_uint32),('countdown',C.c_uint16),('nitro',C.c_uint16),('damage',C.c_uint16),('invulnerable',C.c_uint16),('offroad_ticks',C.c_uint16),('track',C.c_uint8),('mood',C.c_uint8),('manual',C.c_uint8),('difficulty',C.c_uint8),('phase',C.c_uint8),('gear',C.c_uint8),('rank',C.c_uint8),('lap',C.c_uint8),('boost',C.c_uint8),('steering',C.c_int16),('contacts',C.c_uint16*7),('impact',C.c_uint16),('boost_empty',C.c_uint16),('camera_x',C.c_int16),('mode',C.c_uint8),('overtakes',C.c_uint32),('distance_base',C.c_uint32),('heading',C.c_int16)]
def storage(path,items):
 data=bytearray(emu.STORAGE_SIZE);at=0
 for name,value in items:
  r=name.encode()+b'\0'+value;n=2+len(r);struct.pack_into('<H',data,at,n);data[at+2:at+n]=r;at+=n
 assert at+2<=len(data);path.write_bytes(data)
def records(data):
 result={};at=0
 while at+2<=len(data):
  n=struct.unpack_from('<H',data,at)[0]
  if not n:break
  assert n>=3 and at+n<=len(data);name,value=data[at+2:at+n].split(b'\0',1);result[name.decode()]=value;at+=n
 return result
def uint(c,name):return struct.unpack('<I',bytes(c.uc.mem_read(c.symbols[name],4)))[0]
def race(c):return Race.from_buffer_copy(bytes(c.uc.mem_read(c.symbols['race'],C.sizeof(Race))))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--nwlink',default='nwlink');ap.add_argument('--quick',action='store_true');a=ap.parse_args()
 nw=['node',a.nwlink]if Path(a.nwlink).is_file()and not a.nwlink.endswith(('.exe','.cmd'))else shlex.split(a.nwlink)
 out=ROOT/'output/arm';out.mkdir(parents=True,exist_ok=True);tempfile.tempdir=str(out);app=str(ROOT/'output/turborush-debug.nwa');report=[]
 def key(c,k,d=70):
  t=int(c.now_ms)+10;c.keys.append((t,t+d,emu.KEYS[k]));c.run(t+d+130)
 def hold(c,keys,d):
  t=int(c.now_ms)+10
  for k in keys:c.keys.append((t,t+d,emu.KEYS[k]))
  c.run(t+d+120)
 def check(c):assert not c.violations,sorted(set(c.violations));assert not c.firmware_ram_changes()
 def exit_check(c,ups=False):
  assert c.exited;check(c);assert c.app_ram_clean();assert c.locks==0 and c.max_locks==(0 if ups else 1)
 sentinel=b'print(42)\n';reference=None
 profiles=[('n0120','n0120','epsilon',153676),('n0110','n0110','epsilon',148928),('n0115','n0110','epsilon',133652),('upsilon','n0110','upsilon',107674)]
 for profile,model,system,ram in profiles[:1]if a.quick else profiles:
  emu.configure(model,system,ram);emu.CPU_HZ=216_000_000 if model=='n0120'else 86_400_000
  p=out/(profile+'.storage');storage(p,[('keep.py',sentinel)]);c=emu.Calculator(app,storage_file=str(p),nwlink=nw);c.enable_checks();c.run(220)
  assert uint(c,'screen')==0 and uint(c,'selected')==0;c.save_png(str(out/(profile+'-menu.png')))
  key(c,'down');key(c,'ok');assert uint(c,'screen')==3
  for _ in range(4):key(c,'down')
  key(c,'right');c.save_png(str(out/(profile+'-garage.png')));key(c,'back');assert uint(c,'screen')==0
  key(c,'up');key(c,'ok');assert uint(c,'screen')==8 and uint(c,'selected')==6
  for _ in range(3):key(c,'up')
  key(c,'right');key(c,'up');key(c,'right') # manual, night
  for _ in range(3):key(c,'down')
  key(c,'right');key(c,'down');c.save_png(str(out/(profile+'-setup.png')))
  key(c,'ok');assert uint(c,'screen')==1 and race(c).manual and race(c).mood==2
  c.run(c.now_ms+3200);assert race(c).phase==1
  hold(c,['up'],2200);assert race(c).cars[0].speed>0
  for _ in range(4):key(c,'7')
  assert race(c).gear==5
  hold(c,['up','ok'],2200);check(c);c.save_png(str(out/(profile+'-race.png')))
  r=race(c);assert r.cars[0].distance>10*256 and r.nitro<1000
  hold(c,['up','right'],350);assert race(c).steering>0
  hold(c,['up','left'],350);assert race(c).steering<0
  key(c,'back');assert uint(c,'screen')==2
  frozen=race(c).time;c.run(c.now_ms+450);assert race(c).time==frozen
  c.save_png(str(out/(profile+'-pause.png')))
  key(c,'down');key(c,'ok');assert uint(c,'screen')==4;key(c,'right');key(c,'back');assert uint(c,'screen')==2
  key(c,'down');key(c,'ok');assert uint(c,'screen')==10
  key(c,'back');assert uint(c,'screen')==1 and race(c).time>=frozen
  # Dense traffic in all four landscapes: real indexed art and LCD strips.
  for track in range(4):
   r=race(c);r.track=track;r.cars[0].distance=320*256;r.cars[0].x=0;r.cars[0].lateral=0;r.cars[0].speed=0;r.damage=0;r.impact=0
   for j in range(1,8):r.cars[j].distance=r.cars[0].distance+(j*5)*256;r.cars[j].x=((j%3)-1)*560;r.contacts[j-1]=1000
   c.uc.mem_write(c.symbols['race'],bytes(r));c.run(c.now_ms+150);check(c)
  key(c,'back');key(c,'down');key(c,'down');key(c,'down');key(c,'ok');assert uint(c,'screen')==10
  key(c,'down');key(c,'ok');assert uint(c,'screen')==0
  key(c,'ok');assert uint(c,'screen')==8;key(c,'ok');assert uint(c,'screen')==1
  # Finish each GP circuit through the app's result / next-circuit flow.
  segments=[90,95,85,100,90,90]
  for track in range(6):
   r=race(c);assert r.track==track;r.phase=1;r.time=200000;r.ticks=400;r.cars[0].distance=segments[track]*32*3*256-1;r.cars[0].speed=45*256;r.damage=0;r.cars[0].x=0;r.cars[0].finished=0
   c.uc.mem_write(c.symbols['race'],bytes(r));c.run(c.now_ms+120);assert uint(c,'screen')==5
   before=records(p.read_bytes())['turborush.sav'];c.run(c.now_ms+100);assert records(p.read_bytes())['turborush.sav']==before
   if track<5:key(c,'ok');assert uint(c,'screen')==1
  c.save_png(str(out/(profile+'-result.png')))
  reference=records(p.read_bytes())['turborush.sav'];assert len(reference)==80 and reference[:5]==b'TRSH\1'
  assert struct.unpack_from('<I',reference,16)[0]==6 and struct.unpack_from('<I',reference,8)[0]==4600
  assert reference[48]==1 and reference[51]==2 and reference[52]==1 and reference[54]==2
  key(c,'down');key(c,'ok');assert uint(c,'screen')==3;key(c,'ok');key(c,'back');assert uint(c,'screen')==5
  key(c,'down');key(c,'down');key(c,'ok');assert uint(c,'screen')==0
  key(c,'down');key(c,'down');key(c,'ok');assert uint(c,'screen')==9;c.save_png(str(out/(profile+'-records.png')));key(c,'back');assert uint(c,'screen')==0
  # Free Ride from the real setup menu, with no countdown or race rewards.
  free_before=records(p.read_bytes())['turborush.sav']
  key(c,'up');key(c,'up');key(c,'ok');assert uint(c,'screen')==8
  key(c,'down');key(c,'left');key(c,'up');key(c,'ok');assert uint(c,'screen')==1 and race(c).mode==2 and race(c).countdown==0
  hold(c,['up'],3000);hold(c,['up','right'],600)
  r=race(c);assert r.cars[0].x>180 and 0<r.camera_x<r.cars[0].x
  c.save_png(str(out/(profile+'-free-ride.png')))
  key(c,'back');frozen=race(c).time;c.run(c.now_ms+300);assert race(c).time==frozen;key(c,'back')
  r=race(c);r.time=600000;r.cars[0].distance=90*32*3*256-1;r.cars[0].speed=40*256;r.cars[0].x=0;r.camera_x=0
  c.uc.mem_write(c.symbols['race'],bytes(r));hold(c,['up'],200);assert race(c).phase==1 and race(c).cars[0].finished==0
  max_render=uint(c,'perf_max');max_race_render=uint(c,'perf_race_max');race_frames=uint(c,'perf_race_frames')
  key(c,'home');c.run(c.now_ms+100);exit_check(c,system=='upsilon');assert records(p.read_bytes())['keep.py']==sentinel
  saved=records(p.read_bytes())['turborush.sav'];assert saved[44]==1 and struct.unpack_from('<I',saved,8)[0]==4200
  assert saved[49]==2 and saved[8:48]==free_before[8:48]
  d=emu.Calculator(app,storage_file=str(p),nwlink=nw);d.enable_checks();d.run(200);key(d,'onoff');d.run(d.now_ms+100);exit_check(d,system=='upsilon');assert records(p.read_bytes())['turborush.sav']==saved
  report.append(dict(profile=profile,peak_stack=max(c.stack_used(),d.stack_used()),render_max_ms=max_render,race_render_max_ms=max_race_render,race_frames=race_frames,gameplay=True,manual_and_boost=True,pause_freezes=True,six_circuit_grand_prix=True,reward_once=True,free_ride=True,visible_car_steering=True,free_ride_no_race_rewards=True,shop_and_save=True,firmware_memory_preserved=True,clean_exit=True))
  print(json.dumps(report[-1]),flush=True)
 emu.configure();emu.CPU_HZ=216_000_000
 p=out/'corrupt.storage';bad=bytearray(reference);bad[20]^=128;storage(p,[('keep.py',sentinel),('turborush.sav',bad)])
 c=emu.Calculator(app,storage_file=str(p),nwlink=nw);c.enable_checks();c.run(200);key(c,'home');c.run(c.now_ms+100);exit_check(c);assert records(p.read_bytes())['keep.py']==sentinel
 p=out/'full.storage';filler=b'x'*(emu.STORAGE_SIZE-24);storage(p,[('full.py',filler)])
 c=emu.Calculator(app,storage_file=str(p),nwlink=nw);c.enable_checks();c.run(200)
 key(c,'down');key(c,'ok');key(c,'down');key(c,'down');key(c,'down');key(c,'down');key(c,'right');key(c,'home');assert uint(c,'screen')==6
 key(c,'ok');assert uint(c,'screen')==6;key(c,'down');key(c,'down');key(c,'ok');c.run(c.now_ms+100);exit_check(c);assert records(p.read_bytes())=={'full.py':filler}
 c=emu.Calculator(app,nwlink=nw);c.enable_checks();c.keys=[(0,400,emu.KEYS['ok'])];c.run(500);assert uint(c,'screen')==0;key(c,'home');c.run(c.now_ms+100);exit_check(c)
 report.append(dict(corrupt_save_ignored=True,full_storage_handled=True,held_launch_key_ignored=True))
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('ARM tests passed',flush=True)
if __name__=='__main__':main()
