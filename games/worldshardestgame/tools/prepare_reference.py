"""Make static reference movies for the board and collision-mask exports."""
from pathlib import Path
import argparse,copy,xml.etree.ElementTree as E
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--reference',type=Path,required=True);a=ap.parse_args();root=a.reference
 r=E.parse(root/'original.xml').getroot()
 for mode in ['boards','walls']:
  t=copy.deepcopy(r);tags=t.find('tags')
  for x in list(tags):
   typ=x.get('type','')
   if typ.startswith('PlaceObject'):
    name=x.get('name','');keep=name=='walls'if mode=='walls'else name not in ['player','enemies','bars']and x.get('characterId')!='183'
    if not keep:tags.remove(x)
   elif typ in ['DoActionTag','StartSoundTag','SoundStreamBlockTag']:tags.remove(x)
  E.ElementTree(t).write(root/(mode+'.xml'),encoding='utf-8',xml_declaration=True)
 print('Prepared board-only and wall-only timelines.')
if __name__=='__main__':main()
