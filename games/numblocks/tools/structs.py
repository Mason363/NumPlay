#!/usr/bin/env python3
"""Packs how Minecraft 1.8.8 builds its villages' and desert temples' pieces into src/gen_struct_data.h.

    python3 tools/structs.py MCP_DIR

MCP_DIR holds the decompiled 1.8.8 (1.8.9: the same world) classes StructureVillagePieces.java and
ComponentScatteredFeaturePieces.java (MCP names). Their addComponentParts methods are read, not run:
a small parser walks the few statements they use (fills, blocks set, loops, ifs) and writes, for each
piece (and each of its random variants), a program of ops in the piece's own coordinates:

  what never depends on the world or the random  -> boxes of one block, the fewest that cover it
  the rest, in its order                          -> doors, steps, crops, chests, pillars, villagers

The game (gen_struct.h) runs them, turned the piece's way, into the chunk being made."""
import os
import re
import sys

# ---------------------------------------------------------------- blocks: the palette
# what the programs place: (name, meta) of 1.8.8, and how NumBlocks writes it (blocks.h's name). Stairs and
# torches turn with the piece (k: 'stairs' takes getMetadataWithOffset's meta, 'torch' a facing).
PAL = []


def pal(state):
    if state not in PAL:
        PAL.append(state)
    return PAL.index(state)


# 1.8.8 block (and meta) -> NumBlocks block name
FIXED = {
    ('air', 0): 'AIR', ('cobblestone', 0): 'COBBLESTONE', ('planks', 0): 'PLANKS_OAK', ('planks', 1): 'PLANKS_SPRUCE',
    ('log', 0): 'LOG_OAK', ('glass_pane', 0): 'GLASS_PANE', ('oak_fence', 0): 'FENCE_OAK',
    ('farmland', 0): 'FARMLAND', ('water', 0): 'WATER', ('flowing_water', 0): 'WATER', ('dirt', 0): 'DIRT',
    ('wooden_pressure_plate', 0): 'WOODEN_PRESSURE_PLATE', ('double_stone_slab', 0): 'STONE_SLAB_DOUBLE',
    ('stone_slab', 0): 'STONE_SLAB', ('stone_slab', 1): 'SANDSTONE_SLAB', ('sandstone', 0): 'SANDSTONE',
    ('sandstone', 1): 'SANDSTONE_CHISELED', ('sandstone', 2): 'SANDSTONE_SMOOTH', ('gravel', 0): 'GRAVEL',
    ('iron_bars', 0): 'IRON_BARS', ('furnace', 0): 'FURNACE', ('flowing_lava', 0): 'FLOWING_LAVA',
    ('bookshelf', 0): 'BOOKSHELF', ('wool', 15): 'WOOL_BLACK', ('crafting_table', 0): 'CRAFTING_TABLE',
    ('ladder', 0): 'LADDER', ('tnt', 0): 'TNT', ('stone_pressure_plate', 0): 'STONE_PRESSURE_PLATE',
    ('stained_hardened_clay', 1): 'STAINED_CLAY_ORANGE', ('stained_hardened_clay', 11): 'STAINED_CLAY_BLUE',
    ('chest', 0): 'CHEST',
}
STAIRS = {'oak_stairs': 'OAK_STAIRS', 'stone_stairs': 'COBBLESTONE_STAIRS', 'sandstone_stairs': 'SANDSTONE_STAIRS'}


def block_key(st):
    """palette entry -> ('fixed', name) | ('stairs', family, meta) | ('torch', turn)"""
    name, meta, extra = st
    if name in STAIRS:
        return ('stairs', STAIRS[name], meta)
    if name == 'torch':
        return ('torch', extra)
    if name == 'ladder':
        return ('fixed', 'LADDER')
    if (name, meta) not in FIXED:
        raise SystemExit(f'structs.py: no NumBlocks block for {name}:{meta}')
    return ('fixed', FIXED[(name, meta)])


# ---------------------------------------------------------------- Java, read (a small subset of it)
TOK = re.compile(r'\s*(?:(\d+\.\d*F?|\d+F|\d+)|([A-Za-z_$][\w$]*)|(<<|>>|<=|>=|==|!=|&&|\|\||\+\+|--|\+=|-=|[-+*/%<>=!&|~?:;,.(){}\[\]]))')


