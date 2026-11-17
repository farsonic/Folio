#!/usr/bin/env python3
"""filtertest.py - test vectors for core/filters.c and core/crypto.c.

Usage: python3 tests/filtertest.py ./filtertest

Writes vectors (zlib at every level and strategy, LZW with both EarlyChange values plus libtiff's LZW, PNG and TIFF
predictors, CCITT G3/G4 strips from Pillow/libtiff, hashes, RC4 and AES from hashlib/cryptography) into a temporary
directory with a manifest, runs the C test on it, then checks that what flt_deflate wrote decodes with Python's zlib.
"""
import hashlib
import io
import os
import random
import subprocess
import sys
import tempfile
import zlib

rnd = random.Random(1234)
man = []


def put(d, name, **files):
    for ext, data in files.items():
        with open(os.path.join(d, name + '.' + ext), 'wb') as f:
            f.write(data)


def datasets():
    words = b'the quick brown fox jumps over a lazy dog PDF stream content BT /F1 12 Tf 72 712 Td (Hello) Tj ET q Q re f'.split()
    text = b' '.join(rnd.choice(words) for _ in range(40000))
    content = b''.join(b'%d %d m %d %d l S\n' % (rnd.randrange(600), rnd.randrange(800), rnd.randrange(600), rnd.randrange(800))
                       for _ in range(20000))
    runs = b''.join(bytes([rnd.randrange(4)]) * rnd.randrange(1, 300) for _ in range(3000))
    rand = bytes(rnd.randrange(256) for _ in range(60000))
    img = bytes((x * y // 7 + (x ^ y)) & 255 for y in range(200) for x in range(300))
    big = (text + content + runs + rand) * 4
    return {'empty': b'', 'one': b'A', 'two': b'\x00\xff', 'text': text, 'content': content, 'runs': runs, 'rand': rand,
            'img': img, 'zeros': bytes(100000), 'big': big[:3000000]}


def gen_flate(d, ds):
    for nm, data in ds.items():
        for lvl in range(0, 10):
            if nm == 'big' and lvl not in (1, 6, 9):
                continue
            n = 'fl_%s_%d' % (nm, lvl)
            put(d, n, **{'in': zlib.compress(data, lvl), 'out': data})
            man.append('flate ' + n)
        for strat, sn in ((zlib.Z_FIXED, 'fixed'), (zlib.Z_HUFFMAN_ONLY, 'huff'), (zlib.Z_RLE, 'rle'), (zlib.Z_FILTERED, 'filt')):
            c = zlib.compressobj(6, zlib.DEFLATED, 15, 8, strat)
            put(d, 'fl_%s_%s' % (nm, sn), **{'in': c.compress(data) + c.flush(), 'out': data})
            man.append('flate fl_%s_%s' % (nm, sn))
        c = zlib.compressobj(6, zlib.DEFLATED, -15)
        put(d, 'fl_%s_raw' % nm, **{'in': c.compress(data) + c.flush(), 'out': data})
        man.append('flate fl_%s_raw' % nm)
        c = zlib.compressobj(9, zlib.DEFLATED, 9, 1)
        put(d, 'fl_%s_w9' % nm, **{'in': c.compress(data) + c.flush(), 'out': data})
        man.append('flate fl_%s_w9' % nm)
        # many blocks: full flushes between chunks (empty stored blocks too)
        c = zlib.compressobj(5)
        out = b''
        for i in range(0, len(data), 7000):
            out += c.compress(data[i:i + 7000]) + c.flush(zlib.Z_FULL_FLUSH)
        out += c.flush()
        put(d, 'fl_%s_flush' % nm, **{'in': out, 'out': data})
        man.append('flate fl_%s_flush' % nm)
        if nm != 'big':
            put(d, 'df_' + nm, **{'in': data})
            man.append('deflate df_' + nm)
    put(d, 'df_big', **{'in': ds['big']})
    man.append('deflate df_big')


def lzw_encode(data, early):
    out, acc, nacc = bytearray(), 0, 0

    def emit(code, width):
        nonlocal acc, nacc
        acc = (acc << width) | code
        nacc += width
        while nacc >= 8:
            out.append((acc >> (nacc - 8)) & 255)
            nacc -= 8
    width, table, nxt = 9, {bytes([i]): i for i in range(256)}, 258
    emit(256, width)
    w = b''
    for c in data:
        wc = w + bytes([c])
        if wc in table:
            w = wc
            continue
        emit(table[w], width)
        table[wc] = nxt
        nxt += 1
        if nxt - 1 + early >= (1 << width) and width < 12:
            width += 1
        if nxt == 4096:
            emit(256, width)
            width, table, nxt = 9, {bytes([i]): i for i in range(256)}, 258
        w = bytes([c])
    if w:
        emit(table[w], width)
        nxt += 1
        if nxt - 1 + early >= (1 << width) and width < 12:
            width += 1
    emit(257, width)
    if nacc:
        out.append((acc << (8 - nacc)) & 255)
    return bytes(out)


def gen_lzw(d, ds):
    for nm in ('empty', 'one', 'two', 'text', 'runs', 'rand', 'img', 'zeros'):
        for early in (0, 1):
            n = 'lzw_%s_%d' % (nm, early)
            put(d, n, **{'in': lzw_encode(ds[nm], early), 'out': ds[nm]})
            man.append('lzw %s %d' % (n, early))
    # libtiff's LZW (TIFF LZW is PDF's with EarlyChange 1)
    from PIL import Image, TiffImagePlugin
    for w, h, seed in ((300, 200, 1), (1000, 300, 2), (17, 5, 3)):
        r = random.Random(seed)
        raw = bytes((x // 9 + y // 5 + (r.randrange(3) if seed != 3 else 0)) & 255 for y in range(h) for x in range(w))
        im = Image.frombytes('L', (w, h), raw)
        b = io.BytesIO()
        ti = TiffImagePlugin.ImageFileDirectory_v2()
        ti[278] = h
        im.save(b, 'TIFF', compression='tiff_lzw', tiffinfo=ti)
        t = Image.open(io.BytesIO(b.getvalue())).tag_v2
        strip = b.getvalue()[t[273][0]:t[273][0] + t[279][0]]
        put(d, 'lzwtiff_%d' % seed, **{'in': strip, 'out': raw})
        man.append('lzw lzwtiff_%d 1' % seed)


def png_filter(raw, colors, bpc, columns):
    bpp = max(1, (colors * bpc + 7) // 8)
    rb = (colors * bpc * columns + 7) // 8
    out, prev = bytearray(), bytes(rb)
    for r in range(len(raw) // rb):
        row = raw[r * rb:(r + 1) * rb]
        ft = r % 5 if r < 10 else rnd.randrange(5)
        f = bytearray([ft])
        for i in range(rb):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 0: p = 0
            elif ft == 1: p = a
            elif ft == 2: p = b
            elif ft == 3: p = (a + b) // 2
            else:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                p = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            f.append((row[i] - p) & 255)
        out += f
        prev = row
    return bytes(out)


def tiff_diff(raw, colors, bpc, columns):
    rb = (colors * bpc * columns + 7) // 8
    out = bytearray()
    for r in range(len(raw) // rb):
        row = raw[r * rb:(r + 1) * rb]
        ns = colors * columns
        bits = int.from_bytes(row, 'big')
        tot = rb * 8
        samples = [(bits >> (tot - (i + 1) * bpc)) & ((1 << bpc) - 1) for i in range(ns)]
        diff = [samples[i] if i < colors else (samples[i] - samples[i - colors]) & ((1 << bpc) - 1) for i in range(ns)]
        v = 0
        for s in diff:
            v = (v << bpc) | s
        v <<= tot - ns * bpc
        v |= bits & ((1 << (tot - ns * bpc)) - 1)   # keep the padding bits
        out += v.to_bytes(rb, 'big')
    return bytes(out)


def gen_predict(d):
    k = 0
    for colors, bpc, columns, rows in ((1, 8, 50, 30), (3, 8, 33, 20), (4, 8, 17, 12), (1, 16, 21, 9), (3, 16, 10, 15),
                                       (1, 1, 77, 13), (1, 2, 31, 11), (1, 4, 9, 14), (2, 4, 13, 8), (3, 2, 7, 6), (32, 8, 3, 5)):
        rb = (colors * bpc * columns + 7) // 8
        raw = bytes((rnd.randrange(256) if rnd.random() < 0.3 else (i * 7 // 3) & 255) for i in range(rb * rows))
        for pred in (10, 12, 15):
            n = 'png_%d' % k
            k += 1
            put(d, n, **{'in': zlib.compress(png_filter(raw, colors, bpc, columns)), 'out': raw})
            man.append('predict %s %d %d %d %d' % (n, pred, colors, bpc, columns))
        n = 'tiff_%d' % k
        k += 1
        put(d, n, **{'in': zlib.compress(tiff_diff(raw, colors, bpc, columns)), 'out': raw})
        man.append('predict %s 2 %d %d %d' % (n, colors, bpc, columns))


def gen_ccitt(d):
    from PIL import Image, ImageDraw, TiffImagePlugin
    k = 0
    for w, h, kind in ((100, 20, 'box'), (1728, 60, 'text'), (1, 9, 'noise'), (7, 7, 'noise'), (64, 64, 'noise'),
                       (200, 150, 'text'), (2000, 40, 'lines'), (2600, 12, 'lines'), (333, 80, 'text'), (50, 50, 'black')):
        im = Image.new('1', (w, h), 1)
        dr = ImageDraw.Draw(im)
        r = random.Random(w * 31 + h)
        if kind == 'box':
            dr.rectangle((10, 5, 40, 12), fill=0)
        elif kind == 'text':
            for y in range(0, h, 14):
                dr.text((r.randrange(10), y), ''.join(chr(r.randrange(33, 127)) for _ in range(w // 6)), fill=0)
        elif kind == 'noise':
            for y in range(h):
                for x in range(w):
                    if r.random() < 0.4:
                        im.putpixel((x, y), 0)
        elif kind == 'lines':
            for i in range(30):
                dr.line((r.randrange(w), r.randrange(h), r.randrange(w), r.randrange(h)), fill=0, width=r.randrange(1, 4))
            dr.rectangle((w - 1900, 3, w - 5, 8), fill=0)
        else:
            dr.rectangle((0, 0, w, h), fill=0)
        for inv in (0, 1):
            img = im if not inv else im.point(lambda v: 255 - v)
            expect = img.tobytes()   # 1 = Pillow white = what libtiff codes as fax black (photometric MinIsBlack)
            for comp, t4, K, eols in (('group4', None, -1, (0,)), ('group3', None, 0, (0, 1)), ('group3', 1, 2, (0, 1)),
                                      ('group3', 5, 4, (0, 1)), ('group3', 4, 0, (0, 1))):
                b = io.BytesIO()
                ti = TiffImagePlugin.ImageFileDirectory_v2()
                ti[278] = h
                if t4 is not None:
                    ti[292] = t4
                img.save(b, 'TIFF', compression=comp, tiffinfo=ti)
                t = Image.open(io.BytesIO(b.getvalue())).tag_v2
                strip = b.getvalue()[t[273][0]:t[273][0] + t[279][0]]
                n = 'fax_%d' % k
                k += 1
                put(d, n, **{'in': strip, 'out': expect})
                for eol in eols:
                    for rows in (h, 0):
                        man.append('ccitt %s %d %d %d %d %d %d %d' % (n, K, w, rows, 1, 0, eol, 1))
                    if t4 == 5:   # fill bits: lines start on byte boundaries
                        man.append('ccitt %s %d %d %d %d %d %d %d' % (n, K, w, h, 1, 1, eol, 1))


def rc4(key, data):
    s = list(range(256))
    j = 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) & 255
        s[i], s[j] = s[j], s[i]
    i = j = 0
    out = bytearray()
    for c in data:
        i = (i + 1) & 255
        j = (j + s[i]) & 255
        s[i], s[j] = s[j], s[i]
        out.append(c ^ s[(s[i] + s[j]) & 255])
    return bytes(out)


def gen_crypto(d):
    lens = list(range(0, 260)) + [1000, 4095, 4096, 65537, 300001]
    for i, n in enumerate(lens):
        m = os.urandom(n)
        for h in ('md5', 'sha256', 'sha384', 'sha512'):
            put(d, 'h_%s_%d' % (h, i), **{'in': m, 'out': hashlib.new(h, m).digest()})
            man.append('%s h_%s_%d' % (h, h, i))
    for i in range(40):
        key = os.urandom(rnd.choice((1, 5, 16, 32, 256)))
        m = os.urandom(rnd.randrange(0, 3000))
        put(d, 'rc4_%d' % i, key=key, **{'in': m, 'out': rc4(key, m)})
        man.append('rc4 rc4_%d' % i)
    try:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    except ImportError:
        print('cryptography not available: AES cross-checks skipped')
        return
    for i in range(60):
        kl = (16, 24, 32)[i % 3]
        key, iv = os.urandom(kl), os.urandom(16)
        m = os.urandom(rnd.randrange(0, 2000) if i else 0)
        p = 16 - len(m) % 16
        enc = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
        ct = enc.update(m + bytes([p]) * p) + enc.finalize()
        put(d, 'aes_%d' % i, key=key, **{'in': iv + ct, 'out': m})
        man.append('aescbc aes_%d %d' % (i, kl))
        m2 = os.urandom(16 * rnd.randrange(0, 100))
        enc = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
        put(d, 'aesenc_%d' % i, key=key, iv=iv, **{'in': m2, 'out': enc.update(m2) + enc.finalize()})
        man.append('aesenc aesenc_%d %d' % (i, kl))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    exe = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory() as d:
        ds = datasets()
        gen_flate(d, ds)
        gen_lzw(d, ds)
        gen_predict(d)
        gen_ccitt(d)
        gen_crypto(d)
        with open(os.path.join(d, 'manifest.txt'), 'w') as f:
            f.write('\n'.join(man) + '\n')
        print('%d vectors written' % len(man))
        rc = subprocess.call([exe, d])
        # flt_deflate's output must decode with zlib
        bad = good = 0
        for fn in sorted(os.listdir(d)):
            if not fn.endswith('.def'):
                continue
            base, lvl = fn[:-4].rsplit('.', 1)
            orig = open(os.path.join(d, base + '.in'), 'rb').read()
            comp = open(os.path.join(d, fn), 'rb').read()
            try:
                ok = zlib.decompress(comp) == orig
            except zlib.error as e:
                ok = False
                print('zlib error on %s: %s' % (fn, e))
            if ok:
                good += 1
            else:
                bad += 1
                print('FAIL: zlib does not decode %s back' % fn)
        print('flt_deflate output checked with Python zlib: %d ok, %d bad' % (good, bad))
        sys.exit(1 if rc or bad or not good else 0)


if __name__ == '__main__':
    main()
