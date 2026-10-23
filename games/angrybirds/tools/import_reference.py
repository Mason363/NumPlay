from pathlib import Path
import argparse,json,zlib,re,hashlib
from PIL import Image,ImageDraw
parser=argparse.ArgumentParser(description='Convert the pinned Angry Birds fowl/ reference directory into offline build inputs.')
parser.add_argument('--source',type=Path,required=True,help='Path to the extracted cors/fowl directory from commit 29006b0f11904704c390b457f7055c9fa1e53484')
args=parser.parse_args()
src=args.source.resolve();dest=Path(__file__).resolve().parents[1];assets=dest/'assets'
for sub in ['assets','src','tools','tests','docs','output']:(dest/sub).mkdir(parents=True,exist_ok=True)
def read(p):return json.loads((src/p).read_text())
objects=read('json/Objects.json');objects.update(read('json/ChromeObjects.json'))
materials=read('json/Materials.json');materials.update(read('json/ChromeMaterials.json'))
shapes=read('json/Shapes.json');shapes.update(read('json/ChromeShapes.json'))
episodes=read('json/Episodes.json')['episodes'];alllevels={p.stem[5:].lower():json.loads(p.read_text())for p in (src/'json').glob('Level*.json')if 'benchmark'not in p.name}
order=[6,7,8,9,10,11,0,1,2,3,4,5];levels=[];groups=[];used=set()
names={'1':'Poached Eggs','2':'Mighty Hoax','3':'Danger Above','christmas':"Season's Greedings",'chrome':'Chrome Dimension'}
for i in order:
    e=episodes[i];first=len(levels)
    for page,n in zip(e['pageIndexes'],e['levelsPerPage']):
        for j in range(1,n+1):
            key=f'{page}-{j}'.lower();d=alllevels[key];levels.append(dict(key=key,**d));used.add(key)
    groups.append(dict(name=names.get(e['name'],e['name']),first=first,count=len(levels)-first,image=e['menuImage']))
first=len(levels)
for k in sorted(alllevels.keys()-used):levels.append(dict(key=k,**alllevels[k]))
groups.append(dict(name='Bonus maps',first=first,count=len(levels)-first,image=episodes[11]['menuImage']))
limits=read('json/starLimits.json')
for d in levels:
    star=limits.get('Level'+d['key'],{})
    # Map keys in starLimits retain the source's mixed case.
    if not star:star=next((v for k,v in limits.items()if k.lower()=='level'+d['key']),{})
    for k in ['scoreGold','scoreSilver','scoreEagle']:
        if k in star:d[k]=star[k]
cutscenes=read('json/Cutscenes.json');triggers={}
for e in episodes:triggers.update({k.lower():v for k,v in e.get('cutscenes',{}).items()if v in cutscenes})
model=dict(commit='29006b0f11904704c390b457f7055c9fa1e53484',levels=levels,groups=groups,objects=objects,materials=materials,shapes=shapes,stories=cutscenes,triggers=triggers)
(assets/'source.json.zlib').write_bytes(zlib.compress(json.dumps(model,separators=(',',':')).encode(),9))

sprites={}
for p in sorted((src/'sprite_sheets').glob('*.json')):
    d=json.loads(p.read_text());im=Image.open(p.with_suffix('.png')).convert('RGBA')
    for v in d.values():
        if isinstance(v,dict)and 'id'in v:
            sprites.setdefault(v['id'],(im.crop((v['x'],v['y'],v['x']+v['width'],v['y']+v['height'])),v))
# Extract the original classic wordmark component, excluding the detached Beta
# badge. Do not pack the Chrome-branded title into the calculator's menu.
logo,info=sprites['MAINMENU_AB_GOOGLE_PLUS_LOGO_HD'];alpha=logo.getchannel('A');pixels=alpha.load()
remaining={(x,y)for y in range(logo.height)for x in range(logo.width)if pixels[x,y]};largest=[]
while remaining:
    seed=remaining.pop();stack=[seed];component=[seed]
    while stack:
        x,y=stack.pop()
        for adjacent in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
            if adjacent in remaining:remaining.remove(adjacent);stack.append(adjacent);component.append(adjacent)
    if len(component)>len(largest):largest=component