def tokens(src):
    out, i = [], 0
    while i < len(src):
        m = TOK.match(src, i)
        if not m:
            if src[i:].strip() == '':
                break
            raise SystemExit('structs.py: cannot read: ' + src[i:i + 60])
        i = m.end()
        num, name, op = m.groups()
        if num is not None:
            out.append(('num', float(num.rstrip('F')) if ('.' in num or num.endswith('F')) else int(num)))
        elif name is not None:
            out.append(('name', name))
        elif op is not None:
            out.append(('op', op))
    return out


class P:
    def __init__(self, toks):
        self.t, self.i = toks, 0

    def peek(self, k=0):
        return self.t[self.i + k] if self.i + k < len(self.t) else ('eof', None)

    def take(self, v=None):
        tok = self.peek()
        if v is not None and tok[1] != v:
            raise SystemExit(f'structs.py: expected {v}, got {tok} near {self.t[self.i:self.i + 12]}')
        self.i += 1
        return tok

    def at(self, v):
        return self.peek()[1] == v

    # statements: ('block', [s]) ('for', init, cond, step, s) ('foreach', var, expr, s) ('if', c, s, s)
    # ('decl', name, expr) ('assign', target, op, expr) ('expr', e) ('return', e)
    def stmt(self):
        if self.at('{'):
            self.take('{')
            body = []
            while not self.at('}'):
                body.append(self.stmt())
            self.take('}')
            return ('block', body)
        if self.at('for'):
            self.take('for')
            self.take('(')
            if self.peek(2)[1] == ':':
                self.take()
                var = self.take()[1]
                self.take(':')
                e = self.expr()
                self.take(')')
                return ('foreach', var, e, self.stmt())
            init = self.simple()
            self.take(';')
            cond = self.expr()
            self.take(';')
            step = self.simple()
            self.take(')')
            return ('for', init, cond, step, self.stmt())
        if self.at('if'):
            self.take('if')
            self.take('(')
            c = self.expr()
            self.take(')')
            s = self.stmt()
            e = None
            if self.at('else'):
                self.take('else')
                e = self.stmt()
            return ('if', c, s, e)
        if self.at('return'):
            self.take('return')
            e = self.expr()
            self.take(';')
            return ('return', e)
        s = self.simple()
        self.take(';')
        return s

    def simple(self):
        t0, t1 = self.peek(), self.peek(1)
        if t0[0] == 'name' and t1[0] == 'name' and t0[1] in ('int', 'boolean', 'float', 'IBlockState', 'EnumFacing'):
            self.take()
            name = self.take()[1]
            self.take('=')
            return ('decl', name, self.expr())
        if t0[1] in ('++', '--'):
            self.take()
            return ('assign', self.postfix(), '+=' if t0[1] == '++' else '-=', ('num', 1))
        e = self.expr()
        if self.peek()[1] in ('=', '+=', '-='):
            op = self.take()[1]
            return ('assign', e, op, self.expr())
        if self.peek()[1] in ('++', '--'):
            op = self.take()[1]
            return ('assign', e, '+=' if op == '++' else '-=', ('num', 1))
        return ('expr', e)

    BIN = [['||'], ['&&'], ['|'], ['&'], ['==', '!='], ['<', '<=', '>', '>='], ['<<', '>>'], ['+', '-'], ['*', '/', '%']]

    def expr(self):
        c = self.binary(0)
        if self.at('?'):
            self.take('?')
            a = self.expr()
            self.take(':')
            b = self.expr()
            return ('?', c, a, b)
        return c

    def binary(self, lvl):
        if lvl == len(self.BIN):
            return self.unary()
        e = self.binary(lvl + 1)
        while self.peek()[1] in self.BIN[lvl]:
            op = self.take()[1]
            e = ('bin', op, e, self.binary(lvl + 1))
        return e

    def unary(self):
        if self.peek()[1] in ('-', '!', '~'):
            op = self.take()[1]
            return ('un', op, self.unary())
        if self.at('(') and self.peek(1)[1] in ('float', 'int') and self.peek(2)[1] == ')':
            self.take(), self.take(), self.take()
            return self.unary()
        return self.postfix()

    def postfix(self):
        tok = self.take()
        if tok[0] == 'num':
            e = ('num', tok[1])
        elif tok[1] == '(':
            e = self.expr()
            self.take(')')
        elif tok[1] == 'new':
            cls = self.take()[1]
            while self.at('.'):
                self.take('.')
                cls += '.' + self.take()[1]
            if self.at('['):
                self.take('['), self.take(']')
                self.take('{')
                items = []
                while not self.at('}'):
                    items.append(self.expr())
                    if self.at(','):
                        self.take(',')
                self.take('}')
                e = ('array', cls, items)
            else:
                e = ('new', cls, self.args())
        else:
            e = ('name', tok[1])
        while True:
            if self.at('.'):
                self.take('.')
                e = ('get', e, self.take()[1])
            elif self.at('('):
                e = ('call', e, self.args())
            elif self.at('['):
                self.take('[')
                i = self.expr()
                self.take(']')
                e = ('index', e, i)
            else:
                return e

    def args(self):
        self.take('(')
        a = []
        while not self.at(')'):
            a.append(self.expr())
            if self.at(','):
                self.take(',')
        self.take(')')
        return a


