#!/usr/bin/env python3
"""Checks a forwarder CIA the way the 3DS does (every hash), independently
of fwdcia.c, and prints its title IDs, name and game path.
usage: verify.py file.cia [banner.ppm]"""
import hashlib, struct, sys

def sha(b): return hashlib.sha256(b).digest()
def a64(x): return (x + 63) & ~63
SIG = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80, 0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}

d = open(sys.argv[1], 'rb').read()
ok = True
def check(name, cond):
    global ok
    print(('  ok   ' if cond else '  BAD  ') + name)
    ok &= bool(cond)

hdr, typ, ver, certs, tick, tmds, metas, cont = struct.unpack_from('<IHHIIIIQ', d, 0)
o_cert = a64(hdr); o_tick = a64(o_cert + certs); o_tmd = a64(o_tick + tick)
o_cont = a64(o_tmd + tmds); o_meta = a64(o_cont + cont)
tmdh = o_tmd + SIG[struct.unpack_from('>I', d, o_tmd)[0]]
tickd = o_tick + SIG[struct.unpack_from('>I', d, o_tick)[0]]
count = struct.unpack_from('>H', d, tmdh + 0x9E)[0]
info = tmdh + 0xC4; chunks = info + 0x900
cid, cidx, ctype, csize = struct.unpack_from('>IHHQ', d, chunks)
ncch = d[o_cont:o_cont + csize]
check('content 0 hash (TMD)', sha(ncch) == d[chunks + 0x10:chunks + 0x30])
icount = struct.unpack_from('>H', d, info + 2)[0]
check('content info record hash', sha(d[chunks:chunks + 0x30 * icount]) == d[info + 4:info + 0x24])
check('content info records hash', sha(d[info:info + 0x900]) == d[tmdh + 0xA4:tmdh + 0xC4])
check('NCCH magic', ncch[0x100:0x104] == b'NCCH')
check('NCCH unencrypted', ncch[0x18F] & 4)
check('exheader hash', sha(ncch[0x200:0x600]) == ncch[0x160:0x180])
eo, es, eh = [x * 0x200 for x in struct.unpack_from('<III', ncch, 0x1A0)]
exefs = ncch[eo:eo + es]
check('ExeFS superblock hash', sha(exefs[:eh]) == ncch[0x1C0:0x1E0])
smdh = None; code = None; bnr = None
for i in range(10):
    name, off, size = struct.unpack_from('<8sII', exefs, i * 16)
    name = name.rstrip(b'\0').decode()
    if not name: continue
    data = exefs[0x200 + off:0x200 + off + size]
    check('ExeFS %-7s hash' % name, sha(data) == exefs[0x200 - (i + 1) * 0x20:0x200 - i * 0x20])
    if name == 'icon': smdh = data
    if name == '.code': code = data
    if name == 'banner': bnr = data
tids = {
    'ticket': struct.unpack_from('>Q', d, tickd + 0x9C)[0],
    'TMD': struct.unpack_from('>Q', d, tmdh + 0x4C)[0],
    'NCCH partition': struct.unpack_from('<Q', ncch, 0x108)[0],
    'NCCH program': struct.unpack_from('<Q', ncch, 0x118)[0],
    'exheader ACI': struct.unpack_from('<Q', ncch, 0x200 + 0x200)[0],
}
for k, v in tids.items(): print('  %-15s %016X' % (k, v))
check('title IDs all the same', len(set(tids.values())) == 1)
check('no template title ID left', struct.pack('<Q', 0x000400000F3D0000) not in d and struct.pack('>Q', 0x000400000F3D0000) not in d)
if smdh:
    check('SMDH magic', smdh[:4] == b'SMDH')
    print('  name: %r' % smdh[8:0x88].decode('utf-16-le').rstrip('\0'))
    if metas >= 0x400 + 0x36C0:
        check('meta SMDH = ExeFS icon', d[o_meta + 0x400:o_meta + 0x400 + 0x36C0] == smdh)
if code:
    i = code.find(b'GP3D-FWD-SLOT-1\0')
    check('forwarder slot', i >= 0)
    if i >= 0:
        tgt = struct.unpack_from('<Q', code, i + 16)[0]
        launch = code[i + 24:i + 152].split(b'\0')[0].decode()
        path = code[i + 152:i + 664].split(b'\0')[0].decode()
        print('  target %016X  launch %r  path %r' % (tgt, launch, path))

def lz11(c):
    n = c[1] | c[2] << 8 | c[3] << 16; i = 4; o = bytearray()
    while len(o) < n:
        f = c[i]; i += 1
        for b in range(8):
            if len(o) >= n: break
            if f & (0x80 >> b):
                t = c[i] >> 4
                if t == 0: ln = (((c[i] & 15) << 4) | (c[i+1] >> 4)) + 0x11; dist = (((c[i+1] & 15) << 8) | c[i+2]) + 1; i += 3
                elif t == 1: ln = (((c[i] & 15) << 12) | (c[i+1] << 4) | (c[i+2] >> 4)) + 0x111; dist = (((c[i+2] & 15) << 8) | c[i+3]) + 1; i += 4
                else: ln = t + 1; dist = (((c[i] & 15) << 8) | c[i+1]) + 1; i += 2
                for _ in range(ln): o.append(o[-dist])
            else: o.append(c[i]); i += 1
    return bytes(o), i
def zo(x, y):
    return sum((((x >> b) & 1) << (2*b)) | (((y >> b) & 1) << (2*b+1)) for b in range(3))
if bnr:
    check('banner CBMD', bnr[:4] == b'CBMD')
    cg, cw = struct.unpack_from('<I', bnr, 8)[0], struct.unpack_from('<I', bnr, 0x84)[0]
    g, used = lz11(bnr[cg:cw])
    check('banner model fits before the sound', cg + used <= cw)
    check('banner CGFX', g[:4] == b'CGFX')
    t = g.find(b'TXOB')
    while t >= 0 and struct.unpack_from('<I', g, t + 0x18)[0] != 256: t = g.find(b'TXOB', t + 4)
    check('banner 256x128 texture', t >= 0)
    if len(sys.argv) > 2 and t >= 0:
        off = t + 0x44 + struct.unpack_from('<I', g, t + 0x44)[0]
        rgb = bytearray(256 * 128 * 3)
        for y in range(128):
            for x in range(256):
                v = struct.unpack_from('<H', g, off + (((y // 8) * 32 + x // 8) * 64 + zo(x & 7, y & 7)) * 2)[0]
                rgb[(y * 256 + x) * 3:(y * 256 + x) * 3 + 3] = bytes(((v >> 12) * 17, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17))
        open(sys.argv[2], 'wb').write(b'P6 256 128 255\n' + bytes(rgb))
        print('  banner picture -> %s' % sys.argv[2])
print('ALL OK' if ok else 'PROBLEMS')
sys.exit(0 if ok else 1)
