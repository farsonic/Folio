#!/usr/bin/env python3
"""
mkfonts.py - makes Folio's font test files and the outlines fontTools expects of them (expected.txt), for
tests/fonttest.c. Needs fontTools and cffsubr. Run from anywhere:  python3 tests/fonts/mkfonts.py [ttfdir]
ttfdir holds LiberationSans-Regular.ttf and Carlito-Regular.ttf; the Type 1 font is Bitstream Charter from X11.
"""
import os, sys, struct, random, shutil, io
from fontTools.ttLib import TTFont, newTable
from fontTools.ttLib.ttCollection import TTCollection
from fontTools.ttLib.sfnt import SFNTWriter
from fontTools.ttLib.tables._g_l_y_f import Glyph, GlyphComponent
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable
from fontTools import subset, cffLib
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.pens.basePen import BasePen
from fontTools.misc.psCharStrings import T2CharString, T1CharString
from fontTools.misc import eexec
from fontTools.t1Lib import T1Font
from fontTools.encodings.StandardEncoding import StandardEncoding

HERE = os.path.dirname(os.path.abspath(__file__))
TTFDIR = sys.argv[1] if len(sys.argv) > 1 else '/tmp/claude-0/-home-user/3a066da9-8230-5cea-817a-b3782336c61e/scratchpad/lpk1/Ledger/TrueType'
CHARTER = '/usr/share/fonts/X11/Type1/c0648bt_.pfb'
out = lambda n: os.path.join(HERE, n)
random.seed(1)

# ---------------------------------------------------------------- outlines as fontTools sees them
class Rec(BasePen):
    """contours as lists of points (start, line ends, curve points), components decomposed, quadratics raised"""
    def __init__(self, gs):
        BasePen.__init__(self, gs); self.contours = []; self.cur = None
    def _moveTo(self, p): self.cur = [p]
    def _lineTo(self, p): self.cur.append(p)
    def _curveToOne(self, a, b, c): self.cur += [a, b, c]
    def _end(self):
        c = self.cur
        if c and len(c) > 1:
            if len(c) > 1 and abs(c[-1][0] - c[0][0]) < 1e-9 and abs(c[-1][1] - c[0][1]) < 1e-9: c = c[:-1]
            self.contours.append(c)
        self.cur = None
    def _closePath(self): self._end()
    def _endPath(self): self._end()

lines = []
def emit(s): lines.append(s)
def emit_glyph(gid, name, adv, contours):
    emit('glyph %d %s %g %d' % (gid, name or '-', adv, len(contours)))
    fmt = lambda v: ('%.3f' % v).rstrip('0').rstrip('.')
    for c in contours: emit('c %d ' % len(c) + ' '.join(fmt(x) + ' ' + fmt(y) for x, y in c))

# ---------------------------------------------------------------- TrueType
def subset_ttf(src, dst, unicodes):
    o = subset.Options(); o.glyph_names = True; o.hinting = False; o.layout_features = []; o.notdef_outline = True
    o.drop_tables += ['GPOS', 'GSUB', 'GDEF', 'kern', 'DSIG', 'gasp', 'hdmx', 'VDMX', 'LTSH', 'vhea', 'vmtx']
    o.name_IDs = [1, 2]; o.desubroutinize = True
    f = TTFont(src); s = subset.Subsetter(o); s.populate(unicodes=unicodes); s.subset(f); f.save(dst)

def long_loca(path):
    """rewrites the font with a long loca (fontTools would pick the short one)"""
    f = TTFont(path); r = f.reader
    raw = {t: r[t] for t in r.keys()}
    loca = f['loca']
    raw['loca'] = b''.join(struct.pack('>I', v) for v in loca.locations)
    head = bytearray(raw['head']); head[50:52] = struct.pack('>h', 1); head[8:12] = b'\0\0\0\0'; raw['head'] = bytes(head)
    with open(path, 'wb') as fp:
        w = SFNTWriter(fp, len(raw), flavor=None)
        for t in sorted(raw): w[t] = raw[t]
        w.close()

def tt_expect(fname, path, names, codes, maps):
    f = TTFont(path); gs = f.getGlyphSet(); glyf = f['glyf']; order = f.getGlyphOrder()
    emit('font %s %d' % (fname, len(order)))
    upm = f['head'].unitsPerEm
    emit('matrix %g 0 0 %g 0 0' % (1.0 / upm, 1.0 / upm))
    for n in names:
        gid = order.index(n); g = glyf[n]
        p = Rec(gs)
        if g.isComposite():
            coords, ends, flags = g.getCoordinates(glyf)
            t = Glyph(); t.numberOfContours = len(ends); t.coordinates = coords; t.endPtsOfContours = ends
            t.flags = flags; t.program = None
            t.draw(p, glyf, f['hmtx'][n][1] - g.xMin)       # (moved to hmtx's left side bearing, as glyph sets do)
        else:
            gs[n].draw(p)
        emit_glyph(gid, n, f['hmtx'][n][0], p.contours)
    cm = f['cmap']
    for (pl, en, code) in codes:
        t = cm.getcmap(pl, en)
        emit('cmap %d %d %d %d' % (pl, en, code, order.index(t.cmap[code]) if t and code in t.cmap else (0 if t else -1)))
    for gid in maps: emit('name %d %s' % (gid, order[gid]))