def method_body(src, cls, method='addComponentParts'):
    """the statements of cls's method (by brace matching)"""
    m = re.search(r'public static class ' + cls + r'\b', src)
    if not m:
        raise SystemExit('structs.py: no class ' + cls)
    i = src.index('public boolean ' + method + '(', m.end())
    j = src.index('{', i)
    depth, k = 0, j
    while True:
        if src[k] == '{':
            depth += 1
        elif src[k] == '}':
            depth -= 1
            if depth == 0:
                break
        k += 1
    p = P(tokens(src[j:k + 1]))
    return p.stmt()


# ---------------------------------------------------------------- the walk
class Dyn:
    """a value only known when the piece is built (the random, the world): it may only flow into ops"""
    def __init__(self, what):
        self.what = what


FACES = {'NORTH': 2, 'SOUTH': 3, 'WEST': 4, 'EAST': 5}            # EnumFacing.getIndex
HORIZ = {'SOUTH': 0, 'WEST': 1, 'NORTH': 2, 'EAST': 3}            # getHorizontalIndex


class Face:
    """a facing: absolute, or the piece's own turned (turn 0 coordBaseMode, 1 rotateY, 2 opposite, 3 rotateYCCW)"""
    def __init__(self, turn=None, absolute=None):
        self.turn, self.absolute = turn, absolute


