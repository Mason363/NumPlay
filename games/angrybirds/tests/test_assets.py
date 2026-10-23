"""Offline source preservation, bounds and reproducible ROM generation."""
from pathlib import Path
import hashlib,json,re,struct,subprocess,sys,zlib
ROOT=Path(__file__).resolve().parents[1];A=ROOT/'assets'
proof=json.loads((A/'provenance.json').read_text())
for name,digest in proof['converted_sha256'].items():assert hashlib.sha256((A/name).read_bytes()).hexdigest()==digest,name
model=json.loads(zlib.decompress((A/'source.json.zlib').read_bytes()))
assert len(model['levels'])==443 and sum(g['count']for g in model['groups'])==443
assert model['groups'][-1]['count']==14 and len(model['stories'])==19
assert 'benchmark-1'not in {d['key']for d in model['levels']}
before={n:(ROOT/'src'/n).read_bytes()for n in ['data.c','ids.h']}
subprocess.run([sys.executable,str(ROOT/'tools/assets.py')],check=True)
for n,data in before.items():assert data==(ROOT/'src'/n).read_bytes(),n
source=before['data.c'].decode();packed=bytes(map(int,re.search(r'const uint8_t level_bytes\[\]=\{([^}]+)',source).group(1).split(',')))
report=json.loads((ROOT/'output/asset-report.json').read_text());offset=0
for d,p in zip(model['levels'],report['levels_proof']):
    # Each DEFLATE stream is independently bounded and self-terminating.
    decoder=zlib.decompressobj(-15);planar=decoder.decompress(packed[offset:]);consumed=len(packed)-offset-len(decoder.unused_data);assert decoder.eof;offset+=consumed
    raw=bytearray(len(planar));n=len(planar)//4
    for i in range(4):raw[i::4]=planar[i*n:(i+1)*n]
    assert hashlib.sha256(raw).hexdigest()==p['raw_sha256']
    count,joints,birds=struct.unpack_from('<HHB',raw);assert len(raw)==8+count*16+joints*44 and count<=350 and joints<=145 and len(raw)<=14000
    originals=[(k,v)for k,v in d['world'].items()if'id'in v];originals.sort(key=lambda kv:(not kv[0].startswith('bird'),int(re.search(r'\d+',kv[0]).group())if re.search(r'\d+',kv[0])else 0))
    assert count==len(originals)and birds==sum(k.startswith('bird')for k,v in originals)
    for i,(key,v)in enumerate(originals):
        assert raw[8+i*16+4:8+i*16+16]==struct.pack('<fff',v['x'],v['y'],v['angle'])
    assert count+joints==len(d['world'])
assert offset==len(packed)
print('PASS: 429 campaign levels + 14 bonus maps, exact float32 placements, 19 storyboards, input hashes and deterministic packing')
