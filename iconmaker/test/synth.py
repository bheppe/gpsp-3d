#!/usr/bin/env python3
"""A small, valid stand-in for the real template CIA (built by makerom on a
PC), for testing fwdcia without devkitPro. usage: synth.py out.cia"""
import hashlib, struct, sys
TID = 0x000400000F3D0000
def sha(b): return hashlib.sha256(b).digest()
def pad(b, n): return b + b'\0' * (-len(b) % n)
sig = struct.pack('>I', 0x10004) + b'\0' * (0x100 + 0x3C)
# ticket
tdata = bytearray(0x210); tdata[0x9C:0xA4] = struct.pack('>Q', TID)
ticket = sig + bytes(tdata)
# SMDH
smdh = bytearray(0x36C0); smdh[:4] = b'SMDH'
name = 'Template'.encode('utf-16-le'); smdh[8:8 + len(name)] = name
# .code with the slot in its data
slot = b'GP3D-FWD-SLOT-1\0' + b'\0' * (664 - 16)
code = b'\xAA' * 0x1000 + slot + b'\xBB' * 0x200
# ExeFS
files = [('.code', code), ('icon', bytes(smdh)), ('banner', b'CBMD' + b'\0' * 0x1FC)]
hdr = bytearray(0x200); body = b''
for i, (n, data) in enumerate(files):
    struct.pack_into('<8sII', hdr, i * 16, n.encode(), len(body), len(data))
    hdr[0x200 - (i + 1) * 0x20:0x200 - i * 0x20] = sha(data)
    body = pad(body + data, 0x200)
exefs = bytes(hdr) + body
# exheader (program ID in the ACI and the access descriptor)
exh = bytearray(0x800); exh[0x1C8:0x1D0] = struct.pack('<Q', TID)
exh[0x200:0x208] = struct.pack('<Q', TID); exh[0x600:0x608] = struct.pack('<Q', TID)
# NCCH
nh = bytearray(0x200); nh[0x100:0x104] = b'NCCH'
struct.pack_into('<Q', nh, 0x108, TID); struct.pack_into('<Q', nh, 0x118, TID)
nh[0x150:0x160] = b'CTR-P-GP3F\0\0\0\0\0\0'
nh[0x160:0x180] = sha(bytes(exh[:0x400])); struct.pack_into('<I', nh, 0x180, 0x400)
nh[0x18F] = 4
eo = (0x200 + 0x800) // 0x200
struct.pack_into('<III', nh, 0x1A0, eo, len(exefs) // 0x200, 1)
nh[0x1C0:0x1E0] = sha(exefs[:0x200])
ncch = bytes(nh) + bytes(exh) + exefs
struct.pack_into('<I', nh, 0x104, len(ncch) // 0x200); ncch = bytes(nh) + bytes(exh) + exefs
# TMD
th = bytearray(0xC4); th[0x4C:0x54] = struct.pack('>Q', TID); struct.pack_into('>H', th, 0x9E, 1)
chunk = struct.pack('>IHHQ', 0, 0, 0, len(ncch)) + sha(ncch)
info = bytearray(0x900); struct.pack_into('>HH', info, 0, 0, 1); info[4:0x24] = sha(chunk)
th[0xA4:0xC4] = sha(bytes(info))
tmd = sig + bytes(th) + bytes(info) + chunk
# meta with an SMDH copy
meta = bytearray(0x3AC0); meta[0x400:0x400 + 0x36C0] = smdh
certs = b'\0' * 0xA00
h = bytearray(0x2020)
struct.pack_into('<IHHIIIIQ', h, 0, 0x2020, 0, 0, len(certs), len(ticket), len(tmd), len(meta), len(ncch))
h[0x20] = 0x80
out = pad(bytes(h), 64) + pad(certs, 64) + pad(ticket, 64) + pad(tmd, 64) + pad(ncch, 64) + bytes(meta)
open(sys.argv[1], 'wb').write(out)