class Walk:
    def __init__(self, fields, size=(0, 0, 0)):
        self.fields = fields          # the piece's own: flags of its variant
        self.vars = {}
        self.ops = []                 # ('set', x, y, z, pal) ('dyn', kind, args...)
        self.sx, self.sy, self.sz = size

    def run(self, s):
        k = s[0]
        if k == 'block':
            for t in s[1]:
                if self.run(t) == 'return':
                    return 'return'
        elif k == 'decl':
            self.vars[s[1]] = self.ev(s[2])
        elif k == 'assign':
            tgt, op, v = s[1], s[2], self.ev(s[3])
            if tgt[0] == 'name':
                cur = self.vars.get(tgt[1])
                self.vars[tgt[1]] = v if op == '=' else cur + v if op == '+=' else cur - v
            elif tgt[0] == 'get' and tgt[1] == ('name', 'this'):
                self.fields[tgt[2]] = v
            elif tgt[0] == 'index':
                pass   # (a chest's flag: the op keeps it)
            else:
                raise SystemExit(f'structs.py: assignment to {tgt}')
        elif k == 'for':
            self.run(s[1])
            while self.ev(s[2]):
                if self.run(s[4]) == 'return':
                    return 'return'
                self.run(s[3])
        elif k == 'foreach':
            coll = self.ev(s[2])
            for v in coll:
                self.vars[s[1]] = v
                self.run(s[3])
        elif k == 'if':
            c = self.cond(s)
            if c is None:
                return
            if c:
                return self.run(s[2])
            elif s[3]:
                return self.run(s[3])
        elif k == 'expr':
            self.ev(s[1])
        elif k == 'return':
            return 'return'

    def cond(self, s):
        """an if's test; the ones that look at the world become ops (None: handled)"""
        c = s[1]
        txt = repr(c)
        if 'field_143015_k' in txt or 'func_74935_a' in txt:
            return False   # (the piece's height: gen_struct.h finds it before running the program)
        if 'getBlockStateFromPos' in txt:
            # the stone step in front of a door: air there, something under it
            body = s[2]
            call = body[1][0] if body[0] == 'block' else body
            args = call[1][2]
            x, y, z = (self.ev(a) for a in args[2:5])
            self.ops.append(('dyn', 'step', x, y, z))
            return None
        if 'hasMadeChest' in txt:
            # House2: the chest, once, by the population whose box holds it
            body = s[2][1]
            call = [t for t in body if t[0] == 'expr'][0][1]
            a = call[2]
            x, y, z = (self.ev(v) for v in a[3:6])
            self.ops.append(('dyn', 'chest', x, y, z, 'blacksmith'))
            return None
        if 'hasPlacedChest' in txt:
            body = s[2][1]
            xs = {}
            for t in body:
                if t[0] == 'decl':
                    xs[t[1]] = self.ev(t[2])
            call = [t for t in body if t[0] == 'assign'][0][3]
            a = call[2]
            x = self.ev(a[3], xs)
            y = self.ev(a[4], xs)
            z = self.ev(a[5], xs)
            self.ops.append(('dyn', 'chest', x, y, z, 'desert'))
            return None
        sym = [n for n, f in self.fields.items() if isinstance(f, Sym) and repr(('get', ('name', 'this'), n)) in txt]
        if sym:
            # a test of the piece's own random choice: each branch, under a guard, for the values that take it
            name = sym[0]
            dom = self.fields[name].domain
            for branch, want in ((s[2], True), (s[3], False)):
                if branch is None:
                    continue
                for v in dom:
                    self.fields[name] = v
                    if bool(self.ev(c)) != want:
                        continue
                    saved, self.ops = self.ops, []
                    self.run(branch)
                    sub, self.ops = self.ops, saved
                    self.ops.append(('if', name, v, sub))
                self.fields[name] = Sym(dom)
            return None
        v = self.ev(c)
        if isinstance(v, Dyn):
            raise SystemExit(f'structs.py: a test that depends on the world: {txt[:200]}')
        return bool(v)

    def ev(self, e, extra=None):
        k = e[0]
        if k == 'num':
            return e[1]
        if k == 'name':
            n = e[1]
            if extra and n in extra:
                return extra[n]
            if n in self.vars:
                return self.vars[n]
            if n in ('true', 'false'):
                return n == 'true'
            if n in ('worldIn', 'structureBoundingBoxIn', 'randomIn', 'this', 'Blocks', 'EnumFacing', 'Material',
                     'BlockTorch', 'BlockSandStone', 'EnumDyeColor', 'MathHelper', 'BlockStoneSlab', 'Items',
                     'WeightedRandomChestContent', 'BlockStairs', 'null'):
                return ('sym', n)
            raise SystemExit('structs.py: unknown name ' + n)
        if k == '?':
            return self.ev(e[2], extra) if self.ev(e[1], extra) else self.ev(e[3], extra)
        if k == 'un':
            v = self.ev(e[2], extra)
            return -v if e[1] == '-' else (not v) if e[1] == '!' else ~v
        if k == 'bin':
            a, b = self.ev(e[2], extra), self.ev(e[3], extra)
            op = e[1]
            if isinstance(a, Face) or isinstance(b, Face):
                if op in ('==', '!='):
                    same = a.turn == b.turn and a.absolute == b.absolute
                    if a.turn is not None and b.absolute is not None or a.absolute is not None and b.turn is not None:
                        raise FaceTest(a, b, op)
                    return same if op == '==' else not same
            return {'+': lambda: a + b, '-': lambda: a - b, '*': lambda: a * b,
                    '/': lambda: int(a / b), '%': lambda: a % b, '<': lambda: a < b, '<=': lambda: a <= b,
                    '>': lambda: a > b, '>=': lambda: a >= b, '==': lambda: a == b, '!=': lambda: a != b,
                    '&&': lambda: a and b, '||': lambda: a or b, '&': lambda: a & b, '|': lambda: a | b}[op]()
        if k == 'get':
            base = e[1]
            if base == ('name', 'this'):
                f = e[2]
                if f == 'scatteredFeatureSizeX':
                    return self.sx
                if f == 'scatteredFeatureSizeY':
                    return self.sy
                if f == 'scatteredFeatureSizeZ':
                    return self.sz
                if f == 'coordBaseMode':
                    return Face(turn=0)
                if f in self.fields:
                    return self.fields[f]
                raise SystemExit('structs.py: field ' + f)
            if base == ('name', 'EnumFacing'):
                return Face(absolute=e[2])
            if base[0] == 'name' and base[1] == 'Blocks':
                return ('block', e[2])
            if base == ('get', ('name', 'EnumFacing'), 'Plane'):
                return [Face(absolute=f) for f in ('NORTH', 'EAST', 'SOUTH', 'WEST')]
            if base[0] == 'get' and base[1] == ('name', 'EnumDyeColor'):
                return ('dye', base[2]) if False else ('dyecolor', e[2])
            if base[0] in ('get', 'name') and repr(base).count('Type') or e[2] in ('FACING', 'ATTACHED'):
                return ('sym', e[2])
            if base[0] == 'get' and base[1] in (('name', 'BlockSandStone'), ('name', 'BlockStoneSlab')):
                return ('enum', e[2])
            return ('sym', e[2])
        if k == 'index':
            return Dyn('index')
        if k == 'call':
            return self.call(e[1], [self.ev(a, extra) if not (isinstance(a, tuple) and a[0] == 'array') else None
                                    for a in e[2]], e[2], extra)
        if k == 'new':
            return ('new', e[1])
        if k == 'array':
            return None
        raise SystemExit(f'structs.py: expression {k}')

    def state(self, v):
        """a value meant as a block state -> (name, meta, extra)"""
        if isinstance(v, tuple) and v[0] == 'state':
            return v[1:]
        raise SystemExit(f'structs.py: not a block state: {v}')

    def call(self, f, a, raw, extra):
        if f[0] != 'get':
            raise SystemExit(f'structs.py: call of {f}')
        obj, m = f[1], f[2]
        # block states
        if m == 'getDefaultState':
            b = self.ev(obj, extra)
            return ('state', b[1], 0, None)
        if m == 'getStateFromMeta':
            b = self.ev(obj, extra)
            meta = a[0]
            if isinstance(b, tuple) and b[0] == 'crop':
                return ('state', 'crop', b[1], meta)
            if isinstance(meta, Dyn):
                return ('state', b[1], meta, None)
            if isinstance(meta, tuple) and meta[0] == 'turned':
                return ('state', b[1], meta[2], 'turned')
            return ('state', b[1], meta, None)
        if m == 'withProperty':
            st = self.ev(obj, extra)
            if isinstance(a[1], Face):
                return ('state', st[1], 0, a[1].turn)
            return st
        if m == 'getMetadata':
            o = self.ev(obj, extra)
            return {'DEFAULT': 0, 'CHISELED': 1, 'SMOOTH': 2, 'SAND': 1}[o[1]]
        if m == 'getDyeDamage':
            o = self.ev(obj, extra)
            return {'WHITE': 15, 'ORANGE': 14, 'BLUE': 4}[o[1]]
        if m in ('rotateY', 'rotateYCCW', 'getOpposite'):
            fc = self.ev(obj, extra)
            d = {'rotateY': 1, 'getOpposite': 2, 'rotateYCCW': 3}[m]
            if fc.turn is not None:
                return Face(turn=(fc.turn + d) % 4)
            order = ['NORTH', 'EAST', 'SOUTH', 'WEST']
            return Face(absolute=order[(order.index(fc.absolute) + d) % 4])
        if m in ('getFrontOffsetX', 'getFrontOffsetZ'):
            fc = self.ev(obj, extra)
            dx = {'NORTH': (0, -1), 'SOUTH': (0, 1), 'WEST': (-1, 0), 'EAST': (1, 0)}[fc.absolute]
            return dx[0] if m == 'getFrontOffsetX' else dx[1]
        if m == 'getHorizontalIndex':
            return HORIZ[self.ev(obj, extra).absolute]
        if m == 'getIndex':
            return FACES[self.ev(obj, extra).absolute]
        if m == 'getHorizontal':
            return ('horiz', a[0])
        if m == 'getMetadataWithOffset':
            return ('turned', a[0][1], a[1])
        if m == 'getRandomIntegerInRange':
            return Dyn(('rand', self.ev(raw[1], extra), self.ev(raw[2], extra)))
        if m == 'nextInt':
            return Dyn(('nextInt', a[0]))
        if obj == ('name', 'this') and m.startswith('cropType'):
            return None
        # piece ops
        if obj == ('name', 'this'):
            return self.op(m, a, raw, extra)
        if m in ('getBlock', 'getMaterial'):
            return Dyn('world')
        if m in ('func_177629_a', 'getRandom'):
            return None
        if m == 'isVecInside':
            return Dyn('inside')
        raise SystemExit(f'structs.py: method {m}')

    def op(self, m, a, raw, extra):
        if m == 'fillWithBlocks':
            x0, y0, z0, x1, y1, z1 = a[2:8]
            out, ins, keep = self.state(a[8]), self.state(a[9]), a[10]
            if keep:
                raise SystemExit('structs.py: fillWithBlocks over what exists')
            self.ops.append(('fill', x0, y0, z0, x1, y1, z1, pal(out), pal(ins)))
            return None
        if m == 'fillWithAir':
            x0, y0, z0, x1, y1, z1 = a[1:7]
            p = pal(('air', 0, None))
            self.ops.append(('fill', x0, y0, z0, x1, y1, z1, p, p))
            return None
        if m == 'setBlockState':
            st = a[1]
            x, y, z = a[2:5]
            if isinstance(st, tuple) and st[1] == 'crop':
                self.ops.append(('dyn', 'crop', x, y, z, st[2]))
                return None
            self.put(x, y, z, self.state(st))
            return None
        if m == 'placeDoorCurrentPosition':
            x, y, z = a[3:6]
            h = a[6]
            assert h[0] == 'horiz' and h[1][0] == 'turned'
            self.ops.append(('dyn', 'door', x, y, z, h[1][2]))
            return None
        if m == 'clearCurrentPositionBlocksUpwards':
            x, y, z = a[1:4]
            self.ops.append(('dyn', 'clear', x, y, z))
            return None
        if m == 'replaceAirAndLiquidDownwards':
            st = self.state(a[1])
            x, y, z = a[2:5]
            self.ops.append(('dyn', 'down', x, y, z, pal(st)))
            return None
        if m == 'spawnVillagers':
            x, y, z, n = a[2:6]
            self.ops.append(('dyn', 'villagers', x, y, z, n))
            return None
        if m == 'getXWithOffset' or m == 'getYWithOffset' or m == 'getZWithOffset':
            return Dyn('pos')
        if m.startswith('func_15155') or m.startswith('func_151560'):
            return None
        raise SystemExit('structs.py: piece method ' + m)

    def put(self, x, y, z, st):
        self.ops.append(('set', x, y, z, pal(st)))


