"""Reproducible ROM packer. All inputs are checked in; no network required."""
from pathlib import Path
import json, zlib, struct, math, re, hashlib
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
A=ROOT/'assets';OUT=ROOT/'src'
def number(x):
    x=float(x)
    return f'{x:.8g}'+('f'if '.'in f'{x:.8g}'or'e'in f'{x:.8g}'else '.0f')
def array(name,data,typ='uint8_t'):
    return f'const {typ} {name}[]={{'+','.join(str(x)for x in data)+'};\n'
def lzss(data):
    data=bytes(data)
    out=bytearray();pos=0;history={}
    while pos<len(data):
        flagpos=len(out);out.append(0);flags=0
        for bit in range(8):
            if pos>=len(data):break
            key=data[pos:pos+3];best=0;distance=0
            for old in reversed(history.get(key,[])[-40:]):
                dist=pos-old
                if dist>4096:break
                n=3
                while n<18 and pos+n<len(data)and data[old+n]==data[pos+n]:n+=1
                if n>best:best=n;distance=dist
            if best>=3:
                v=((distance-1)<<4)|(best-3);out.extend(struct.pack('<H',v));n=best
            else:flags|=1<<bit;out.append(data[pos]);n=1
            for j in range(n):
                key=data[pos+j:pos+j+3];bucket=history.setdefault(key,[]);bucket.append(pos+j)
                if len(bucket)>80:del bucket[:40]
            pos+=n
        out[flagpos]=flags
    return out

