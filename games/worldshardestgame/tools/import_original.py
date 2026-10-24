"""Bake the original 30 Flash stages and animation loops into flash-only tables.

Input: JPEXS original.xml, exported scripts/text and board-only 550x400 PNGs.
See docs/reference.md. This authoring tool is never run on the calculator.
"""
from pathlib import Path
from collections import Counter
import argparse, copy, hashlib, json, math, re
import xml.etree.ElementTree as E
from PIL import Image
import numpy as np

ROOT=Path(__file__).resolve().parents[1]

def frames(tags):
    result=[]; scene={}; frame=0
    for t in tags:
        typ=t.get('type','')
        if typ.startswith('PlaceObject'):
            d=int(t.get('depth')); n=scene.get(d,{}).copy()
            if t.get('placeFlagHasCharacter')=='true' or typ=='PlaceObjectTag':
                n=dict(id=int(t.get('characterId')),birth=frame,matrix=(1,0,0,1,0,0),name='')
            if t.get('name') is not None:n['name']=t.get('name')
            m=t.find('matrix')
            if m is not None:
                n['matrix']=tuple(float(m.get(k,v)) for k,v in [('scaleX','1'),('rotateSkew0','0'),('rotateSkew1','0'),('scaleY','1'),('translateX','0'),('translateY','0')])
            scene[d]=n
        elif typ.startswith('RemoveObject'):scene.pop(int(t.get('depth')),None)
        elif typ=='ShowFrameTag':result.append(scene.copy());frame+=1
    return result

def mul(a,b):
    return (a[0]*b[0]+a[2]*b[1],a[1]*b[0]+a[3]*b[1],a[0]*b[2]+a[2]*b[3],a[1]*b[2]+a[3]*b[3],a[0]*b[4]+a[2]*b[5]+a[4],a[1]*b[4]+a[3]*b[5]+a[5])

def point(m,x,y):return (m[0]*x+m[2]*y+m[4],m[1]*x+m[3]*y+m[5])

class Movie:
    def __init__(self,path):
        self.xml=E.parse(path).getroot();self.defs={};self.cache={}
        for x in self.xml.find('tags'):
            for key in ['spriteId','shapeId','characterID']:
                if x.get(key):self.defs[int(x.get(key))]=x;break
        self.stage=frames(self.xml.find('tags'))
    def timeline(self,i):
        if i not in self.cache:self.cache[i]=frames(self.defs[i].find('subTags'))
        return self.cache[i]
    def walk(self,i,t=0,m=(1,0,0,1,0,0),path=(),period=1):
        e=self.defs[i]
        if e.get('type')=='DefineSpriteTag':
            fs=self.timeline(i);period=math.lcm(period,len(fs))
            for d,n in sorted(fs[t%len(fs)].items()):
                yield from self.walk(n['id'],t-n['birth'],mul(m,n['matrix']),path+(d,),period)
        else:yield dict(id=i,path=path,period=period,m=m,element=e)
    def rect(self,n):
        boxes=[]
        for x in self.walk(n['id'],m=n['matrix']):
            b=x['element'].find('shapeBounds');assert b is not None
            v=[point(x['m'],int(b.get('X'+a)),int(b.get('Y'+c))) for a,c in [('min','min'),('max','min'),('max','max'),('min','max')]]
            boxes.append((min(p[0] for p in v),min(p[1] for p in v),max(p[0] for p in v),max(p[1] for p in v)))
        return [round(min(b[0]for b in boxes)),round(min(b[1]for b in boxes)),round(max(b[2]for b in boxes)),round(max(b[3]for b in boxes))]
    def walls(self,n):
        edges=[]
        for leaf in self.walk(n['id'],m=n['matrix']):
            records=leaf['element'].find('shapes/shapeRecords'); x=y=0; f0=f1=0
            assert records is not None
            def add(a,b):
                a=tuple(round(v)for v in point(leaf['m'],*a));b=tuple(round(v)for v in point(leaf['m'],*b))
                if a[1]!=b[1] and (f0 or f1):edges.append([*a,*b,f0,f1])
            for e in records:
                typ=e.get('type')
                if typ=='StyleChangeRecord':
                    if e.get('stateMoveTo')=='true':x=int(e.get('moveDeltaX'));y=int(e.get('moveDeltaY'))
                    if e.get('stateFillStyle0')=='true':f0=int(e.get('fillStyle0'))
                    if e.get('stateFillStyle1')=='true':f1=int(e.get('fillStyle1'))
                elif typ=='StraightEdgeRecord':
                    x1=x+int(e.get('deltaX','0'));y1=y+int(e.get('deltaY','0'));add((x,y),(x1,y1));x,y=x1,y1
                elif typ=='CurvedEdgeRecord':
                    cx=x+int(e.get('controlDeltaX'));cy=y+int(e.get('controlDeltaY'))
                    ex=cx+int(e.get('anchorDeltaX'));ey=cy+int(e.get('anchorDeltaY'));previous=(x,y)
                    for j in range(1,9):
                        u=j/8;v=1-u;q=(v*v*x+2*u*v*cx+u*u*ex,v*v*y+2*u*v*cy+u*u*ey)
                        add(previous,q);previous=q
                    x,y=ex,ey
                else:assert typ=='EndShapeRecord',typ
        return edges