class FaceTest(Exception):
    pass


class Sym:
    """a field the piece draws when it is laid out: its values"""
    def __init__(self, domain):
        self.domain = domain


# ---------------------------------------------------------------- programs
# Bytes. Coordinates are the piece's own: x, z in 0..31, y + 16 in 0..31, three in two bytes (x | y << 5 |
# z << 10). The first byte of an op: its kind in the top two bits, then a palette index (blocks) or a sub-kind.
K_SET, K_BOX, K_FILL, K_EXT = 0, 1, 2, 3       # a block; a box of one; a box of one with another inside; others
X_STEP, X_DOOR, X_CROP, X_CHEST, X_VILLAGERS, X_CLEAR, X_DOWN, X_IF, X_END = range(9)
CHEST_KIND = {'blacksmith': 0, 'desert': 1}
# the piece's flags (gen_struct.h's VPiece flag): where each field the piece drew is kept
FLAG = {'isRoofAccessible': (0, 1), 'isTallHouse': (0, 1), 'tablePosition': (1, 3)}


def xyz(x, y, z):
    """(a step or a torch may sit one block outside the piece, at -1: kept as 31)"""
    x, z = x & 31, z & 31
    assert -16 <= y < 16, (x, y, z)
    v = x | (y + 16) << 5 | z << 10
    return [v & 255, v >> 8]


