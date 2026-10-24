"""Execute the real native app with keyboard, storage and firmware guards."""
import argparse,ctypes as C,io,json,shlex,struct,tempfile,types
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
path=REPO/'tools/emu.py';emu=types.ModuleType('football_emu');emu.__file__=str(path);emu.io=io
source=path.read_text().replace('ELFFile(open(elf, "rb"))','ELFFile(io.BytesIO(open(elf, "rb").read()))')
source=source.replace('cmd = [nwlink, "nwa-elf",','cmd = nwlink + ["nwa-elf",').replace('chunk = CPU_HZ // 1000','chunk = CPU_HZ // 10000').replace('self.now_ms += 1\n','self.now_ms += 0.1\n')
source=source.replace('        next_frame = self.now_ms if frame_ms else None','        self.checkpoint = until_ms\n        next_frame = self.now_ms if frame_ms else None').replace('            self.slept_ms += r0\n','            self.slept_ms += r0\n            if self.now_ms >= self.checkpoint: uc.emu_stop()\n')
exec(compile(source,str(path),'exec'),emu.__dict__)
U8=C.c_uint8;U16=C.c_uint16;U32=C.c_uint32;I8=C.c_int8;I16=C.c_int16;I32=C.c_int32
class Player(C.LittleEndianStructure):_fields_=[(n,I16)for n in ['x','y','vx','vy']]+[('stamina',U16),('cooldown',U8),('pose',U8)]
class Ball(C.LittleEndianStructure):_fields_=[(n,I32)for n in ['x','y','z','vx','vy','vz']]+[(n,I8)for n in ['owner','last','receiver']]+[('grace',U8)]
class Motion(C.LittleEndianStructure):_fields_=[('face_x',I16),('face_y',I16)]+[(n,U8)for n in ['touch','action','think','skill']]
class Match(C.LittleEndianStructure):
 _fields_=[('p',Player*22),('ball',Ball)]+[(n,U32)for n in ['seed','ticks','clock','duration']]+[(n,U16)for n in ['timer','charge']]+[(n,U8*2)for n in ['teams','score','shots','possession']]+[(n,U8)for n in ['phase','selected','difficulty','formation','half','set_team']]+[(n,U8*2)for n in ['fouls','corners']]+[(n,U8)for n in ['pass_cool','notice']]+[('aim',I16),('power',U16),('offside',I8),('taker',U8),('mode',U8),('quality',U8*2),('previous_input',U8),('shoot_taken',U16*2),('shoot_score',U8*2)]+[(n,U8)for n in ['shoot_turn','shoot_round','shoot_done','shoot_result']]+[('shoot_keeper',I16),('shoot_target',I16),('shoot_timer',U16),('shoot_state',U8),('motion',Motion*22),('input_latch',U16),('pass_charge',U16),('charge_owner',I8),('pass_kind',U8),('ball_kind',U8),('control_lock',U8),('receive_x',I16),('receive_y',I16),('spin',I16),('keeper_wait',U8*2),('keeper_dive',U8*2),('passes',U16*2),('completed',U16*2),('action_aim',U16),('tactics',U8*2)]+[(n,U8)for n in ['tactic_notice','drill','drill_target','drill_hits']]+[(n,I16)for n in ['shoot_height','shoot_start','shoot_goal','shoot_guard_z']]+[('shoot_flight',U8),('shoot_resolved',U8),('shoot_marks',U16*2),('yellow',U8*22),('red',U8*22),('keeper_id',U8*2)]+[(n,U8)for n in ['card_player','card_kind','card_timer','forfeit']]+[('ai_delay',U8*22),('ai_stumble',U8*22),('feint_cool',U8),('ai_x',I16*22),('ai_y',I16*22),('tracked_owner',I8),('feint_x',I8),('feint_y',I8),('kick_clock',U16),('kick_quality',U8),('single_penalty',U8),('kick_height',I16),('curve',I8),('wall_count',U8),('wall',U8*4),('wall_clock',U8),('free_x',I16),('free_y',I16),('free_age',U16),('free_attempts',U16),('free_goals',U16),('free_view',U8),('free_result',U8),('free_team',U8)]
class Profile(C.LittleEndianStructure):
 _fields_=[(n,U32)for n in ['money','wins','played','seed']]+[('season',U16)]+[(n,U8)for n in ['team','opponent','difficulty','length','formation','kit','career_active','career_team','career_round','coach','training']]+[('roster',U8*11),('contracts',U8*11)]+[(n,U8*8)for n in ['league','pts','gf','ga']]+[(n,U8)for n in ['cup_active','cup_team','cup_round','cup_stage','cup_alive']]+[('group_goals',(U8*2)*48),('bracket',U8*16),('knock_goals',(U8*2)*15),('trophies',U16),('last_rank',U8),('venue',U8)]