def make_sans():
    uni = list(range(0x20, 0x7f)) + list(range(0xa0, 0x100)) + [0x106, 0x10c, 0x152, 0x153, 0x160, 0x161, 0x178, 0x17d, 0x17e,
           0x2013, 0x2014, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2026, 0x20ac, 0x2122, 0xfb01, 0xfb02]
    subset_ttf(os.path.join(TTFDIR, 'LiberationSans-Regular.ttf'), out('sans.ttf'), uni)

def make_carlito():
    p = out('carlito.ttf')
    subset_ttf(os.path.join(TTFDIR, 'Carlito-Regular.ttf'), p, list(range(0x20, 0x7f)))
    f = TTFont(p); glyf = f['glyf']; hmtx = f['hmtx']; order = list(f.getGlyphOrder())
    def comp(name, x=0, y=0, tr=None, flags=0, pts=None):
        c = GlyphComponent(); c.glyphName = name; c.flags = flags
        if pts: c.firstPt, c.secondPt = pts
        else: c.x, c.y = x, y
        if tr: c.transform = tr
        return c
    def add(name, comps):
        g = Glyph(); g.numberOfContours = -1; g.components = comps; glyf.glyphs[name] = g
        order.append(name); hmtx[name] = (900, 0)
    nA = len(glyf['A'].getCoordinates(glyf)[0])
    add('cscale', [comp('A', 100, 50, [[0.5, 0], [0, 0.5]])])
    add('cxy', [comp('B', -20, 10, [[0.75, 0], [0, 1.25]])])
    add('c2x2', [comp('C', 30, 40, [[0.8, 0.3], [-0.3, 0.8]], flags=0x800)])
    add('cmatch', [comp('A'), comp('period', pts=(3, 0))])
    add('cnest', [comp('cscale', 0, 0), comp('O', 400, -100), comp('c2x2', 10, 300, [[0.5, 0], [0, 0.5]])])
    glyf.glyphOrder = order; f.setGlyphOrder(order)
    # more cmap subtables: (1,0) format 0, (0,3) format 6, (3,10) format 12 with a code past the BMP
    base = f['cmap'].getcmap(3, 1).cmap
    t0 = CmapSubtable.newSubtable(0); t0.platformID, t0.platEncID, t0.language = 1, 0, 0
    t0.cmap = {c: n for c, n in base.items() if c < 256}
    t6 = CmapSubtable.newSubtable(6); t6.platformID, t6.platEncID, t6.language = 0, 3, 0
    t6.cmap = {c: n for c, n in base.items() if 0x20 <= c <= 0x7e}
    t12 = CmapSubtable.newSubtable(12); t12.platformID, t12.platEncID, t12.language = 3, 10, 0
    t12.cmap = dict(base); t12.cmap.update({0x1F600: 'A', 0xE000: 'cscale', 0xE001: 'cxy', 0xE002: 'c2x2', 0xE003: 'cmatch', 0xE004: 'cnest'})
    f['cmap'].tables = [t for t in f['cmap'].tables if (t.platformID, t.platEncID) not in ((1, 0), (0, 3))] + [t0, t6, t12]
    f['maxp'].numGlyphs = len(order)
    post = f['post']; post.formatType = 2.0; post.extraNames = []; post.mapping = {}
    f.save(p)
    long_loca(p)

def make_ttc():
    c = TTCollection(); c.fonts = [TTFont(out('sans.ttf')), TTFont(out('carlito.ttf'))]; c.save(out('test.ttc'))

# ---------------------------------------------------------------- CFF in OpenType, subroutinized by cffsubr
def t2prog(gs, name):
    pen = T2CharStringPen(None, gs); gs[name].draw(pen)
    return pen.getCharString().program        # (ends with endchar)