def boxes(cells):
    """cells {(x, y, z): pal} -> the fewest boxes of one block that cover them (greedily, biggest first)"""
    left = dict(cells)
    out = []
    while left:
        best = None
        for (x, y, z), p in sorted(left.items()):
            x1 = x
            while left.get((x1 + 1, y, z)) == p:
                x1 += 1
            z1 = z
            while all(left.get((i, y, z1 + 1)) == p for i in range(x, x1 + 1)):
                z1 += 1
            y1 = y
            while all(left.get((i, y1 + 1, k)) == p for i in range(x, x1 + 1) for k in range(z, z1 + 1)):
                y1 += 1
            n = (x1 - x + 1) * (y1 - y + 1) * (z1 - z + 1)
            if best is None or n > best[0]:
                best = (n, x, y, z, x1, y1, z1, p)
        _, x, y, z, x1, y1, z1, p = best
        for i in range(x, x1 + 1):
            for j in range(y, y1 + 1):
                for k in range(z, z1 + 1):
                    del left[(i, j, k)]
        out.append((x, y, z, x1, y1, z1, p))
    return out


def rects(ops, kind):
    """clear and down ops (x, y, z[, pal]) of one height and block, as rectangles (one column each)"""
    out = []
    by = {}
    for o in ops:
        key = (o[2],) + tuple(o[4:])
        by.setdefault(key, set()).add((o[1], o[3]))
    for key, cols in sorted(by.items()):
        cols = set(cols)
        while cols:
            x, z = min(cols)
            x1 = x
            while (x1 + 1, z) in cols:
                x1 += 1
            z1 = z
            while all((i, z1 + 1) in cols for i in range(x, x1 + 1)):
                z1 += 1
            for i in range(x, x1 + 1):
                for k in range(z, z1 + 1):
                    cols.discard((i, k))
            out.append((kind, x, key[0], z, x1, z1) + key[1:])
    return out