mask=Image.new('L',logo.size)
for x,y in largest:mask.putpixel((x,y),pixels[x,y])
logo=logo.copy();logo.putalpha(mask);logo=logo.crop(mask.getbbox())
sprites['CLASSIC_AB_LOGO']=(logo,dict(width=logo.width,height=logo.height,pivotx=logo.width/2,pivoty=0))
ids=set();objids=set(v['id']for d in levels for v in d['world'].values()if'id'in v)
missing=[];mapping={}
for name in sorted(objids|{'BIRD_SARDINE','BIRD_ME','MISC_PROP_WHITE_EGG','SLINGSHOT'}):
    variants=[]
    choices=[name,name+'_1',name+'_01']
    if name=='BIRD_ME':choices.append('BIRD_ME_MOTION')
    # Some props use their material's sprite name.
    mat=objects[name]['material'];choices += [mat,mat+'_1',mat+'_01']
    for k in choices:
        if k in sprites:variants=[k];break
    if variants:
        base=variants[0]
        prefix=base[:-1]if base.endswith('_1')else base[:-2]if base.endswith('_01')else None
        if prefix:
            for j in range(2,5):
                key=prefix+(f'{j:02}'if base.endswith('_01')else str(j))
                if key in sprites:variants.append(key)
        if name.startswith('BIRD_'):
            for suffix in ['FLYING','SPECIAL','BLINK','YELL']:
                key=name+'_'+suffix
                if key in sprites:variants.append(key)
        ids.update(variants);mapping[name]=variants
    else:missing.append(name)
for name in ['CLASSIC_AB_LOGO','MAINMENU_BUTTON_PLAY_NORMAL','MENU_PLAY_CF','P_STAR_1','LEVELOVERWIN_STARS_BIG_0','LEVELOVERWIN_STARS_BIG_1','LEVELOVERWIN_STARS_BIG_2','LEVELOVERWIN_STARS_BIG_3','LEVELOVERWIN_BUTTON_RESTART','LEVELOVERWIN_BUTTON_NEXTLEVEL','LEVELOVERWIN_BUTTON_MENU','LEVELPAUSE_BUTTON_RESUME','LEVELPLAY_BUTTON_PAUSE','SLINGSHOT_FRONT','SLINGSHOT_BACK']:
    if name in sprites:ids.add(name)
# Slingshot artwork lives in the misc sheet under a separate prefix.
for name in sprites:
    if 'SLING'in name or name in [g['image']for g in groups]:ids.add(name)
packed=[];thumbs=[]
for name in sorted(ids):
    im,v=sprites[name];scale=.25
    if name=='CLASSIC_AB_LOGO':scale=min(250/im.width,90/im.height)
    elif name in ['MAINMENU_BUTTON_PLAY_NORMAL','MENU_PLAY_CF']:scale=min(90/im.width,55/im.height)
    elif name.startswith('LEVELOVERWIN_STARS_BIG'):scale=min(140/im.width,50/im.height)
    elif name.startswith(('LEVELPAUSE','LEVELOVERWIN_BUTTON','LEVELPLAY')):scale=min(40/im.width,38/im.height)
    w=max(1,round(im.width*scale));h=max(1,round(im.height*scale));im=im.resize((w,h),Image.Resampling.LANCZOS)
    packed.append((name,im,dict(w=w,h=h,ow=v['width'],oh=v['height'],px=v.get('pivotx',v['width']/2)*scale,py=v.get('pivoty',v['height']/2)*scale)))
# UI text uses NumPlay's Nunito glyphs at their native size, not resized artwork.