def make_otf():
    src = TTFont(out('sans.ttf')); gs = src.getGlyphSet(); order = src.getGlyphOrder(); hm = src['hmtx']
    DEF, NOM = 500, 600
    progs = {}
    for n in order:
        prog = t2prog(gs, n); adv = hm[n][0]
        w = [] if adv == DEF else [adv - NOM]
        progs[n] = w + prog
    # hints: stems, a hintmask with implied vstems, a second hintmask in the middle, a cntrmask
    def hinted(n, extra_mid=True):
        p = progs[n]; body = t2prog(gs, n); w = p[:len(p) - len(body)]
        mv = [i for i, t in enumerate(body) if t in ('rmoveto', 'hmoveto', 'vmoveto')]
        new = w + [0, 20, 680, 20, 'hstemhm', 50, 30, 400, 30, 'hintmask', bytes([0xF0])]
        if extra_mid and len(mv) > 1:
            # (an operator's arguments start after the previous operator)
            k = mv[1]
            while k > 0 and not isinstance(body[k - 1], str): k -= 1
            body = body[:k] + ['hintmask', bytes([0xA0])] + body[k:]
        progs[n] = new + body
    for n in ['H', 'O', 'o', 'e', 'B', 'percent']: hinted(n)
    p = progs['D']; body = t2prog(gs, 'D'); w = p[:len(p) - len(body)]
    progs['D'] = w + [10, 20, 'hstem', 30, 40, 50, 60, 'vstem', 'cntrmask', bytes([0xE0])] + body
    # flex ops
    flex = {
        'flexa': [100, 'hmoveto', 50, 50, 50, 50, 50, 0, 50, 0, 50, -50, 50, -50, 50, 'flex', 0, 300, 'rlineto', 'endchar'],
        'flexb': [100, 100, 'rmoveto', 50, 60, 50, 70, 50, 60, 50, 'hflex', 0, 200, 'rlineto', 'endchar'],
        'flexc': [100, 100, 'rmoveto', 50, 10, 50, 60, 50, 50, 50, -60, 50, 'hflex1', 0, 200, 'rlineto', 'endchar'],
        'flexd': [100, 100, 'rmoveto', 50, 10, 50, 60, 50, 0, 50, 0, 50, -60, 30, 'flex1', -300, 'hlineto', 'endchar'],
    }
    for n, pr in flex.items(): progs[n] = [700 - NOM] + pr; order.append(n)
    # accented characters made by endchar (seac): A + acute, c + cedilla
    progs['Aacute'] = [hm['Aacute'][0] - NOM, 120, 180, 65, 194, 'endchar']
    progs['ccedilla'] = [hm['ccedilla'][0] - NOM, 0, 0, 99, 203, 'endchar']
    fb = FontBuilder(1000, isTTF=False)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(src['cmap'].getBestCmap())
    cs = {n: T2CharString(program=progs[n]) for n in order}
    fb.setupCFF('FolioSans', {'FullName': 'Folio Sans'}, cs, {'defaultWidthX': DEF, 'nominalWidthX': NOM})
    adv = {n: (hm[n][0] if n in hm.metrics else 700, 0) for n in order}
    fb.setupHorizontalMetrics(adv); fb.setupHorizontalHeader(ascent=900, descent=-200)
    fb.setupNameTable({'familyName': 'Folio Sans', 'styleName': 'Regular'}); fb.setupOS2(); fb.setupPost()
    fb.save(out('sans.otf'))
    import cffsubr
    f = TTFont(out('sans.otf')); cffsubr.subroutinize(f, keep_glyph_names=True); f.save(out('sans.otf'))
    f = TTFont(out('sans.otf'))
    open(out('sans.cff'), 'wb').write(f.reader['CFF '])

class CffGS(dict):
    """a glyph set over a CFF's CharStrings, for decomposing seac"""
    def __init__(self, cs):
        dict.__init__(self); self.cs = cs
    def __getitem__(self, n):
        cs = self.cs
        class G:
            def draw(self, pen): cs[n].draw(pen)
        return G()
    def __contains__(self, n): return n in self.cs

def cff_expect(fname, path, names, extra=None):
    data = open(path, 'rb').read()
    if data[:4] == b'OTTO': data = TTFont(path).reader['CFF ']
    fs = cffLib.CFFFontSet(); fs.decompile(io.BytesIO(data), None)
    top = fs[0]; csd = top.CharStrings; order = top.charset
    emit('font %s %d' % (fname, len(order)))
    m = getattr(top, 'FontMatrix', [0.001, 0, 0, 0.001, 0, 0])
    if hasattr(top, 'FDArray') and hasattr(top.FDArray[0], 'FontMatrix') and 'FontMatrix' not in top.rawDict:
        m = top.FDArray[0].FontMatrix
    emit('matrix ' + ' '.join('%g' % v for v in m))
    gs = CffGS(csd)
    for n in names:
        if isinstance(n, int): n = order[n]
        gid = order.index(n)
        p = Rec(gs); csd[n].draw(p)
        emit_glyph(gid, n if not hasattr(top, 'ROS') else None, csd[n].width, p.contours)
    if hasattr(top, 'ROS'):
        emit('cidfont')
        for gid, n in enumerate(order): emit('cid %d %d' % (int(n[3:]) if n.startswith('cid') else 0, gid))
    else:
        for gid in range(0, len(order), 7): emit('name %d %s' % (gid, order[gid]))
        enc = top.rawDict.get('Encoding', 0)
        if enc not in (0, None):
            e = top.Encoding
            for code in range(256):
                nm = e[code] if code < len(e) else '.notdef'
                emit('enc %d %d' % (code, order.index(nm) if nm and nm != '.notdef' and nm in order else -1))
        else: emit('stdenc')
    if extra: extra()

# ---------------------------------------------------------------- hand-made CFFs (charset/encoding/FDSelect formats)
def num(v):
    if isinstance(v, float) and v != int(v):
        s = ('%g' % v).replace('e-', 'c').replace('e', 'b').replace('.', 'a').replace('-', 'e')
        nib = [int(ch, 16) for ch in s] + [0xf]
        if len(nib) & 1: nib.append(0xf)
        return bytes([30] + [nib[i] * 16 + nib[i + 1] for i in range(0, len(nib), 2)])
    return bytes([29]) + struct.pack('>i', int(v))
def op(o): return bytes([12, o - 1200]) if o >= 1200 else bytes([o])
def dict_(ents): return b''.join(b''.join(num(v) for v in vals) + op(o) for o, vals in ents)
def index(items):
    if not items: return b'\0\0'
    offs = [1]
    for it in items: offs.append(offs[-1] + len(it))
    os_ = 1 if offs[-1] < 256 else 2 if offs[-1] < 65536 else 3 if offs[-1] < 1 << 24 else 4
    return struct.pack('>HB', len(items), os_) + b''.join(o.to_bytes(os_, 'big') for o in offs) + b''.join(items)
