"""Execute the actual ARM app, including firmware memory and clean-exit checks."""
import argparse,io,json,os,re,shlex,struct,tempfile,types
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
emu_path=REPO/'tools/emu.py';emu=types.ModuleType('angry_emu');emu.__file__=str(emu_path);emu.io=io
source=emu_path.read_text().replace('ELFFile(open(elf, "rb"))','ELFFile(io.BytesIO(open(elf, "rb").read()))')
source=source.replace('cmd = [nwlink, "nwa-elf",','cmd = nwlink + ["nwa-elf",')
source=source.replace('chunk = CPU_HZ // 1000','chunk = CPU_HZ // 10000').replace('self.now_ms += 1\n','self.now_ms += 0.1\n')
source=source.replace('        next_frame = self.now_ms if frame_ms else None','        self.checkpoint = until_ms\n        next_frame = self.now_ms if frame_ms else None')
source=source.replace('            self.slept_ms += r0\n','            self.slept_ms += r0\n            if self.now_ms >= self.checkpoint: uc.emu_stop()\n')
exec(compile(source,str(emu_path),'exec'),emu.__dict__)
def presses(*items):return[(t,t+d,emu.KEYS[k])for t,k,d in items]
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
def exited(c,upsilon=False):
    assert c.exited,'App did not exit';assert not c.violations,sorted(set(c.violations));assert not c.firmware_ram_changes();assert c.app_ram_clean();assert c.locks==0 and c.max_locks==(0 if upsilon else 1)