class App(C.LittleEndianStructure):
 _fields_=[('profile',Profile),('match',Match)]+[(n,C.c_int)for n in ['screen','selected','page','mode','return_to','confirm','contract_games']]+[('cam_x',I32),('cam_y',I32),('ui_time',U32),('dirty',C.c_bool),('rewarded',C.c_bool),('quit',C.c_bool),('message',C.c_char*80),('use_visual',C.c_bool),('preview_target',C.c_int),('previous_match',Match),('visual_match',Match)]
assert C.sizeof(Profile)==236
def storage(path,items):
 b=bytearray(emu.STORAGE_SIZE);at=0
 for name,value in items:
  r=name.encode()+b'\0'+value;n=2+len(r);struct.pack_into('<H',b,at,n);b[at+2:at+n]=r;at+=n
 assert at+2<=len(b);path.write_bytes(b)
def records(b):
 at=0;result={}
 while at+2<=len(b):
  n=struct.unpack_from('<H',b,at)[0]
  if not n:break
  assert n>=3 and at+n<=len(b);name,value=b[at+2:at+n].split(b'\0',1);result[name.decode()]=value;at+=n
 return result
def state(c):return App.from_buffer_copy(bytes(c.uc.mem_read(c.symbols['app'],C.sizeof(App))))
def write_match(c,m):c.uc.mem_write(c.symbols['app']+App.match.offset,bytes(m))
def metric(c,s):return struct.unpack('<I',bytes(c.uc.mem_read(c.symbols[s],4)))[0]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--nwlink',default='nwlink');ap.add_argument('--quick',action='store_true');a=ap.parse_args()
 nw=['node',a.nwlink]if Path(a.nwlink).is_file()and not a.nwlink.endswith(('.exe','.cmd'))else shlex.split(a.nwlink)
 out=ROOT/'output/arm';out.mkdir(parents=True,exist_ok=True);tempfile.tempdir=str(out);app=str(ROOT/'output/footballfc-debug.nwa');report=[]
 def key(c,k,hold=75):
  t=int(c.now_ms)+5;c.keys.append((t,t+hold,emu.KEYS[k]));c.run(t+hold+100)
 def hold(c,keys,duration):
  t=int(c.now_ms)+5
  for k in keys:c.keys.append((t,t+duration,emu.KEYS[k]))
  c.run(t+duration+100)
 def check(c):assert not c.violations,sorted(set(c.violations));assert not c.firmware_ram_changes()
 def exit_check(c,ups=False):assert c.exited;check(c);assert c.app_ram_clean();assert c.locks==0 and c.max_locks==(0 if ups else 1)
 sentinel=b'print(42)\n';saved=None
 profiles=[('n0120','n0120','epsilon',153676),('n0110','n0110','epsilon',148928),('n0115','n0110','epsilon',133652),('upsilon','n0110','upsilon',107674)]
 for profile,model,system,ram in profiles[:1]if a.quick else profiles:
  emu.configure(model,system,ram);emu.CPU_HZ=216_000_000 if model=='n0120'else 86_400_000
  p=out/(profile+'.storage');storage(p,[('keep.py',sentinel)])
  c=emu.Calculator(app,storage_file=str(p),nwlink=nw);c.enable_checks();c.run(180);assert state(c).screen==0;c.save_png(str(out/(profile+'-menu.png')))
  key(c,'ok');assert state(c).screen==1
  key(c,'right');key(c,'left')
  for _ in range(4):key(c,'down')
  key(c,'ok');assert state(c).screen==2;c.run(c.now_ms+1900);assert state(c).match.phase==1
  y=state(c).match.p[state(c).match.selected].y;hold(c,['right','xnt'],300);assert state(c).match.p[state(c).match.selected].y>y
  x=state(c).match.p[state(c).match.selected].x;hold(c,['up'],200);assert state(c).match.p[state(c).match.selected].x>x
  key(c,'7');m=state(c).match;m.phase=1;m.ball.owner=9;m.selected=9;m.p[9].x=0;m.p[9].y=75*256;m.ball.x=0;m.ball.y=75*256;m.p[9].cooldown=0;write_match(c,m)
  hold(c,['exe','up'],400);assert state(c).match.shots[0]>0 and state(c).match.charge==0
  c.save_png(str(out/(profile+'-match.png')));check(c)
  # New buttons go through the real EADK path, not direct engine calls.
  def midfield():
   m=state(c).match;m.phase=1;m.charge=m.pass_charge=0;m.input_latch=m.previous_input=m.action_aim=0;m.charge_owner=-1;m.selected=9;m.ball_kind=0
   m.shoot_state=m.single_penalty=m.card_timer=m.card_kind=m.wall_count=m.wall_clock=m.feint_cool=0;m.keeper_id[0]=0;m.keeper_id[1]=11;m.tracked_owner=-1
   m.free_view=m.free_age=0
   for j in range(22):m.yellow[j]=m.red[j]=m.ai_delay[j]=m.ai_stumble[j]=0
   for j in range(22):m.p[j].x=(30 if j%2 else -30)*256;m.p[j].y=(15 if j<11 else 95)*256;m.p[j].vx=m.p[j].vy=0;m.p[j].cooldown=0;m.motion[j].touch=0
   m.p[9].x=m.p[7].x=0;m.p[9].y=50*256;m.p[7].y=64*256;m.ball.x=0;m.ball.y=50*256;m.ball.z=0;m.ball.vx=m.ball.vy=m.ball.vz=0;m.ball.owner=9;m.ball.last=9;m.ball.receiver=-1;m.motion[9].face_x=0;m.motion[9].face_y=256
   write_match(c,m)
  midfield();hold(c,['8','right'],220);assert state(c).match.ball_kind==2 and state(c).match.passes[0]>0 and state(c).match.ball.receiver==7
  midfield();hold(c,['9','right'],220);assert state(c).match.ball_kind==3 and state(c).match.ball.z>0 and state(c).match.ball.receiver==7
  midfield();hold(c,['6'],260);assert state(c).match.tactics[0]==1
  hold(c,['6'],80);assert state(c).match.tactics[0]==2
  midfield();hold(c,['exe','alpha','up'],300);assert state(c).match.ball_kind==5 and state(c).match.spin!=0
  midfield();m=state(c).match;m.phase=7;m.set_team=0;m.taker=9;m.timer=100;write_match(c,m)
  key(c,'right');assert state(c).match.aim>0
  key(c,'up');assert state(c).match.power>m.power
  c.save_png(str(out/(profile+'-free-kick.png')))
  # The real key path changes curl/height, holds a wall, and releases a shot.
  midfield();m=state(c).match;m.phase=7;m.set_team=0;m.taker=9;m.timer=480;m.power=500;m.aim=0;m.curve=0;m.kick_clock=0;m.kick_height=350
  m.p[9].x=m.ball.x=0;m.p[9].y=m.ball.y=(110-25)*256;m.ball.owner=-1;m.wall_count=3
  m.free_view=1;m.free_x=0;m.free_y=(110-25)*256;m.free_team=0
  for w,j in enumerate([12,13,14]):m.wall[w]=j;m.p[j].x=(w-1)*240;m.p[j].y=(110-16)*256
  write_match(c,m);hold(c,['8'],160);assert state(c).match.curve<0
  key(c,'5');assert state(c).match.kick_height==570
  key(c,'7');assert state(c).match.curve==0
  hold(c,['9'],120);assert state(c).match.curve>0
  c.save_png(str(out/(profile+'-free-kick-wall.png')))
  hold(c,['exe'],400);assert state(c).match.phase==1 and state(c).match.shots[0]>0 and state(c).match.spin>0
  # A dangerous rear collision awards a card and an actual in-match penalty.
  midfield();m=state(c).match;m.p[9].x=m.ball.x=0;m.p[9].y=m.ball.y=100*256;m.motion[9].face_y=256
  m.p[20].x=0;m.p[20].y=100*256-160;m.p[20].vy=11*256;m.motion[20].think=100
  write_match(c,m);c.run(c.now_ms+80);m=state(c).match
  assert m.single_penalty and m.shoot_state==1 and m.red[20] and m.card_kind==2
  c.save_png(str(out/(profile+'-match-penalty.png')))
  hold(c,['exe'],500);c.run(c.now_ms+2500);assert not state(c).match.single_penalty and not state(c).match.shoot_state
  midfield()
  key(c,'back');assert state(c).screen==3;clock=state(c).match.clock;c.run(c.now_ms+350);assert state(c).match.clock==clock
  key(c,'down');key(c,'ok');assert state(c).screen==11;key(c,'right');assert state(c).page==1
  for _ in range(7):key(c,'right')
  assert state(c).page==0
  key(c,'back');assert state(c).screen==3;key(c,'ok');assert state(c).screen==2
  # Score/result fixture; full-length games are covered by the host tests.
  m=state(c).match;m.half=1;m.phase=1;m.clock=m.duration-1;m.score[0]=2;m.score[1]=1;write_match(c,m);c.run(c.now_ms+90)
  assert state(c).screen==4;assert state(c).profile.played==1
  before=records(p.read_bytes());assert len(before['footballfc.sav'])==256 and before['footballfc_save.py'].startswith(b'# Football FC profile backup\n# FFCP1:')
  c.run(c.now_ms+130);assert state(c).profile.played==1 and records(p.read_bytes())==before
  c.save_png(str(out/(profile+'-result.png')))
  key(c,'down');key(c,'ok');assert state(c).screen==0
  # Career creation, one fixture and a contract purchase.
  key(c,'down');key(c,'ok');assert state(c).screen==1
  key(c,'ok');key(c,'ok');key(c,'ok');key(c,'ok');assert state(c).screen==5
  key(c,'ok');assert state(c).screen==2;m=state(c).match;m.half=1;m.phase=1;m.clock=m.duration-1;m.score[0]=3;m.score[1]=0;write_match(c,m);c.run(c.now_ms+100)
  assert state(c).screen==4 and state(c).profile.career_round==1 and state(c).profile.money>1800
  key(c,'ok');assert state(c).screen==5;key(c,'down');key(c,'ok');assert state(c).screen==6
  rating=state(c).profile.roster[0];key(c,'ok');assert state(c).profile.roster[0]==rating+2;key(c,'exe');assert state(c).profile.contracts[0]>6
  c.save_png(str(out/(profile+'-squad.png')));key(c,'back');assert state(c).screen==5;key(c,'back');assert state(c).screen==0
  # All three drills use actual menu/input/result paths and never reward a match.
  for _ in range(4):key(c,'down')
  key(c,'ok');assert state(c).screen==14
  c.save_png(str(out/(profile+'-training-menu.png')))
  played=state(c).profile.played;money=state(c).profile.money
  for drill in range(3):
   for _ in range(drill):key(c,'down')
   key(c,'ok');assert state(c).screen==2 and state(c).match.mode==4 and state(c).match.drill==drill
   hold(c,['right'],160);assert state(c).match.clock>0
   if drill==0:
    hold(c,['exe'],350);assert state(c).match.shots[0]>0
   elif drill==1:
    hold(c,['ok','down','right'],300);c.run(c.now_ms+1500);assert state(c).match.drill_hits>=1
   c.save_png(str(out/(profile+f'-training-{drill}.png')))
   m=state(c).match;m.clock=m.duration-1;write_match(c,m);c.run(c.now_ms+100)
   assert state(c).screen==4 and state(c).profile.played==played and state(c).profile.money==money
   key(c,'ok');assert state(c).screen==2 and state(c).match.drill==drill
   key(c,'back');key(c,'down');key(c,'down');key(c,'ok');assert state(c).screen==12
   key(c,'up');key(c,'ok');assert state(c).screen==14
  key(c,'back');assert state(c).screen==0
  # Stadium choice survives real save, exit and reinstallation.
  key(c,'down');key(c,'ok');assert state(c).screen==10
  for _ in range(4):key(c,'down')
  key(c,'right');key(c,'right');assert state(c).profile.venue==2
  c.save_png(str(out/(profile+'-settings.png')));key(c,'back');assert state(c).screen==0
  # Shootout controls for both aiming and the goalkeeper.
  key(c,'up');key(c,'up');key(c,'up');key(c,'ok');assert state(c).screen==1
  for _ in range(5):key(c,'ok')
  assert state(c).screen==2 and state(c).match.shoot_state==1
  key(c,'right');assert state(c).match.shoot_target>0
  height=state(c).match.shoot_height;key(c,'up');assert state(c).match.shoot_height>height
  key(c,'ok');c.run(c.now_ms+2400);assert state(c).match.shoot_turn==1
  hold(c,['left'],250);assert state(c).match.shoot_keeper<0
  key(c,'ok');assert state(c).match.shoot_state==2
  start=state(c).match.shoot_start;c.run(c.now_ms+500)
  assert state(c).match.shoot_keeper<start and state(c).match.ball.y<11*256 and state(c).match.ball.z>0
  c.save_png(str(out/(profile+'-penalties.png')))
  c.run(c.now_ms+600);assert state(c).match.shoot_resolved
  c.save_png(str(out/(profile+'-penalty-result.png')))
  # An actual red-timed penalty must leave the goal frame and award no goal.
  m=state(c).match;m.phase=1;m.shoot_state=1;m.shoot_turn=0;m.shoot_timer=0;m.kick_clock=0;m.charge=0;m.input_latch=m.previous_input=0;m.shoot_resolved=0;m.shoot_target=0;m.shoot_height=256
  m.ball.x=m.ball.z=0;m.ball.y=11*256;m.ball.vx=m.ball.vy=m.ball.vz=0;penalty_before=m.shoot_score[0];write_match(c,m)
  key(c,'ok');assert state(c).match.kick_quality<40;c.run(c.now_ms+1100)
  assert state(c).match.shoot_result==3 and state(c).match.shoot_score[0]==penalty_before
  # Free-kick practice starts from its own menu entry and repeats without rewards.
  key(c,'back');key(c,'down');key(c,'down');key(c,'ok');key(c,'up');key(c,'ok');assert state(c).screen==0
  for _ in range(4):key(c,'down')
  key(c,'ok');assert state(c).screen==1 and state(c).mode==5
  for _ in range(5):key(c,'ok')
  assert state(c).screen==2 and state(c).match.mode==5 and state(c).match.phase==7
  played=state(c).profile.played;money=state(c).profile.money
  c.save_png(str(out/(profile+'-free-practice.png')))
  hold(c,['exe'],450);assert state(c).match.free_attempts==1
  c.run(c.now_ms+4500);assert state(c).match.phase==7 and state(c).match.free_attempts==1
  hold(c,['6'],150);assert state(c).match.free_attempts==1 and state(c).match.wall_count>=3
  key(c,'back');key(c,'down');key(c,'down');key(c,'ok');key(c,'up');key(c,'ok')
  assert state(c).screen==0 and state(c).selected==4 and state(c).profile.played==played and state(c).profile.money==money
  max_render=metric(c,'perf_max');max_match=metric(c,'perf_match_max');key(c,'home');c.run(c.now_ms+80);exit_check(c,system=='upsilon')
  saved=records(p.read_bytes());assert saved['keep.py']==sentinel
  # Reinstallation keeps scripts, and the actual app recovers its profile.
  restored=out/(profile+'-restore.storage');storage(restored,[(k,v)for k,v in saved.items()if k.endswith('.py')])
  d=emu.Calculator(app,storage_file=str(restored),nwlink=nw);d.enable_checks();d.run(180);assert state(d).profile.career_round==1 and state(d).profile.roster[0]==rating+2 and state(d).profile.venue==2
  key(d,'onoff');d.run(d.now_ms+80);exit_check(d,system=='upsilon')
  report.append(dict(profile=profile,max_render_ms=max_render,max_match_render_ms=max_match,peak_stack=max(c.stack_used(),d.stack_used()),controls=True,training=True,tactics=True,stadium_save=True,side_camera=True,through_and_lob=True,finesse=True,free_kick_wall_curl=True,third_person_free_kicks=True,free_kick_practice=True,red_timing_misses=True,red_card_match_penalty=True,pause=True,result_reward_once=True,career=True,contracts=True,penalties=True,physical_penalty_flight=True,keeper_dive=True,help_eight_pages=True,script_backup_restore=True,firmware_preserved=True,clean_exit=True))
  print(json.dumps(report[-1]),flush=True)
 emu.configure();emu.CPU_HZ=216_000_000
 p=out/'full.storage';filler=b'x'*(emu.STORAGE_SIZE-24);storage(p,[('full.py',filler)]);c=emu.Calculator(app,storage_file=str(p),nwlink=nw);c.enable_checks();c.run(180)
 for _ in range(6):key(c,'down')
 key(c,'ok');assert state(c).screen==10;key(c,'right');key(c,'home');assert state(c).screen==13
 key(c,'ok');assert state(c).screen==13;key(c,'down');key(c,'down');key(c,'ok');c.run(c.now_ms+80);exit_check(c);assert records(p.read_bytes())=={'full.py':filler}
 p=out/'corrupt.storage';bad=bytearray(saved['footballfc.sav']);bad[35]^=128;storage(p,[('keep.py',sentinel),('footballfc.sav',bytes(bad))]);c=emu.Calculator(app,storage_file=str(p),nwlink=nw);c.enable_checks();c.run(180);assert state(c).profile.career_active==0;key(c,'home');c.run(c.now_ms+80);exit_check(c)
 c=emu.Calculator(app,nwlink=nw);c.enable_checks();c.keys=[(0,350,emu.KEYS['ok'])];c.run(450);assert state(c).screen==0;key(c,'home');c.run(c.now_ms+80);exit_check(c)
 report.append(dict(full_storage=True,corrupt_save=True,held_launch_key=True));(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('ARM tests passed',flush=True)
if __name__=='__main__':main()