def t2(prog): c = T2CharString(program=prog); c.compile(); return c.bytecode
def t2num(v):
    if -107 <= v <= 107: return bytes([v + 139])
    if 108 <= v <= 1131: v -= 108; return bytes([247 + (v >> 8), v & 255])
    if -1131 <= v <= -108: v = -v - 108; return bytes([251 + (v >> 8), v & 255])
    return bytes([28]) + struct.pack('>h', v)
def bias(n): return 107 if n < 1240 else 1131 if n < 33900 else 32768

def build_cff(name, glyphs, strings, charset_bytes, cid=None, encoding_bytes=None, priv=None, gsubrs=(), fds=None,
              fdselect_bytes=None, matrix=None):
    """glyphs: charstring bytes; fds: [(private dict entries, local subrs)] for a CID font, else priv likewise"""
    hdr = bytes([1, 0, 4, 4]); nameidx = index([name.encode()]); stridx = index([s.encode() for s in strings])
    gsub = index(list(gsubrs)); csidx = index(glyphs)
    def layout(top_len):
        pos = 4 + len(nameidx) + len(index([b'\0' * top_len])) + len(stridx) + len(gsub)
        off = {}
        off['charset'] = pos; pos += len(charset_bytes)
        if encoding_bytes: off['enc'] = pos; pos += len(encoding_bytes)
        if fdselect_bytes: off['fdsel'] = pos; pos += len(fdselect_bytes)
        off['cs'] = pos; pos += len(csidx)
        return off, pos
    def privs(pos):
        out_ = b''; info = []
        for ents, subrs in (fds if fds else [priv]):
            body = lambda so: dict_(ents + ([(19, [so])] if subrs else []))
            plen = len(body(0)); pd = body(plen)
            info.append((len(pd), pos + len(out_))); out_ += pd + (index(subrs) if subrs else b'')
        return out_, info
    def top(off, fdarray_off, pinfo):
        ents = []
        if cid: ents.append((1230, cid))
        if matrix: ents.append((1207, matrix))
        ents.append((15, [off['charset']]))
        if encoding_bytes: ents.append((16, [off['enc']]))
        ents.append((17, [off['cs']]))
        if fds: ents += [(1236, [fdarray_off]), (1237, [off['fdsel']]), (1234, [len(glyphs)])]
        else: ents.append((18, list(pinfo[0])))
        return dict_(ents)
    tl = len(top({'charset': 0, 'enc': 0, 'cs': 0, 'fdsel': 0}, 0, [(0, 0)]))
    off, pos = layout(tl)
    if fds:
        fdlen = len(index([dict_([(18, [0, 0])]) for _ in fds]))
        pbytes, pinfo = privs(pos + fdlen)
        fdarr = index([dict_([(18, list(pi))] + ([(1207, [1, 0, 0, 1, 0, 0])] if i == 0 else [])) for i, pi in enumerate(pinfo)])
        fdlen2 = len(fdarr)
        assert fdlen2 >= fdlen
        pbytes, pinfo = privs(pos + fdlen2)
        fdarr = index([dict_([(18, list(pi))] + ([(1207, [1, 0, 0, 1, 0, 0])] if i == 0 else [])) for i, pi in enumerate(pinfo)])
        tail = fdarr + pbytes; fdoff = pos
    else:
        pbytes, pinfo = privs(pos); tail = pbytes; fdoff = 0
    topd = top(off, fdoff, pinfo); assert len(topd) == tl
    data = hdr + nameidx + index([topd]) + stridx + gsub + charset_bytes + (encoding_bytes or b'') + (fdselect_bytes or b'') + csidx + tail
    return data