def selections(app,nw,out):
    results=[]
    for profile,model,ram in [('n0120','n0120',153676),('n0110','n0110',148928),('n0115','n0110',133652)]:
        emu.configure(model,'epsilon',ram);emu.CPU_HZ=216_000_000 if model=='n0120'else 86_400_000
        for episode in range(13):
            c=emu.Calculator(app,nwlink=nw);c.enable_checks();c.keys=presses((450,'ok',90));c.run(850)
            for i in range(episode):
                t=int(c.now_ms)+30;c.keys+=presses((t,'right',60));c.run(t+150)
            t=int(c.now_ms)+40;c.keys+=presses((t,'ok',90));c.run(t+400);assert uint(c,'current_view')==2
            t=int(c.now_ms)+40;c.keys+=presses((t,'ok',90));c.run(t+1000)
            for i in range(3):
                if uint(c,'current_view')==3:break
                assert uint(c,'current_view')in (6,9),(profile,episode,uint(c,'current_view'))
                t=int(c.now_ms)+40;c.keys+=presses((t,'ok',90));c.run(t+400)
            assert uint(c,'current_view')==3,(profile,episode,'stage did not open')
            # Retry the level through the real pause menu, then leave with Home.
            t=int(c.now_ms)+50;c.keys+=presses((t,'back',90),(t+350,'down',90),(t+650,'ok',90));c.run(t+1600);assert uint(c,'current_view')==3
            if profile=='n0120':c.save_png(str(out/('episode-'+str(episode+1)+'.png')))
            t=int(c.now_ms)+50;c.keys+=presses((t,'home',150));c.run(t+800);exited(c)
            results.append(dict(profile=profile,episode=episode+1,opened_and_retried=True,stack_bytes=c.stack_used()))
        for key in ('back','home'):
            st=out/(profile+'-loading-'+key+'.bin');storage(st,[('keep.py',b'keep'),('angrybirds.sav',(ROOT/'output/large-stage.sav').read_bytes())]);c=emu.Calculator(app,storage_file=str(st),nwlink=nw);c.enable_checks();c.keys=presses((450,'1',70));c.run(400)
            for i in range(100):
                c.run(c.now_ms+5)
                if uint(c,'current_view')==10:break
            else:raise AssertionError((profile,'loading view was not reached'))
            if profile=='n0120':c.save_png(str(out/'loading.png'))
            t=int(c.now_ms)+10;c.keys+=presses((t,key,120));c.run(t+400)
            if key=='back':
                assert uint(c,'current_view')==2,(profile,'loading cancel failed');t=int(c.now_ms)+20;c.keys+=presses((t,'home',120));c.run(t+700)
            exited(c);assert records(st.read_bytes())['keep.py']==b'keep';results.append(dict(profile=profile,loading_interrupt=key,result='pass'))
        print(profile+': all 13 episode selections and retries passed',flush=True)
    (out/'selection-report.json').write_text(json.dumps(results,indent=2)+'\n')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--nwlink',default='nwlink');ap.add_argument('--quick',action='store_true');ap.add_argument('--selection-only',action='store_true');a=ap.parse_args()
    nw=['node',a.nwlink]if Path(a.nwlink).is_file()and not a.nwlink.endswith(('.exe','.cmd'))else shlex.split(a.nwlink)
    out=ROOT/'output/arm';out.mkdir(parents=True,exist_ok=True);tempfile.tempdir=str(out);app=str(ROOT/'output/angrybirds-debug.nwa');report=[];sentinel=b'print(42)\n'
    if a.selection_only:selections(app,nw,out);return
    profiles=[('n0120','n0120','epsilon',153676),('n0110','n0110','epsilon',148928),('n0115','n0110','epsilon',133652),('upsilon','n0110','upsilon',107674)]
    for profile,model,system,ram in profiles[:1]if a.quick else profiles:
        emu.configure(model,system,ram);emu.CPU_HZ=216_000_000 if model=='n0120'else 86_400_000;st=out/(profile+'.bin');storage(st,[('keep.py',sentinel)])
        c=emu.Calculator(app,storage_file=str(st),nwlink=nw);c.enable_checks();c.keys=presses((450,'1',90),(1600,'ok',90),(3300,'back',90),(4100,'back',90),(5200,'home',120))
        c.run(300);c.save_png(str(out/(profile+'-title.png')));c.run(1300);c.save_png(str(out/(profile+'-aim.png')))
        assert uint(c,'current_view')==3;c.run(3700);assert uint(c,'current_view')==4;c.save_png(str(out/(profile+'-pause.png')))
        c.run(4800);c.save_png(str(out/(profile+'-flight.png')));r=dict(profile=profile,frames=uint(c,'perf_frames'),draw_max_ms=uint(c,'perf_max'),step_max_ms=uint(c,'perf_step_max'),draws_over_33ms=uint(c,'perf_slow'),stack_bytes=c.stack_used());c.run(6200);exited(c,system=='upsilon')
        saved=records(st.read_bytes());assert saved['keep.py']==sentinel and len(saved['angrybirds.sav'])==2336
        d=emu.Calculator(app,storage_file=str(st),nwlink=nw);d.enable_checks();d.keys=presses((450,'onoff',120));d.run(1100);exited(d,system=='upsilon');assert records(st.read_bytes())['keep.py']==sentinel
        report.append(r);print(r,flush=True)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    if a.quick:return
    emu.configure();st=out/'full.bin';filler=b'x'*(emu.STORAGE_SIZE-24);storage(st,[('full.py',filler)])
    c=emu.Calculator(app,storage_file=str(st),nwlink=nw);c.enable_checks();c.keys=presses((400,'home',90),(1000,'ok',90),(1400,'1',90),(1900,'home',90),(2400,'back',90));c.run(750);assert uint(c,'current_view')==8;c.run(1200);assert uint(c,'current_view')==8;c.run(1700);assert uint(c,'current_view')==0;c.run(3100);exited(c);assert records(st.read_bytes())['full.py']==filler
    c=emu.Calculator(app,nwlink=nw);c.enable_checks();c.keys=presses((0,'ok',1000),(1400,'home',100));c.run(600);assert uint(c,'current_view')==0;c.run(2000);exited(c)
    report.append(dict(full_storage_retry_menu_unsaved_exit='pass',launch_held_key='pass'))
    host=json.loads((ROOT/'output/host-report.json').read_text())
    def field(c,key,fmt):return struct.unpack(fmt,bytes(c.uc.mem_read(c.symbols['game']+host[key],struct.calcsize(fmt))))[0]
    # Navigate the real menus, help and original intro without injecting game state.
    c=emu.Calculator(app,nwlink=nw);c.enable_checks();c.keys=presses((450,'ok',90),(1100,'ok',90),(1650,'ok',90),(2300,'ok',90),(2900,'ok',90),(4000,'back',90),(4900,'home',120))
    c.run(800);assert uint(c,'current_view')==1;c.save_png(str(out/'episodes.png'))
    c.run(1400);assert uint(c,'current_view')==2;c.save_png(str(out/'stages.png'))
    c.run(2050);assert uint(c,'current_view')==6;c.save_png(str(out/'help.png'))
    c.run(2600);assert uint(c,'current_view')==9;c.save_png(str(out/'story.png'))
    c.run(3500);assert uint(c,'current_view')==3;c.save_png(str(out/'gameplay.png'));c.run(4400);assert uint(c,'current_view')==4;c.run(5600);exited(c)
    # A complete stage through calculator key events, including saved progression.
    st=out/'clear.bin';storage(st,[('keep.py',sentinel)]);c=emu.Calculator(app,storage_file=str(st),nwlink=nw);c.enable_checks();c.keys=presses((450,'1',90));c.run(1200)
    target=json.loads((ROOT/'output/stage1-solution.json').read_text())['angle']
    for attempt in range(100):
        angle=field(c,'angle','<f')*180/3.141592653589793
        if abs(angle-target)<.1:break
        t=int(c.now_ms)+30;c.keys+=presses((t,'down'if angle>target else'up',24));c.run(t+90)
    else:raise AssertionError('Aiming through real keys did not converge')
    t=int(c.now_ms)+50;c.keys+=presses((t,'ok',80));c.run(t+200)
    for i in range(40):
        c.run(c.now_ms+500)
        if uint(c,'current_view')==5:break
    assert uint(c,'current_view')==5 and field(c,'won','<B')==1,'Stage 1 did not clear'
    c.save_png(str(out/'stage-clear.png'));t=int(c.now_ms)+100;c.keys+=presses((t,'home',120));c.run(t+800);exited(c)
    saved=records(st.read_bytes());p=saved['angrybirds.sav'];assert saved['keep.py']==sentinel and struct.unpack_from('<H',p,2279)[0]>=2
    report.append(dict(real_keys_stage1_clear='pass',score=struct.unpack_from('<I',p,8)[0],stars=p[1780],angle=target))
    # A host-generated progression fixture opens the most populated original map.
    for profile,model,ram in [('n0120','n0120',153676),('n0110','n0110',148928)]:
        emu.configure(model,'epsilon',ram);emu.CPU_HZ=216_000_000 if model=='n0120'else 86_400_000
        st=out/(profile+'-large.bin');storage(st,[('keep.py',sentinel),('angrybirds.sav',(ROOT/'output/large-stage.sav').read_bytes())]);c=emu.Calculator(app,storage_file=str(st),nwlink=nw);c.enable_checks();c.keys=presses((450,'1',120),(1800,'ok',120),(3500,'ok',90),(6500,'home',180))
        c.run(6100);c.save_png(str(out/(profile+'-large.png')));assert field(c,'overflows','<H')==0
        r=dict(profile=profile,fixture='easterspecial-3',objects=350,draw_max_ms=uint(c,'perf_max'),step_max_ms=uint(c,'perf_step_max'),frames=uint(c,'perf_frames'),stack_bytes=c.stack_used());c.run(7800);exited(c);report.append(r);print(r,flush=True)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('ARM tests passed',flush=True)
if __name__=='__main__':main()