def program(ops, top=True):
    """the walk's ops, in their order -> bytes. A run of single blocks becomes boxes (the fewest for what the
    run leaves); a run of pillars and clearings becomes rectangles."""
    out = []
    i = 0
    while i < len(ops):
        o = ops[i]
        k = o[0]
        if k == 'set':
            run = {}
            while i < len(ops) and ops[i][0] == 'set':
                run[ops[i][1:4]] = ops[i][4]
                i += 1
            for b in boxes(run):
                if b[:3] == b[3:6]:
                    out += [K_SET << 6 | b[6]] + xyz(*b[:3])
                else:
                    out += [K_BOX << 6 | b[6]] + xyz(*b[:3]) + xyz(*b[3:6])
            continue
        if k == 'dyn' and o[1] in ('clear', 'down'):
            run = []
            while i < len(ops) and ops[i][0] == 'dyn' and ops[i][1] in ('clear', 'down'):
                run.append(ops[i][1:])
                i += 1
            for r in rects([q for q in run if q[0] == 'clear'], X_CLEAR) + \
                    rects([q for q in run if q[0] == 'down'], X_DOWN):
                out += [K_EXT << 6 | r[0]] + xyz(r[1], r[2], r[3]) + xyz(r[4], 0, r[5]) + \
                    ([r[6]] if r[0] == X_DOWN else [])
            continue
        i += 1
        if k == 'fill':
            x0, y0, z0, x1, y1, z1, po, pi = o[1:]
            hollow = po != pi and x1 - x0 > 1 and y1 - y0 > 1 and z1 - z0 > 1
            if hollow:
                out += [K_FILL << 6 | po, pi] + xyz(x0, y0, z0) + xyz(x1, y1, z1)
            else:
                out += [K_BOX << 6 | po] + xyz(x0, y0, z0) + xyz(x1, y1, z1)
        elif k == 'if':
            sh, mask = FLAG[o[1]]
            body = program(o[3], False)
            assert len(body) < 256
            out += [K_EXT << 6 | X_IF, sh << 4 | mask, int(o[2]), len(body)] + body
        elif k == 'dyn':
            d = o[1]
            if d == 'step':
                out += [K_EXT << 6 | X_STEP] + xyz(*o[2:5])
            elif d == 'door':
                out += [K_EXT << 6 | X_DOOR] + xyz(*o[2:5]) + [o[5]]
            elif d == 'crop':
                out += [K_EXT << 6 | X_CROP] + xyz(*o[2:5]) + [ord(o[5][-1]) - ord('A')]
            elif d == 'chest':
                out += [K_EXT << 6 | X_CHEST] + xyz(*o[2:5]) + [CHEST_KIND[o[5]]]
            elif d == 'villagers':
                out += [K_EXT << 6 | X_VILLAGERS] + xyz(*o[2:5]) + [o[5]]
            else:
                raise SystemExit('structs.py: op ' + d)
        else:
            raise SystemExit(f'structs.py: op {o}')
    if top:
        out.append(K_EXT << 6 | X_END)
    return out