def main():
    model=json.loads(zlib.decompress((A/'source.json.zlib').read_bytes()));atlas=Image.open(A/'atlas.png').convert('RGBA');meta=json.loads((A/'atlas.json').read_text());mapping=json.loads((A/'mapping.json').read_text())
    names=sorted(meta);sprites={k:i for i,k in enumerate(names)};pixels=bytearray();sprite_rows=[]
    for name in names:
        v=meta[name];im=atlas.crop((v['x'],v['y'],v['x']+v['w'],v['y']+v['h']));mask=im.getchannel('A');rgb=im.convert('RGB');q=rgb.quantize(colors=15,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
        pal=q.getpalette();pal += [0]*(45-len(pal));palette=[0]+[((pal[i*3]>>3)<<11)|((pal[i*3+1]>>2)<<5)|(pal[i*3+2]>>3)for i in range(15)]
        values=[p+1 if a>=96 else 0 for p,a in zip(q.getdata(),mask.getdata())];start=len(pixels)
        for j in range(0,len(values),2):pixels.append(values[j]|((values[j+1]if j+1<len(values)else 0)<<4))
        sprite_rows.append('{'+','.join(map(str,[start,v['w'],v['h'],v['ow'],v['oh'],round(v['px']*16),round(v['py']*16)]))+',{'+','.join(map(str,palette))+'}}')
    used=sorted(mapping['objects']);types={k:i for i,k in enumerate(used)}
    # Missing artwork is handled explicitly, without substituting an unrelated bird.
    for name in ['BIRD_ME','SLINGSHOT']:
        if name not in types:types[name]=len(used);used.append(name)
    shape_names=sorted(set(model['objects'][k]['shape']for k in used));shape_ids={n:i for i,n in enumerate(shape_names)};shape_rows=[]
    for name in shape_names:
        s=model['shapes'][name];ox=s.get('offsetX',0)/20;oy=s.get('offsetY',0)/20
        if s['type']=='CIRCLE':
            r=s['radius']/20;verts=[];cx=ox;cy=oy;mass=math.pi*r*r;inertia=r*r*.5;kind=0
        else:
            if s['type']=='RECTANGLE':
                w=s['width']/40;h=s['height']/40;verts=[[-w,-h],[w,-h],[w,h],[-w,h]]
            else:verts=[[x/20,y/20]for x,y in s['vertices']]
            verts=[[x+ox,y+oy]for x,y in verts];area=sum(verts[i][0]*verts[(i+1)%len(verts)][1]-verts[(i+1)%len(verts)][0]*verts[i][1]for i in range(len(verts)))
            if area<0:verts.reverse();area=-area
            cx=cy=0;moment=0
            for i,(x,y)in enumerate(verts):
                u,v=verts[(i+1)%len(verts)];cross=x*v-u*y;cx+=(x+u)*cross;cy+=(y+v)*cross;moment+=cross*(x*x+x*u+u*u+y*y+y*v+v*v)
            cx/=3*area;cy/=3*area;mass=area*.5;inertia=max(.001,moment/(6*area)-cx*cx-cy*cy);verts=[[x-cx,y-cy]for x,y in verts];r=max(math.hypot(x,y)for x,y in verts);kind=1
        assert len(verts)<=8
        flat=[z for p in verts for z in p]+[0]*(16-len(verts)*2)
        shape_rows.append('{'+f'{kind},{len(verts)},'+','.join(number(v)for v in [r,cx,cy,mass,inertia])+',{'+','.join(number(v)for v in flat)+'}}')
    mats=sorted(set(model['objects'][k]['material']for k in used));mat_ids={k:i for i,k in enumerate(mats)};mat_rows=[]
    for name in mats:
        m=model['materials'][name];mat_rows.append('{'+','.join(number(m.get(k,0))for k in ['density','friction','restitution','strength','defense'])+'}')
    damage=[];velocity=[]
    for name in mats:
        m=model['materials'][name]
        for other in mats:
            damage.append(round(m.get('damageMultipliers',{}).get(other,1)*16));velocity.append(round(m.get('velocityMultipliers',{}).get(other,1)*16))
    type_rows=[]
    for name in used:
        o=model['objects'][name];m=model['materials'][o['material']];kind=1 if o['type']=='BIRD'else 2 if o['type']=='PIG'else 5 if o['type']=='BLOCK_FRONT'else 4 if m['bodyType']=='STATIC'else 3
        if ('TNT'in name or 'BOMB'in name or 'EXPLOSIVE'in name)and o['type']!='BIRD':kind=6
        spec=mapping['objects'].get(name,[]);ss=[sprites[n]for n in spec][:8];ss += [65535]*(8-len(ss))
        type_rows.append('{'+f'{shape_ids[o["shape"]]},{mat_ids[o["material"]]},{kind},{len(spec)},{int(m["bodyType"]=="STATIC")},'+number(o.get('maxHealth',m['strength']))+f',{o["score"]},'+'{'+','.join(map(str,ss))+'}}')
    levels_data=bytearray();level_rows=[];maximum=0;jmaximum=0;proof=[]
    for d in model['levels']:
        objs=[(k,v)for k,v in d['world'].items()if 'id'in v];objs.sort(key=lambda kv:(not kv[0].startswith('bird'),int(re.search(r'\d+',kv[0]).group())if re.search(r'\d+',kv[0])else 0))
        index={k:i for i,(k,v)in enumerate(objs)};birds=sum(k.startswith('bird')for k,v in objs);joints=[v for v in d['world'].values()if'id'not in v];raw=bytearray(struct.pack('<HHB3x',len(objs),len(joints),birds));first=objs[0][1];bounds=[first['x']-20,first['y']-50,first['x']+80,4.]
        for key,v in objs:
            raw.extend(struct.pack('<H2xfff',types[v['id']],v['x'],v['y'],v['angle']))
            bounds[0]=min(bounds[0],v['x']-4);bounds[1]=min(bounds[1],v['y']-4);bounds[2]=max(bounds[2],v['x']+4)
        for j in joints:
            # The web runtime adds these zero-based block indexes after ground/sling.
            a=index.get('block_'+str(j['index1']+1),65535);b=index.get('block_'+str(j['index2']+1),65535)
            flags=int(j.get('collideConnected',False))|int(j.get('motor',False))<<1|int(j.get('limit',False))<<2|int(j.get('backAndForth',False))<<3
            raw.extend(struct.pack('<HHBB2x8fH2x',a,b,j['type'],flags,*[j.get(k,0)for k in ['x1','y1','x2','y2','motorSpeed','maxTorque','lowerLimit','upperLimit']],min(65535,round(j.get('destroyTimer',0)))))
        while len(raw)%4:raw.append(0)
        planar=b''.join(raw[i::4]for i in range(4))
        packed=zlib.compress(planar,9,wbits=-15);start=len(levels_data);levels_data.extend(packed);maximum=max(maximum,len(objs));jmaximum=max(jmaximum,len(joints))
        theme=mapping['themes'].index(d.get('theme','BACKGROUND_BLUE_GRASS'));gold=int(d.get('scoreGold',d.get('maxScore',50000)*.8));silver=int(d.get('scoreSilver',gold*.7))
        level_rows.append('{'+f'{start},{len(packed)},{len(raw)},{gold},{silver},{int(d.get("scoreEagle",0))},{theme},'+','.join(number(x)for x in bounds)+',"'+d['key']+'"}')
        proof.append(dict(key=d['key'],objects=len(objs),joints=len(joints),birds=birds,raw_sha256=hashlib.sha256(raw).hexdigest()))
    groups=['{'+f'{g["first"]},{g["count"]},{sprites.get(g["image"],0)},"{g["name"]}"'+'}'for g in model['groups']]
    ids='\n'.join(f'#define SPR_{re.sub("[^A-Z0-9_]","_",n.upper())} {i}'for n,i in sprites.items())+'\n'
    ids+='\n'.join(f'#define OBJ_{n} {i}'for n,i in types.items())+'\n'
    ids+=f'#define LEVEL_COUNT {len(level_rows)}\n#define GROUP_COUNT {len(groups)}\n#define TYPE_COUNT {len(used)}\n#define MAT_COUNT {len(mats)}\n#define SPRITE_COUNT {len(names)}\n#define MAX_ORIGINAL_OBJECTS {maximum}\n#define MAX_ORIGINAL_JOINTS {jmaximum}\n'
    (OUT/'ids.h').write_text(ids,newline='\n')
    output='#include "data.h"\n'+array('art_pixels',pixels)+array('level_bytes',levels_data)
    for typ,name,rows in [('Sprite','sprites',sprite_rows),('Shape','shapes',shape_rows),('Material','materials',mat_rows),('ObjectDef','object_defs',type_rows),('Level','levels',level_rows),('Group','groups',groups)]:output+=f'const {typ} {name}[]={{'+',\n'.join(rows)+'};\n'
    output+=array('damage_matrix',damage)+array('velocity_matrix',velocity)+array('background_ids',[sprites[n]for n in mapping['backgrounds']],'uint16_t')+array('font_ids',[sprites.get('FONT_'+str(c),65535)for c in range(32,127)],'uint16_t')
    for table,prefix in [('story_intro','intro_'),('story_end','')]:output+=array(table,[sprites.get(model.get('triggers',{}).get(prefix+d['key'],''),65535)for d in model['levels']],'uint16_t')
    (OUT/'data.c').write_text(output,newline='\n');(ROOT/'output').mkdir(exist_ok=True)
    (ROOT/'output/asset-report.json').write_text(json.dumps(dict(levels=len(proof),max_objects=maximum,max_joints=jmaximum,sprites=len(names),art_bytes=len(pixels),level_bytes=len(levels_data),levels_proof=proof),indent=2)+'\n')
    print('Packed',len(proof),'levels;',len(levels_data),'level bytes;',len(pixels),'art bytes;',maximum,'objects,',jmaximum,'joints maximum')
if __name__=='__main__':main()