# Reuse the original background artwork, with fixed display-size composites.
theme_prefix={'BLUE_GRASS':'CLASSIC_BLUE_GRASS','RED_FLOWERS':'CLASSIC_RED_FLOWERS','GREEN_PLANTS':'CLASSIC_GREEN_PLANTS','BUSHES':'CLASSIC_BUSHES','CACTUS_DESERT':'CLASSIC_CACTUS_DESERT','CITY':'CLASSIC_CITY','FOREST':'CLASSIC_FOREST','CLOUDS':'CLASSIC_CLOUDS','WESTERN':'CLASSIC_WESTERN','CHROME':'CHROME','XMAS':'SEASONS_XMAS','EASTER':'SEASONS_EASTER','MOONFESTIVAL':'SEASONS_MOONFESTIVAL','CHERRY_BLOSSOM':'SEASONS_CHERRY_BLOSSOM','SUMMERPIGNIC':'SEASONS_SUMMER_PIGNIC','HALLOWEEN_2011':'SEASONS_HAMOWEEN'}
themes=sorted(set(d.get('theme','BACKGROUND_BLUE_GRASS')for d in levels));backgrounds=[]
for theme in themes:
    key=theme.replace('BACKGROUND_BIG_SETUP_','').replace('BACKGROUND_','');prefix=theme_prefix[key]
    night=key in ['MOONFESTIVAL','HALLOWEEN_2011','FOREST'];bg=Image.new('RGBA',(160,120),(28,48,82,255)if night else(135,213,248,255))
    for j in range(5):
        p=src/f'images/BackgroundThemes/{prefix}_LAYER_{j}.png'
        if not p.exists():continue
        layer=Image.open(p).convert('RGBA');scale=160/478;w=max(1,round(layer.width*scale));h=max(1,round(layer.height*scale));layer=layer.resize((w,h),Image.Resampling.LANCZOS)
        y=105 if j==3 else 105-h
        if j==0:layer=layer.resize((160,105),Image.Resampling.LANCZOS);w,h=layer.size;y=0
        for x in range(-w,160,w):bg.alpha_composite(layer,(x,y))
    if not (src/f'images/BackgroundThemes/{prefix}_LAYER_3.png').exists():ImageDraw.Draw(bg).rectangle((0,105,159,119),fill=(98,76,42,255))
    backgrounds.append('BG_'+str(len(backgrounds)));packed.append((backgrounds[-1],bg,dict(w=160,h=120,ow=160,oh=120,px=0,py=0)))
menu=Image.open(src/'images/Menu/main_menu/chrome_mainmenu_bg.png').convert('RGBA').resize((320,240),Image.Resampling.LANCZOS)
fg=Image.open(src/'images/Menu/main_menu/chrome_mainmenu_fg.png').convert('RGBA');fg.thumbnail((320,110),Image.Resampling.LANCZOS);menu.alpha_composite(fg,(0,240-fg.height))
packed.append(('MENU_BG',menu,dict(w=320,h=240,ow=320,oh=240,px=0,py=0)))
for name,story in sorted(cutscenes.items()):
    im=Image.open(src/'images/cutscenes'/story['image']).convert('RGBA');im.thumbnail((320,132),Image.Resampling.LANCZOS)
    packed.append((name,im,dict(w=im.width,h=im.height,ow=im.width,oh=im.height,px=0,py=0)))
# A single checked-in RGBA atlas is the rebuild input; generated ROM uses indexed 4-bit pixels.
atlas=Image.new('RGBA',(1024,4096));x=y=row=0;meta={}
for name,im,v in packed:
    if x+im.width>1024:x=0;y+=row+1;row=0
    atlas.alpha_composite(im,(x,y));meta[name]=dict(x=x,y=y,**v);x+=im.width+1;row=max(row,im.height)
atlas=atlas.crop((0,0,1024,y+row));atlas.save(assets/'atlas.png',optimize=True)
(assets/'atlas.json').write_text(json.dumps(meta,separators=(',',':'))+'\n',encoding='utf-8',newline='\n')
(assets/'mapping.json').write_text(json.dumps(dict(objects=mapping,themes=themes,backgrounds=backgrounds),separators=(',',':'))+'\n',encoding='utf-8',newline='\n')
proof=dict(commit=model['commit'],source_files=802,campaign_levels=429,bonus_maps=14,benchmark_excluded=True,level_hashes={d['key']:hashlib.sha256(json.dumps(alllevels[d['key']],sort_keys=True,separators=(',',':')).encode()).hexdigest()for d in levels},missing_sprites=missing,converted_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in assets.iterdir()if p.name!='provenance.json'and p.is_file()})
(assets/'provenance.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8',newline='\n')
print('Imported',len(levels),'levels,',len(meta),'sprites/fonts/backgrounds. Missing sprites:',missing)