VILLAGE = [  # (class, the fields it draws when laid out)
    ('Well', {}), ('Torch', {}), ('Church', {}), ('House1', {}), ('House2', {'hasMadeChest': False}),
    ('House3', {}), ('House4Garden', {'isRoofAccessible': [False, True]}),
    ('WoodHut', {'isTallHouse': [False, True], 'tablePosition': [0, 1, 2]}),
    ('Hall', {}), ('Field1', {}), ('Field2', {}),
]


def crop_fields():
    return {f'cropType{c}': ('crop', c) for c in 'ABCD'}


def walk(src, cls, fields, size=(0, 0, 0)):
    f = {k: Sym(v) if isinstance(v, list) else v for k, v in fields.items()}
    w = Walk(dict(f, **crop_fields()), size)
    w.run(method_body(src, cls))
    return w.ops


def torch_walks(src):
    """the lamp: its torches' places depend on the piece's way (flag): one program each"""
    out = []
    for flag in (False, True):
        w = Walk(crop_fields())
        body = method_body(src, 'Torch')
        stmts = body[1]
        for s in stmts:
            if s[0] == 'decl' and s[1] == 'flag':
                w.vars['flag'] = flag
                continue
            w.run(s)
        out.append(w.ops)
    return out


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    d = sys.argv[1]
    vsrc = open(os.path.join(d, 'StructureVillagePieces.java')).read()
    tsrc = open(os.path.join(d, 'ComponentScatteredFeaturePieces.java')).read()
    progs = []   # (name, bytes)
    for cls, fields in VILLAGE:
        if cls == 'Torch':
            for k, ops in enumerate(torch_walks(vsrc)):
                progs.append((f'Torch{k}', program(ops)))
            continue
        progs.append((cls, program(walk(vsrc, cls, fields))))
    progs.append(('DesertPyramid', program(walk(tsrc, 'DesertPyramid', {}, (21, 15, 21)))))
    keys = [block_key(s) for s in PAL]
    here = os.path.dirname(os.path.abspath(__file__))
    out = ['/* Made by tools/structs.py from Minecraft 1.8.8\'s StructureVillagePieces and ComponentScatteredFeaturePieces:',
           ' * how each piece is built, in its own coordinates (see gen_struct.h). */',
           '/* the blocks: fixed, or stairs (meta as 1.8.8 writes it, turned with the piece), or a torch (turned) */']
    pal_c = []
    for k in keys:
        if k[0] == 'fixed':
            pal_c.append(f'B_{k[1]}')
        elif k[0] == 'stairs':
            pal_c.append(f'VS_STAIRS | VS_{k[1]} << 4 | {k[2]}')
        else:
            pal_c.append(f'VS_TORCH | {k[1]}')
    out.append(f'static const uint16_t vs_pal[{len(pal_c)}] = {{' + ', '.join(pal_c) + '};')
    allb, offs = [], []
    for name, b in progs:
        offs.append((name, len(allb)))
        allb += b
    out.append('enum { ' + ', '.join(f'VSP_{n.upper()} = {o}' for n, o in offs) + ' };')
    lines = []
    for i in range(0, len(allb), 24):
        lines.append('    ' + ','.join(str(v) for v in allb[i:i + 24]) + ',')
    out.append(f'static const uint8_t vs_prog[{len(allb)}] = {{')
    out += lines
    out.append('};')
    path = os.path.join(here, '..', 'src', 'gen_struct_data.h')
    open(path, 'w').write('\n'.join(out) + '\n')
    print(f'structs.py: {len(progs)} programs, {len(allb)} bytes, {len(PAL)} blocks')
    for name, b in progs:
        print(f'  {name}: {len(b)}')


if __name__ == '__main__':
    main()