def simplify(points):
    """Time-linear interpolation, with <= 1 original twip error per coordinate."""
    keep={0,len(points)-1}
    def part(a,b):
        if b<=a+1:return
        worst=-1;which=a
        for i in range(a+1,b):
            err=max(abs(points[i][k]-(points[a][k]+(points[b][k]-points[a][k])*(i-a)/(b-a))) for k in [0,1])
            if err>worst:worst=err;which=i
        if worst>1.000001:keep.add(which);part(a,which);part(which,b)
    part(0,len(points)-1)
    return [(points[i][0],points[i][1],i) for i in sorted(keep)]

def orientation(p,code):
    x,y=p
    if code&4:x,y=y,x
    return (-x if code&1 else x,-y if code&2 else y)

def carray(name,typ,rows):
    return 'const '+typ+' '+name+'[] = {\n'+''.join(' {'+','.join(str(v)for v in r)+'},\n' for r in rows)+'};\n'

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--reference',type=Path,required=True);a=ap.parse_args();ref=a.reference
    movie=Movie(ref/'original.xml');(ROOT/'src').mkdir(parents=True,exist_ok=True);(ROOT/'tests').mkdir(exist_ok=True);(ROOT/'docs').mkdir(exist_ok=True)
    boards=[Image.open(ref/'boards'/f'{61+4*l}.png').convert('RGB').resize((320,233),Image.Resampling.BOX) for l in range(30)]
    contact=Image.new('RGB',(320*5,233*6))
    for i,im in enumerate(boards):contact.paste(im,(i%5*320,i//5*233))
    palette=contact.quantize(colors=32,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    cols=palette.getpalette()[:96];pal=[((cols[i]>>3)<<11)|((cols[i+1]>>2)<<5)|(cols[i+2]>>3) for i in range(0,96,3)]
    row_ids=[];row_offsets=[];row_data=[];rowmap={}
    for im in boards:
        ix=np.array(im.quantize(palette=palette,dither=Image.Dither.NONE))
        for row in ix:
            key=bytes(row)
            if key not in rowmap:
                rowmap[key]=len(row_offsets);row_offsets.append(len(row_data));start=0
                while start<320:
                    end=start+1
                    while end<320 and row[end]==row[start] and end-start<255:end+=1
                    row_data.extend([end-start,int(row[start])]);start=end
            row_ids.append(rowmap[key])
    contact.resize((800,699),Image.Resampling.NEAREST).save(ROOT/'docs/level-boards.png')
    tracks=[];trackmap={};enemies=[];walls=[];coins=[];checks=[];levels=[];oracle=[];wall_oracle=[]
    expected=[0,1,1,3,0,4,4,3,1,0,2,1,0,0,0,4,0,67,0,7,3,0,36,0,0,4,1,0,2,4]
    textids=[]
    # The original announcement sprite selects the frame matching currentLevel.
    for fs in movie.timeline(174):
        textids.append(next((n['id'] for n in fs.values() if (ref/'text'/f"{n['id']}.txt").exists()),None))
    for level in range(30):
        frame=61+level*4;scene=movie.stage[frame-1];named={n['name']:n for n in scene.values() if n['name']};node=named['enemies']
        leaves=list(movie.walk(node['id'],m=node['matrix']));maxperiod=max(x['period'] for x in leaves)
        samples={t:{x['path']:x for x in movie.walk(node['id'],t,node['matrix'])} for t in range(maxperiod)}
        eoff=len(enemies)
        for index,leaf in enumerate(leaves):
            period=leaf['period'];seq=[]
            for t in range(period):
                x=samples[t][leaf['path']];m=x['m']
                assert abs(math.hypot(m[0],m[1])-1)<0.002 and abs(math.hypot(m[2],m[3])-1)<0.002
                seq.append((round(m[4]),round(m[5])))
            if len(set(seq))==1:period=1;seq=seq[:1]
            origin=seq[0];relative=[(x-origin[0],y-origin[1])for x,y in seq]
            options=[tuple(orientation(p,k)for p in relative)for k in range(8)];code=min(range(8),key=lambda k:options[k]);canonical=options[code]
            # inverse of swap-then-sign: swap the sign bits when undoing a swap.
            inverse=((code&1)<<1)|((code&2)>>1)|4 if code&4 else code
            key=(period,canonical)
            if key not in trackmap:trackmap[key]=len(tracks);tracks.append(dict(period=period,points=simplify(list(canonical)+[canonical[0]])))
            colors=[x.attrib for x in leaf['element'].findall('.//color')];color=next(x for x in colors if any(int(x.get(k,0))for k in ['red','green','blue']))
            rgb=[int(color[k])for k in ['red','green','blue']];kind=0 if rgb==[0,0,255] else 1 if rgb==[100,43,187] else 2
            enemies.append([*origin,trackmap[key],inverse,kind])
            for t in sorted(set([0,1,7,17,33,period//2,period-1,period+11])):
                orig=next(x for x in movie.walk(node['id'],t,node['matrix']) if x['path']==leaf['path'])
                oracle.append([level,t,index,round(orig['m'][4]),round(orig['m'][5])])
        woff=len(walls);walls.extend(movie.walls(named['walls']))
        mask=Image.open(ref/'walls'/f'{frame}.png').convert('RGB')
        solid={(0,0,0),(180,181,254),(210,188,245),(236,113,113)}
        for py in range(12,400,25):
            for px in range(12,550,25):
                c=mask.getpixel((px,py))
                if c in solid or c==(255,255,255):wall_oracle.append([level,px*20+10,py*20+10,int(c in solid)])
        coff=len(coins)
        for n in scene.values():
            if n['id']==183:
                x,y=map(round,n['matrix'][4:]);wind=[0]*8
                for x0,y0,x1,y1,f0,f1 in walls[woff:]:
                    if min(y0,y1)<=y<max(y0,y1):
                        side=(x1-x0)*(y-y0)-(x-x0)*(y1-y0)
                        if (side>0)==(y1>y0):
                            sign=1 if y1>y0 else -1;wind[f0]+=sign;wind[f1]-=sign
                # Level 9 contains an unused coin hidden behind an opaque wall.
                if not any(wind[1:]):coins.append([x,y])
        assert len(coins)-coff==expected[level],(level,len(coins)-coff)
        qoff=len(checks);ns=sorted((int(name[5:]),n)for name,n in named.items()if name.startswith('check'))
        for _,n in ns:checks.append([*movie.rect(n),round(n['matrix'][4]),round(n['matrix'][5])])
        player_script=next((ref/'scripts/scripts'/f'frame_{frame}').glob('PlaceObject2_154_*/CLIPACTIONRECORD onClipEvent(enterFrame).as')).read_text()
        goal=int(re.search(r'if\(hitTest\(_root.check(\d)\) && _root.currentCoins',player_script)[1])-1
        # checkpoint 1 is the spawn; additional green regions advance in order.
        if level==0:tid=131
        elif level<19:tid=textids[level]
        elif level<29:tid=next(n['id']for n in movie.timeline(326)[level].values() if (ref/'text'/f"{n['id']}.txt").exists())
        else:tid=413
        text=(ref/'text'/f'{tid}.txt').read_text().replace('\n--- RECORDSEPARATOR ---\n',' ').replace('\n',' ')
        levels.append([woff,len(walls)-woff,eoff,len(enemies)-eoff,coff,len(coins)-coff,qoff,len(checks)-qoff,goal,text])
        print(f"Level {level+1}: {len(enemies)-eoff} enemies, {len(coins)-coff} coins, {len(walls)-woff} wall edges",flush=True)
    points=[];trackrows=[]
    for t in tracks:trackrows.append([len(points),len(t['points']),t['period']]);points.extend(t['points'])
    text='/* Derived from the original Flash version 1.0; see docs/reference.md. */\n#include "game.h"\n'
    text+=carray('whg_points','WhgPoint',points)+carray('whg_tracks','WhgTrack',trackrows)+carray('whg_enemies','WhgEnemy',enemies)+carray('whg_walls','WhgWall',walls)+carray('whg_coins','WhgCoin',coins)+carray('whg_checks','WhgCheck',checks)
    text+='const WhgLevel whg_levels[30] = {\n'+''.join(' {'+','.join(str(v)for v in l[:9])+','+json.dumps(l[9])+'},\n'for l in levels)+'};\n'
    tips=[]
    for i in range(30):
        tid={4:197,19:315,29:412}.get(i)
        if tid:
            s=(ref/'text'/f'{tid}.txt').read_text().replace('\n--- RECORDSEPARATOR ---\n',' ')
            s=re.sub(r'\s+',' ',s).removeprefix('TIP:').strip().rstrip('. ')+'.';tips.append(json.dumps(s))
        else:tips.append('0')
    text+='const char *const whg_tips[30] = {'+','.join(tips)+'};\n'
    for name,typ,values in [('whg_palette','uint16_t',pal),('whg_row_ids','uint16_t',row_ids),('whg_row_offsets','uint32_t',row_offsets),('whg_row_data','uint8_t',row_data)]:
        text+='const '+typ+' '+name+'[] = {\n'+''.join(' '+','.join(str(v)for v in values[i:i+24])+',\n'for i in range(0,len(values),24))+'};\n'
    (ROOT/'src/levels.c').write_text(text,encoding='utf-8',newline='\n')
    (ROOT/'tests/reference.inc').write_text('static const int16_t reference[][5] = {\n'+''.join(' {'+','.join(map(str,r))+'},\n'for r in oracle)+'};\n')
    (ROOT/'tests/wall-reference.inc').write_text('static const int16_t wall_reference[][4] = {\n'+''.join(' {'+','.join(map(str,r))+'},\n'for r in wall_oracle)+'};\n')
    # Title only: all interactive text is drawn with native fonts.
    source=Image.open(ref/'screens/27.png').convert('RGB');im=source.crop((19,27,533,160)).convert('RGBA')
    rgb=np.array(im);bg=np.array(source) [27:160,548:549,:];alpha=(np.max(np.abs(rgb[:,:,:3].astype('int16')-bg.astype('int16')),axis=2)>2)*255
    rgb[:,:,3]=alpha;im=Image.fromarray(rgb).resize((300,78),Image.Resampling.LANCZOS)
    im.save(ROOT/'docs/original-title.png');q=im.convert('RGB').quantize(colors=31,dither=Image.Dither.NONE);p=q.getpalette()[:93]
    cp=[0]+[((p[i]>>3)<<11)|((p[i+1]>>2)<<5)|(p[i+2]>>3)for i in range(0,93,3)]
    raw=[v+1 if alpha>=128 else 0 for v,alpha in zip(q.tobytes(),im.getchannel('A').tobytes())];rle=[];at=0
    while at<len(raw):
        end=at+1
        while end<len(raw) and raw[end]==raw[at] and end-at<255:end+=1
        rle.extend([end-at,raw[at]]);at=end
    art='/* Original title, rasterised once for the 320-pixel screen. */\n#include <stdint.h>\n'
    for name,typ,v in [('whg_title_palette','uint16_t',cp),('whg_title_data','uint8_t',rle)]:
        art+='const '+typ+' '+name+'[] = {\n'+''.join(' '+','.join(map(str,v[i:i+24]))+',\n'for i in range(0,len(v),24))+'};\n'
    art+=f'const unsigned whg_title_size={len(rle)};\n';(ROOT/'src/title.c').write_text(art)
    summary=dict(source_sha256=hashlib.sha256((ref/'original.swf').read_bytes()).hexdigest(),levels=30,enemies=[x[3]for x in levels],coins=expected,tracks=len(tracks),points=len(points),wall_edges=len(walls),unique_board_rows=len(row_offsets),board_data_bytes=len(row_data)+len(row_ids)*2+len(row_offsets)*4,oracle_samples=len(oracle),wall_oracle_samples=len(wall_oracle),maximum_path_error_twips=1)
    (ROOT/'docs/import.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))

if __name__=='__main__':main()