def make_hand_cffs():
    src = TTFont(out('sans.ttf')); gs = src.getGlyphSet(); hm = src['hmtx']
    letters = ['.notdef'] + [chr(c) for c in range(ord('A'), ord('Z') + 1)] + ['acute', 'a', 'b', 'c', 'cedilla']
    body = {n: t2prog(gs, n) for n in letters}
    sid = {s: i for i, s in enumerate(cffLib.cffStandardStrings)}
    # ---- enc.cff: name-keyed, charset format 1 with custom names, encoding format 1 + supplement, subrs, seac, arithmetic
    custom = ['Folio.one', 'Folio.two', 'arith']
    names = letters + ['Aacute', 'ccedilla'] + custom
    strings = custom
    def sidof(n): return sid[n] if n in sid else 391 + strings.index(n)
    gsubrs, lsubrs = [], []
    glyphs = []
    NOM, DEF = 100, 250
    for i, n in enumerate(names):
        adv = hm[n][0] if n in hm.metrics else 640
        w = [] if adv == DEF else [adv - NOM]
        if n in body:
            b = body[n][:-1]                  # (without endchar)
            if n == 'F':                      # hints: a hintmask with implied vstems, and one more before the end
                j = [k for k, t in enumerate(b) if isinstance(t, str)][-2] + 1
                b = [0, 20, 680, 20, 'hstemhm', 50, 30, 400, 30, 'hintmask', bytes([0xF0])] + b[:j] + ['hintmask', bytes([0x50])] + b[j:]
            k = next((j for j in range(1, len(b)) if isinstance(b[j], str) and b[j] not in ('rmoveto', 'hmoveto', 'vmoveto')), None)
            if i % 3 == 1 and k:              # the first drawing operator into a local subr
                lsubrs.append(t2(b[:k + 1] + ['return'])); prog = w + [len(lsubrs) - 1 - 107, 'callsubr'] + b[k + 1:] + ['endchar']
            elif i % 3 == 2 and k:            # the start in a global subr which calls a local one for the rest of the glyph
                lsubrs.append(t2(b[k + 1:] + ['endchar']))
                gsubrs.append(t2(b[:k + 1] + [len(lsubrs) - 1 - 107, 'callsubr', 'return']))
                prog = w + [len(gsubrs) - 1 - 107, 'callgsubr']
            else: prog = w + b + ['endchar']
        elif n == 'Aacute': prog = w + [100, 190, 65, 194, 'endchar']
        elif n == 'ccedilla': prog = w + [10, 0, 99, 203, 'endchar']
        elif n == 'arith': continue
        elif n == 'Folio.one':                # flex (and div)
            prog = w + [100, 'hmoveto', 50, 50, 50, 50, 50, 0, 50, 0, 50, -50, 50, -50, 50, 'flex', 0, 300, 'rlineto', -300, 'hlineto', 'endchar']
        else:                                 # flex1, the last point by the larger (vertical) move
            prog = w + [100, 100, 'rmoveto', 10, 50, 60, 50, 0, 50, 0, 50, -60, 50, 30, 'flex1', -300, 'hlineto', 'endchar']
        glyphs.append(t2(prog))
    # arith: 100 0 rmoveto, then a square made with arithmetic: (50 50 add)=100 hlineto; (30 70 add)=100 vlineto;
    # (300 3 div neg)=-100 hlineto; then -100 via exch/drop/dup/index/roll/put/get/ifelse/mul/sub/abs/sqrt
    def raw(*ops):
        out_ = b''
        for o in ops:
            if isinstance(o, int): out_ += t2num(o)
            else: out_ += bytes([12, o[1]]) if o[0] == 'esc' else bytes([o[1]])
        return out_
    E = lambda c: ('esc', c); O = lambda c: ('op', c)
    arith = (t2([640 - NOM, 100, 0, 'rmoveto']) + raw(50, 50, E(10), O(6)) + raw(30, 70, E(10), O(7)) + raw(300, 3, E(12), E(14), O(6)) +
             raw(7, -100, E(28), E(18)) +                          # exch drop -> -100
             raw(E(27), E(18)) +                                   # dup drop
             raw(5, 0, E(20), 0, E(21)) +                          # put 5 in 0, get it back -> -100 5
             raw(E(18)) +                                          # drop -> -100
             raw(1, 2, 3, 3, 1, E(30), E(18), E(18), E(18)) +      # roll then drop three -> -100
             raw(O(7)) +                                           # vlineto -100
             raw(10, 20, 1, 2, E(22), 2, E(24), 3, E(11), E(9), 289, E(26), E(11), O(22)) +  # ifelse 10, *2 20, -3 17, abs, -sqrt(289): hmoveto 0
             raw(0, 0, E(29)) + raw(O(4)) +                        # 0 0 index -> 0 0; vmoveto 0 (the last)
             raw(O(14)))
    glyphs.insert(names.index('arith'), arith)
    # charset format 1: ranges of SIDs
    sids = [sidof(n) for n in names[1:]]
    ranges = []
    for s in sids:
        if ranges and ranges[-1][0] + ranges[-1][1] + 1 == s and ranges[-1][1] < 255: ranges[-1][1] += 1
        else: ranges.append([s, 0])
    charset = bytes([1]) + b''.join(struct.pack('>HB', a, b) for a, b in ranges)
    # encoding format 1 (+ supplement): A-Z at 65.., the rest after; supplement: 200 -> A, 201 -> Folio.two
    nrest = len(names) - 1 - 26
    enc = bytes([0x81, 2, 65, 25, 97, nrest - 1, 2]) + struct.pack('>BH', 200, sid['A']) + struct.pack('>BH', 201, sidof('Folio.two'))
    priv = ([(20, [DEF]), (21, [NOM])], lsubrs)
    data = build_cff('FolioEnc', glyphs, strings, charset, encoding_bytes=enc, priv=priv, gsubrs=gsubrs, matrix=[0.0005, 0, 0, 0.0005, 0, 0])
    open(out('enc.cff'), 'wb').write(data)
    # ---- enc0.cff: the same glyphs with charset format 0 and encoding format 0 (codes counting down from 250)
    charset0 = bytes([0]) + b''.join(struct.pack('>H', x) for x in sids)
    enc0 = bytes([0, len(names) - 1]) + bytes(250 - g for g in range(len(names) - 1))
    data = build_cff('FolioEnc0', glyphs, strings, charset0, encoding_bytes=enc0, priv=priv, gsubrs=gsubrs)
    open(out('enc0.cff'), 'wb').write(data)
    # ---- the CID-keyed ones: two FDs with their own subrs and widths; (FDSelect 3, charset 2) and (FDSelect 0, charset 0)
    for variant in (3, 0):
        lsub = [[], []]; glyphs = []
        fdof = lambda g: (0 if g < 14 else 1) if variant == 3 else g % 2
        widths = [(500, 600), (250, 100)]
        for g, n in enumerate(letters):
            fd = fdof(g); DEFw, NOMw = widths[fd]
            adv = hm[n][0]; w = [] if adv == DEFw else [adv - NOMw]
            b = body[n][:-1]
            k = next((j for j in range(1, len(b)) if isinstance(b[j], str) and b[j] not in ('rmoveto', 'hmoveto', 'vmoveto')), None)
            if k and g % 2 == 0:
                lsub[fd].append(t2(b[:k + 1] + ['return'])); prog = w + [len(lsub[fd]) - 1 - 107, 'callsubr'] + b[k + 1:] + ['endchar']
            else: prog = w + b + ['endchar']
            glyphs.append(t2(prog))
        if variant == 3:
            cids = [0] + list(range(1, 11)) + list(range(500, 520)) + [9000]
            cids = cids[:len(letters)]
            charset = bytes([2])
            rs = []
            for c in cids[1:]:
                if rs and rs[-1][0] + rs[-1][1] + 1 == c: rs[-1][1] += 1
                else: rs.append([c, 0])
            charset += b''.join(struct.pack('>HH', a, b) for a, b in rs)
            fdsel = bytes([3]) + struct.pack('>H', 2) + struct.pack('>HB', 0, 0) + struct.pack('>HB', 14, 1) + struct.pack('>H', len(letters))
        else:
            cids = [0] + [100 + 3 * g for g in range(1, len(letters))]
            charset = bytes([0]) + b''.join(struct.pack('>H', c) for c in cids[1:])
            fdsel = bytes([0]) + bytes(fdof(g) for g in range(len(letters)))
        fds = [([(20, [widths[0][0]]), (21, [widths[0][1]])], lsub[0]), ([(20, [widths[1][0]]), (21, [widths[1][1]])], lsub[1])]
        data = build_cff('FolioCID', glyphs, ['Adobe', 'Identity'], charset, cid=[391, 392, 0], fds=fds, fdselect_bytes=fdsel,
                         matrix=[0.001, 0, 0, 0.001, 0, 0])
        open(out('cid%d.cff' % variant), 'wb').write(data)

