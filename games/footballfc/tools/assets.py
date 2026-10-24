"""Author the game's original recolourable pixel art; no external assets required.

Five camera angles, idle/four running contacts/kick, packed two pixels per byte.
PNG sheets are editable inspection artifacts; the calculator uses assets.h only.
"""
from pathlib import Path
from PIL import Image, ImageDraw
ROOT = Path(__file__).resolve().parents[1]
PALETTE = [0, 0x182325, 0x284378, 0x182b50, 0x4f6aa3, 0xf1e8cc,
           0xc9d1bd, 0xe3b58d, 0xb17e60, 0x352f2b, 0x25334b,
           0x111c2e, 0xe6b650, 0x92a17c, 0x65765e, 0x394e40]

def person(angle, pose):
    im = Image.new('P', (16, 24)); d = ImageDraw.Draw(im)
    side = angle == 2; back = angle >= 3; diagonal = angle in (1, 3)
    step = [0, -3, -1, 3, 1, 0][pose]; bob = int(pose in (2, 4))
    # Back leg is shaded and kept behind the nearer boot and shorts.
    hip = 14 - bob; farx = 9 + step; nearx = 6 - step
    fary = 22 - max(0, step)//2; neary = 22 - max(0, -step)//2
    if side: farx -= 1; nearx += 1
    if pose == 5: nearx = 13; neary = 17; farx = 6; fary = 22
    d.line([(9, hip), (9+step//2, 18-bob), (farx, fary)], fill=8, width=2)
    d.line([(farx, fary-3), (farx, fary-1)], fill=6, width=2)
    d.line([(farx-1, fary), (farx+2, fary)], fill=1, width=2)
    d.line([(6, hip), (6-step//2, 18-bob), (nearx, neary)], fill=7, width=2)
    d.line([(nearx, neary-3), (nearx, neary-1)], fill=5, width=2)
    d.line([(nearx-1, neary), (nearx+2, neary)], fill=12, width=2)
    # Silhouette first; sleeves, highlights and collar are palette materials.
    l, r = (6, 10) if side else (5, 10)
    d.polygon([(l, 7-bob), (r, 7-bob), (r+1, hip-1), (l-1, hip-1)], fill=1)
    d.rectangle((l, 8-bob, r, hip-2), fill=2)
    d.line((l, 8-bob, l, hip-3), fill=4)
    d.line((r, 8-bob, r, hip-2), fill=3)
    d.rectangle((l-1, hip-1, r+1, hip+1), fill=10)
    d.rectangle((8, hip-1, r+1, hip+1), fill=11)
    arm = step//2
    d.line([(r, 9-bob), (12-arm, 12-bob), (11-arm, 14-bob)], fill=8, width=2)
    if not side:
        d.line([(l, 9-bob), (3+arm, 12-bob), (4+arm, 14-bob)], fill=7, width=2)
        d.line((l-1, 8-bob, l-2, 10-bob), fill=2, width=2)
    d.line((r, 8-bob, r+1, 10-bob), fill=3, width=2)
    d.rectangle((7, 5-bob, 8, 7-bob), fill=8)
    hx = 8 + int(side or diagonal)
    d.rectangle((hx-2, 1-bob, hx+1, 5-bob), fill=7)
    d.rectangle((hx-2, 1-bob, hx+1, 2-bob), fill=9)
    d.point((hx-2, 3-bob), fill=9)
    if back:
        d.rectangle((hx-2, 2-bob, hx+1, 4-bob), fill=9)
        d.line((7, 9-bob, 8, 9-bob), fill=5)
        d.line((8, 9-bob, 8, 11-bob), fill=5)
    elif side or diagonal:
        d.point((hx+2, 4-bob), fill=7); d.point((hx+1, 3-bob), fill=1)
    else:
        d.point((hx-1, 4-bob), fill=8); d.line((7, 8-bob, 8, 8-bob), fill=5)
    return im

def crowd(frame):
    im = Image.new('P', (32, 16)); d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 31, 15), fill=15)
    for i, x in enumerate(range(1, 32, 6)):
        y = 3+(i%2); shirt = [2, 5, 12, 3, 4, 10][i]
        d.rectangle((x, y+3, x+3, y+7), fill=shirt)
        d.rectangle((x+1, y, x+2, y+2), fill=7 if i%3 else 8)
        d.line((x, y+8, x, y+10), fill=1)
        d.line((x+3, y+8, x+3, y+10), fill=1)
        if i%3 == frame:
            d.line((x-1, y+4, x-2, y+1), fill=shirt)
            d.line((x+4, y+4, x+5, y+1), fill=shirt)
            d.line((x-2, y, x+5, y), fill=5 if i%2 else 12)
    d.line((0, 15, 31, 15), fill=14)
    return im

def pack(im):
    v = list(im.tobytes()); assert len(v)%2 == 0 and max(v)<16
    return bytes((v[i]<<4)|v[i+1] for i in range(0, len(v), 2))

def ball(frame):
    im = Image.new('P', (8, 8)); d = ImageDraw.Draw(im)
    d.ellipse((0, 0, 7, 7), fill=6); d.ellipse((0, 0, 6, 6), fill=5)
    x, y = [(3, 2), (4, 3), (3, 4), (2, 3)][frame]
    d.rectangle((x, y, x+1, y+1), fill=1)
    for px, py in [(1, 1), (5, 1), (6, 5), (1, 5)]: d.point((px, py), fill=1)
    return im

def scenery():
    bench = Image.new('P', (64, 24)); d = ImageDraw.Draw(bench)
    d.polygon([(0, 9), (5, 0), (60, 0), (63, 9)], fill=1)
    d.polygon([(3, 8), (7, 2), (58, 2), (61, 8)], fill=3)
    d.rectangle((2, 9, 61, 18), fill=14)
    for x in range(5, 61, 8):
        d.rectangle((x, 11, x+5, 19), fill=4); d.line((x, 19, x+5, 19), fill=1)
        if x%3:
            d.rectangle((x+1, 7, x+3, 10), fill=7)
            d.rectangle((x, 11, x+4, 15), fill=12)
            d.line((x+1, 16, x+1, 21), fill=1); d.line((x+3, 16, x+4, 21), fill=1)
    d.rectangle((0, 21, 63, 23), fill=1)
    d.line((1, 8, 1, 22), fill=5); d.line((62, 8, 62, 22), fill=5)
    steward = Image.new('P', (12, 24)); d = ImageDraw.Draw(steward)
    d.rectangle((4, 2, 7, 6), fill=7); d.rectangle((3, 1, 8, 3), fill=1)
    d.polygon([(3, 7), (8, 7), (9, 14), (2, 14)], fill=12)
    d.line((3, 10, 8, 10), fill=5); d.line((3, 13, 8, 13), fill=5)
    d.line((1, 8, 1, 15), fill=7, width=2); d.line((10, 8, 10, 15), fill=7, width=2)
    d.line((4, 15, 3, 22), fill=1, width=2); d.line((7, 15, 8, 22), fill=1, width=2)
    camera = Image.new('P', (24, 24)); d = ImageDraw.Draw(camera)
    d.line((9, 9, 3, 23), fill=14, width=2); d.line((9, 9, 15, 23), fill=14, width=2)
    d.rectangle((2, 3, 13, 9), fill=1); d.rectangle((3, 4, 9, 7), fill=3)
    d.rectangle((13, 4, 16, 8), fill=5); d.rectangle((6, 0, 10, 2), fill=1)
    d.rectangle((18, 4, 21, 8), fill=7); d.rectangle((17, 3, 22, 4), fill=9)
    d.rectangle((17, 9, 22, 15), fill=2); d.line((18, 10, 12, 7), fill=7, width=2)
    d.line((18, 16, 17, 23), fill=1, width=2); d.line((21, 16, 23, 23), fill=1, width=2)
    banner = Image.new('P', (64, 12)); d = ImageDraw.Draw(banner)
    d.rectangle((0, 0, 63, 10), fill=3); d.line((0, 0, 63, 0), fill=5)
    for x in range(-8, 64, 16): d.polygon([(x, 10), (x+8, 1), (x+13, 1), (x+5, 10)], fill=12)
    d.line((0, 11, 63, 11), fill=1)
    return [('fc_bench_pixels', bench), ('fc_steward_pixels', steward), ('fc_camera_pixels', camera), ('fc_banner_pixels', banner)]

def main():
    dst = ROOT/'assets'; dst.mkdir(exist_ok=True)
    players = [person(a, p) for a in range(5) for p in range(6)]
    spectators = [crowd(f) for f in range(3)]
    balls = [ball(f) for f in range(4)]
    sheet = Image.new('P', (16*6, 24*5))
    for i, im in enumerate(players): sheet.paste(im, ((i%6)*16, (i//6)*24))
    palette = [component for c in PALETTE for component in (c>>16, (c>>8)&255, c&255)]
    sheet.putpalette(palette); sheet.save(dst/'players.png', transparency=0)
    sheet.resize((576, 720), Image.Resampling.NEAREST).save(dst/'players-preview.png', transparency=0)
    cs = Image.new('P', (96, 16)); cs.putpalette(palette)
    for i, im in enumerate(spectators): cs.paste(im, (32*i, 0))
    cs.save(dst/'supporters.png')
    bs = Image.new('P', (32, 8)); bs.putpalette(palette)
    for i, im in enumerate(balls): bs.paste(im, (8*i, 0))
    bs.save(dst/'ball.png', transparency=0)
    arrays = [('fc_player_pixels', players), ('fc_crowd_pixels', spectators), ('fc_ball_pixels', balls)]
    for name, im in scenery():
        arrays.append((name, [im])); im.putpalette(palette)
        im.save(dst/(name.removeprefix('fc_').removesuffix('_pixels')+'.png'), transparency=0)
    source = '/* Original Football FC pixel art. Regenerate: python tools/assets.py */\n'
    for name, images in arrays:
        data = b''.join(map(pack, images))
        source += f'static const uint8_t {name}[{len(data)}]={{\n'
        source += '\n'.join(','.join(f'0x{b:02x}' for b in data[i:i+24])+',' for i in range(0, len(data), 24))
        source += '\n};\n'
    (ROOT/'src/assets.h').write_text(source, newline='\n')
    print('Player/crowd/ball/scenery: '+str(sum(len(pack(im)) for _, ims in arrays for im in ims))+' bytes in flash; no decoded RAM atlas')

if __name__ == '__main__': main()