# ---------------------------------------------------------------- hostile fonts: work that must be cut short
def make_hostile():
    # a CFF whose global subrs call each other ten deep, each eight times (8^10 calls, lines at the bottom), and one that
    # calls itself; and a local subr calling itself after drawing 12 lines
    gsubrs = [b''.join(t2num(i + 1 - 107) + bytes([29]) for _ in range(8)) + bytes([11]) for i in range(10)]
    gsubrs.append(t2num(10) + t2num(0) + bytes([5, 11]))
    gsubrs.append(t2num(11 - 107) + bytes([29, 11]))
    lsubrs = [b''.join([t2num(1), t2num(1)] * 12) + bytes([5]) + t2num(-107) + bytes([10, 11])]
    mv = t2([0, 0, 'rmoveto'])[:-1] + bytes([21])
    glyphs = [t2([0, 'endchar']), mv + t2num(-107) + bytes([29, 14]), mv + t2num(11 - 107) + bytes([29, 14]), mv + t2num(-107) + bytes([10, 14])]
    charset = bytes([0]) + b''.join(struct.pack('>H', 391 + i) for i in range(len(glyphs) - 1))
    data = build_cff('Bomb', glyphs, ['b1', 'b2', 'b3'], charset, priv=([], lsubrs), gsubrs=gsubrs)
    open(out('bomb.cff'), 'wb').write(data)
    # a TrueType font whose A is made of 30 copies of itself
    f = TTFont(out('carlito.ttf'), recalcBBoxes=False); glyf = f['glyf']
    gA = Glyph(); gA.numberOfContours = -1; gA.components = []
    for i in range(30):
        c = GlyphComponent(); c.glyphName = 'A'; c.x, c.y = i, 0; c.flags = 0; gA.components.append(c)
    gA.xMin = gA.yMin = 0; gA.xMax = gA.yMax = 100
    glyf.glyphs['A'] = gA
    f.save(out('selfref.ttf'))

# ---------------------------------------------------------------- Type 1
def pfb_parts(path):
    d = open(path, 'rb').read(); parts = []; i = 0
    while i + 2 <= len(d) and d[i] == 0x80 and d[i + 1] in (1, 2):
        n = struct.unpack('<I', d[i + 2:i + 6])[0]; parts.append((d[i + 1], d[i + 6:i + 6 + n])); i += 6 + n
    return parts
def write_pfb(path, clear, binary, trailer):
    with open(path, 'wb') as fp:
        for t, b in ((1, clear), (2, binary), (1, trailer)): fp.write(bytes([0x80, t]) + struct.pack('<I', len(b)) + b)
        fp.write(b'\x80\x03')
def make_charter():
    shutil.copy(CHARTER, out('charter.pfb'))

def pfa_and_t1(pfb, stem):
    """the PFB again as a PFA (hexadecimal) and as a PDF's FontFile (binary straight after eexec)"""
    parts = pfb_parts(pfb)
    clear = parts[0][1]; binary = b''.join(b for t, b in parts if t == 2); trailer = parts[-1][1]
    hexed = b''.join(binary[i:i + 32].hex().encode() + b'\n' for i in range(0, len(binary), 32))
    open(out(stem + '.pfa'), 'wb').write(clear + hexed + trailer)
    open(out(stem + '.t1'), 'wb').write(clear + binary + trailer)

def make_synth():
    """a small Type 1 font with what Charter lacks: a custom Encoding, lenIV 2, flex, div, sbw, hint replacement"""
    def cs(prog):
        c = T1CharString(program=prog); c.compile()
        return eexec.encrypt(b'\x11\x22' + c.bytecode, 4330)[0]
    subrs = [
        [3, 0, 'callothersubr', 'pop', 'pop', 'setcurrentpoint', 'return'],
        [0, 1, 'callothersubr', 'return'],
        [0, 2, 'callothersubr', 'return'],
        ['return'],
        [10, 20, 'hstem', 'return'],
        [100, 200, 'rlineto', 'return'],
    ]
    glyphs = {
        '.notdef': [0, 500, 'hsbw', 'endchar'],
        'square': [50, 600, 'hsbw', 0, 0, 'rmoveto', 500, 'hlineto', 500, 'vlineto', -500, 'hlineto', 'closepath', 'endchar'],
        'curvy': [20, 700, 'hsbw', 100, 300, 'rmoveto', 0, 100, 100, 100, 'vhcurveto', 100, 100, -100, -100, 'hvcurveto',
                  10, -20, 30, -40, 50, -60, 'rrcurveto', 'closepath', 'endchar'],
        'flex': [0, 800, 'hsbw', 100, 100, 'rmoveto', 1, 'callsubr',
                 100, 0, 'rmoveto', 2, 'callsubr', 50, 0, 'rmoveto', 2, 'callsubr', 50, 20, 'rmoveto', 2, 'callsubr',
                 50, 0, 'rmoveto', 2, 'callsubr', 50, -20, 'rmoveto', 2, 'callsubr', 50, 0, 'rmoveto', 2, 'callsubr',
                 50, 0, 'rmoveto', 2, 'callsubr', 50, 500, 100, 0, 'callsubr', 0, 300, 'rlineto', -300, 'hlineto', 'closepath', 'endchar'],
        'divg': [0, 1000, 'hsbw', 1000, 3, 'div', 0, 'rmoveto', 2000, 3, 'div', 'hlineto', 50, 'vlineto', 'closepath', 'endchar'],
        'sbwg': [30, 40, 600, 0, 'sbw', 0, 0, 'rmoveto', 5, 'callsubr', -100, 'hlineto', 'closepath', 'endchar'],
        'hintg': [0, 500, 'hsbw', 4, 1, 3, 'callothersubr', 'pop', 'callsubr', 50, 50, 'rmoveto', 4, 1, 3, 'callothersubr', 'pop',
                  'callsubr', 200, 'hlineto', 200, 'vlineto', 'closepath', 'endchar'],
        'A': [10, 600, 'hsbw', 0, 0, 'rmoveto', 300, 600, 'rlineto', 300, -600, 'rlineto', 'closepath', 'endchar'],
        'acute': [100, 400, 'hsbw', 0, 650, 'rmoveto', 100, 100, 'rlineto', 50, 'hlineto', -100, -100, 'rlineto', 'closepath', 'endchar'],
        'Aacute': [10, 600, 'hsbw', 100, 150, 20, 65, 194, 'seac'],
    }
    order = list(glyphs)
    enc = {65: 'A', 66: 'square', 67: 'curvy', 68: 'flex', 69: 'divg', 70: 'sbwg', 71: 'hintg', 72: 'acute', 0xC1: 'Aacute'}
    clear = (b'%!PS-AdobeFont-1.0: FolioSynth 001\n11 dict begin\n/FontName /FolioSynth def\n/FontType 1 def\n/PaintType 0 def\n'
             b'/FontMatrix [0.0005 0 0 0.0005 0 0] readonly def\n/FontBBox {0 0 1000 1000} readonly def\n'
             b'/FontInfo 2 dict dup begin /FullName (Folio Synth) readonly def end readonly def\n'
             b'/Encoding 256 array\n0 1 255 {1 index exch /.notdef put} for\n' +
             b''.join(b'dup %d /%s put\n' % (c, n.encode()) for c, n in sorted(enc.items())) +
             b'readonly def\ncurrentdict end\ncurrentfile eexec\n')
    priv = (b'dup /Private 8 dict dup begin\n/RD{string currentfile exch readstring pop}executeonly def\n'
            b'/ND{noaccess def}executeonly def\n/NP{noaccess put}executeonly def\n/lenIV 2 def\n/MinFeature{16 16} def\n'
            b'/password 5839 def\n/BlueValues [] def\n/Subrs %d array\n' % len(subrs))
    for i, s in enumerate(subrs):
        b = cs(s); priv += b'dup %d %d RD ' % (i, len(b)) + b + b' NP\n'
    priv += b'ND\n2 index /CharStrings %d dict dup begin\n' % len(order)
    for n in order:
        b = cs(glyphs[n]); priv += b'/%s %d RD ' % (n.encode(), len(b)) + b + b' ND\n'
    priv += b'end\nend\nreadonly put\nnoaccess put\ndup /FontName get exch definefont pop\nmark currentfile closefile\n'
    binary = eexec.encrypt(b'\x01\x02\x03\x04' + priv, 55665)[0]
    trailer = b'\n' + (b'0' * 64 + b'\n') * 8 + b'cleartomark\n'
    write_pfb(out('synth.pfb'), clear, binary, trailer)
    pfa_and_t1(out('synth.pfb'), 'synth')

def t1_expect(fname, path, names):
    f = T1Font(path); f.parse(); gs = f.getGlyphSet(); fd = f.font; csd = fd['CharStrings']
    order = list(csd.keys())
    emit('font %s %d' % (fname, len(order)))
    emit('matrix ' + ' '.join('%g' % v for v in fd['FontMatrix']))
    for n in names:
        p = Rec(gs); gs[n].draw(p)
        c = csd[n]; c.decompile()
        w = c.program[1] if 'hsbw' in c.program else c.program[2]
        if 'sbw' in c.program:        # (fontTools ignores sbw's sidebearing point; the outline starts there)
            p.contours = [[(x + c.program[0], y + c.program[1]) for x, y in ct] for ct in p.contours]
        emit_glyph(order.index(n), n, w, p.contours)
    e = fd['Encoding']
    if e == StandardEncoding or (isinstance(e, list) and e == list(StandardEncoding)): emit('stdenc')
    else:
        for code in range(256):
            emit('enc %d %d' % (code, order.index(e[code]) if e[code] in csd and e[code] != '.notdef' else -1))

def main():
    make_sans(); make_carlito(); make_ttc(); make_otf(); make_hand_cffs(); make_charter(); make_synth(); make_hostile()
    ascii_names = lambda f: [n for n in TTFont(out(f)).getGlyphOrder()]
    sans = ascii_names('sans.ttf')
    sglyf = TTFont(out('sans.ttf'))['glyf']
    tt_expect('sans.ttf', out('sans.ttf'), [n for i, n in enumerate(sans) if i % 4 == 0 or (sglyf[n].isComposite() and i % 3 == 1)],
              [(3, 1, c) for c in (0x20, 0x41, 0x61, 0xe9, 0x20ac, 0xfb01, 0x4e00)] + [(1, 0, 0x41), (1, 0, 0x8e), (3, 0, 0x41), (3, 10, 0x41)],
              list(range(0, len(sans), 5)))
    carl = ascii_names('carlito.ttf')
    tt_expect('carlito.ttf', out('carlito.ttf'), [n for i, n in enumerate(carl) if i % 4 == 0 or n[0] == 'c' and n in
              ('cscale', 'cxy', 'c2x2', 'cmatch', 'cnest')],
              [(3, 1, c) for c in (0x20, 0x41, 0x7a, 0x7e, 0x7f)] + [(1, 0, c) for c in (0x41, 0x61, 0xff)] +
              [(0, 3, c) for c in (0x1f, 0x20, 0x41, 0x7e, 0x7f)] + [(3, 10, c) for c in (0x41, 0xe000, 0xe004, 0x1f600, 0x1f601)],
              list(range(0, len(carl), 9)))
    tt_expect('test.ttc', out('sans.ttf'), sans[:40], [(3, 1, 0x41)], [3])
    order = TTFont(out('sans.otf')).getGlyphOrder()
    cff_expect('sans.otf', out('sans.otf'), [n for i, n in enumerate(order) if i % 4 == 0 or n in
               ('H', 'O', 'o', 'e', 'B', 'D', 'percent', 'Aacute', 'ccedilla', 'flexa', 'flexb', 'flexc', 'flexd')])
    cff_expect('sans.cff', out('sans.cff'), order[:60:4] + ['Aacute', 'ccedilla', 'flexa', 'flexb', 'flexc', 'flexd'])
    cff_expect('enc.cff', out('enc.cff'), list(range(0, 36)))
    emit('glyph 36 arith 640 1')
    emit('c 4 100.0000 0.0000 200.0000 0.0000 200.0000 100.0000 100.0000 100.0000')
    cff_expect('enc0.cff', out('enc0.cff'), list(range(0, 36, 5)))
    cff_expect('cid3.cff', out('cid3.cff'), list(range(32)))
    cff_expect('cid0.cff', out('cid0.cff'), list(range(32)))
    synth = ['.notdef', 'square', 'curvy', 'flex', 'divg', 'sbwg', 'hintg', 'A', 'acute', 'Aacute']
    for fn in ('synth.pfb', 'synth.pfa', 'synth.t1'): t1_expect(fn, out('synth.pfb'), synth)
    f = T1Font(CHARTER); f.parse(); ch = list(f.font['CharStrings'].keys())
    cs = f.font['CharStrings']
    t1_expect('charter.pfb', CHARTER, [n for i, n in enumerate(ch) if i % 5 == 0 or (cs[n].decompile() or 'seac' in cs[n].program) and i % 2])
    open(out('expected.txt'), 'w').write('\n'.join(lines) + '\n')
    for fn in sorted(os.listdir(HERE)): print('%8d %s' % (os.path.getsize(out(fn)), fn))

main()
