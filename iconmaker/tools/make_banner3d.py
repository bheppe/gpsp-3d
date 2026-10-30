#!/usr/bin/env python3
"""Makes Icon Maker's 3D banner template, res/banner3d.cgfx (and a version
without animation, banner3d-still.cgfx), its jingle, res/jingle.wav, and the
3D banners of the apps themselves.

The HOME Menu turns a 3D banner's whole model about the vertical axis. The
model is a tile with the game's icon on both faces, bobbing a little, and
under it the game's name on a strip that always faces the camera. Icon Maker
swaps in two pictures for each game:
  LABEL  128x128 RGBA4  both faces of the tile: the game's icon, corners
                        cut round by transparency
  NAME   256x64  RGBA4  the name strip (transparent around the text)
Both start as noise, so the compressed model always has room for any picture.

  python3 tools/make_banner3d.py --jingle          only res/jingle.wav
  python3 tools/make_banner3d.py /path/to/pycgfx   the models, then the 3D
      banners of gpSP 3D, Icon Maker and (if it's next to this repo, as
      snes9x_3ds-pr2) Snes9x 3D, from their banner3d-name.png (256x64) and
      banner3d-label.png (128x128)

The model needs pycgfx (https://github.com/skyfloogle/pycgfx, glTF to CGFX),
gltflib and Pillow. The camera matches pycgfx's banner-camera.gltf: at
(0, 1, 44.786) with a 30 degree vertical field of view.
"""
import base64, io, json, math, os, random, struct, sys, tempfile, wave

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "res")

# ---- layout (the HOME Menu shows about y -8..9; it turns the model about x = 0) ----
TILE_W, TILE_D = 9.5, 0.4                  # the tile: face, depth
CORNER, CORNER_SEG = 0.2, 6                # corner radius (of the width), points per corner
TILE_Y = 3.6                               # its centre
NAME_W, NAME_Y = 26.0, -7.9                # the name strip: width, bottom edge (height = width / 4)
BOB = 0.4                                  # how far the tile bobs
SECONDS = 4.0                              # one bob, looped


def noise_png(w, h):
    from PIL import Image
    rnd = random.Random(w * 1000 + h)
    im = Image.frombytes("RGBA", (w, h), bytes(rnd.randrange(256) for _ in range(w * h * 4)))
    b = io.BytesIO()
    im.save(b, "PNG")
    return b.getvalue()


def solid_png(w, h, rgb):
    from PIL import Image
    b = io.BytesIO()
    Image.new("RGBA", (w, h), rgb + (255,)).save(b, "PNG")
    return b.getvalue()


class Builder:
    def __init__(self):
        self.buf = bytearray()
        self.views, self.accessors = [], []

    def add(self, data, comp, typ, count, target=None, minmax=None):
        while len(self.buf) % 4:
            self.buf.append(0)
        view = {"buffer": 0, "byteOffset": len(self.buf), "byteLength": len(data)}
        if target:
            view["target"] = target
        self.buf += data
        self.views.append(view)
        acc = {"bufferView": len(self.views) - 1, "componentType": comp, "count": count, "type": typ}
        if minmax:
            acc["min"], acc["max"] = minmax
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def prim_raw(self, pos, nrm, uvs, idx, material):
        p = self.add(b"".join(struct.pack("<3f", *v) for v in pos), 5126, "VEC3", len(pos), 34962,
                     ([min(v[i] for v in pos) for i in range(3)], [max(v[i] for v in pos) for i in range(3)]))
        n = self.add(b"".join(struct.pack("<3f", *v) for v in nrm), 5126, "VEC3", len(nrm), 34962)
        t = self.add(b"".join(struct.pack("<2f", *v) for v in uvs), 5126, "VEC2", len(uvs), 34962)
        i = self.add(b"".join(struct.pack("<H", v) for v in idx), 5123, "SCALAR", len(idx), 34963)
        return {"attributes": {"POSITION": p, "NORMAL": n, "TEXCOORD_0": t}, "indices": i, "material": material}

    def prim(self, quads, material, uv=True):
        pos, nrm, uvs, idx = [], [], [], []
        for verts, n, *t in quads:
            base = len(pos)
            pos += verts
            nrm += [n] * 4
            uvs += t[0] if t else [(0, 0)] * 4
            idx += [base, base + 1, base + 2, base, base + 2, base + 3]
        p = self.add(b"".join(struct.pack("<3f", *v) for v in pos), 5126, "VEC3", len(pos), 34962,
                     ([min(v[i] for v in pos) for i in range(3)], [max(v[i] for v in pos) for i in range(3)]))
        n = self.add(b"".join(struct.pack("<3f", *v) for v in nrm), 5126, "VEC3", len(nrm), 34962)
        a = {"POSITION": p, "NORMAL": n}
        if uv:
            a["TEXCOORD_0"] = self.add(b"".join(struct.pack("<2f", *v) for v in uvs), 5126, "VEC2",
                                       len(uvs), 34962)
        i = self.add(b"".join(struct.pack("<H", v) for v in idx), 5123, "SCALAR", len(idx), 34963)
        return {"attributes": a, "indices": i, "material": material}


def _outline():
    """The tile's outline, counter-clockwise from the right: a square with
    rounded corners (CORNER_SEG points per corner)."""
    h, r, pts = TILE_W / 2, TILE_W * CORNER, []
    for cx, cy, a0 in ((h - r, h - r, 0), (-h + r, h - r, 90), (-h + r, -h + r, 180), (h - r, -h + r, 270)):
        for i in range(CORNER_SEG + 1):
            a = math.radians(a0 + 90 * i / CORNER_SEG)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def rounded_faces(b, material):
    """Front and back of the tile: each a fan of triangles from its centre,
    sharing vertices; the icon fills the square, the corners are cut round.
    The back is mirrored so the icon reads from behind too."""
    pts, z = _outline(), TILE_D / 2
    pos, nrm, uvs, idx = [], [], [], []
    for side, nz in ((0, 1), (1, -1)):
        base = len(pos)
        for x, y in [(0, 0)] + pts:
            pos.append((x, y, z * nz))
            nrm.append((0, 0, nz))
            uvs.append(((-x if side else x) / TILE_W + 0.5, 0.5 - y / TILE_W))
        n = len(pts)
        for i in range(n):
            a, c = base + 1 + i, base + 1 + (i + 1) % n
            idx += [base, a, c] if nz > 0 else [base, c, a]
    return b.prim_raw(pos, nrm, uvs, idx, material)


def rounded_sides(b, material):
    """The rim: one band of quads around the outline, normals pointing out."""
    pts, z = _outline(), TILE_D / 2
    pos, nrm, uvs, idx = [], [], [], []
    n = len(pts)
    for x, y in pts:
        l = math.hypot(x, y) or 1
        for zz in (-z, z):
            pos.append((x, y, zz))
            nrm.append((x / l, y / l, 0))
            uvs.append((0.5, 0.5))
    for i in range(n):
        a0, a1 = 2 * i, 2 * ((i + 1) % n)
        idx += [a0, a1, a1 + 1, a0, a1 + 1, a0 + 1]
    return b.prim_raw(pos, nrm, uvs, idx, material)


def square_tile(b):
    """The box that works on hardware. The two faces carry the icon, its
    corners cut round by transparency. The sides run only along the straight
    part of each edge, where the outline is, and stop where the round corner
    starts, so no square corner shows when the tile turns. They're dark, so
    the edge reads as a thin rim. (Without sides at all it froze.)"""
    x, y, z = TILE_W / 2, TILE_W / 2, TILE_D / 2
    front = ([(-x, -y, z), (x, -y, z), (x, y, z), (-x, y, z)], (0, 0, 1), [(0, 1), (1, 1), (1, 0), (0, 0)])
    rear = ([(x, -y, -z), (-x, -y, -z), (-x, y, -z), (x, y, -z)], (0, 0, -1), [(0, 1), (1, 1), (1, 0), (0, 0)])
    e = x - TILE_W * CORNER                    # where the straight part of an edge ends
    sides = [
        ([(x, -e, z), (x, -e, -z), (x, e, -z), (x, e, z)], (1, 0, 0)),
        ([(-x, -e, -z), (-x, -e, z), (-x, e, z), (-x, e, -z)], (-1, 0, 0)),
        ([(-e, y, z), (e, y, z), (e, y, -z), (-e, y, -z)], (0, 1, 0)),
        ([(-e, -y, -z), (e, -y, -z), (e, -y, z), (-e, -y, z)], (0, -1, 0)),
    ]
    return b.prim([front, rear], 0), b.prim(sides, 1)


def make_gltf(animated=True, rounded=False):
    """The HOME Menu turns a 3D banner's whole model about the vertical axis
    (and shows roughly y -8..9 of it). So: a tile with the game's icon on both
    faces, centred on that axis, bobbing a little; and under it the name, on a
    strip that always faces the camera (a Y-axis billboard, set after
    conversion), so it stays readable while the tile turns."""
    b = Builder()
    if rounded:
        faces, body = rounded_faces(b, 0), rounded_sides(b, 1)
    else:
        faces, body = square_tile(b)
    # (the body is textured too: a material without one crashes the HOME Menu)
    # the name strip: 4:1 like its 256x64 picture, its place baked into the vertices
    nx0, nx1, ny0, ny1 = -NAME_W / 2, NAME_W / 2, NAME_Y, NAME_Y + NAME_W / 4
    name = b.prim([([(nx0, ny0, 0), (nx1, ny0, 0), (nx1, ny1, 0), (nx0, ny1, 0)], (0, 0, 1),
                    [(0, 1), (1, 1), (1, 0), (0, 0)])], 2)

    # the bob: up and down, looped; every channel of the transform given
    # (rotation and scale held), like Blender's exports that work on hardware
    n = 33
    times = [SECONDS * i / (n - 1) for i in range(n)]
    trans = [(0.0, TILE_Y + BOB * math.sin(2 * math.pi * t / SECONDS), 0.0) for t in times]
    t_acc = b.add(b"".join(struct.pack("<f", t) for t in times), 5126, "SCALAR", n, None, ([0.0], [SECONDS]))
    p_acc = b.add(b"".join(struct.pack("<3f", *v) for v in trans), 5126, "VEC3", n)
    r_acc = b.add(struct.pack("<4f", 0, 0, 0, 1) * n, 5126, "VEC4", n)
    s_acc = b.add(struct.pack("<3f", 1, 1, 1) * n, 5126, "VEC3", n)

    images = [("LABEL", noise_png(128, 128)), ("BODY", solid_png(8, 8, (34, 36, 44))),
              ("NAME", noise_png(256, 64))]
    gltf = {
        "asset": {"version": "2.0", "generator": "gpSP 3D make_banner3d.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "Tile", "mesh": 0, "translation": [0, TILE_Y, 0]},
            {"name": "Name", "mesh": 1},
        ],
        "meshes": [{"name": "Tile", "primitives": [faces, body]},
                   {"name": "Name", "primitives": [name]}],
        "materials": [
            # the icon's corners are cut round by its transparency (a rounded
            # outline in the model froze the HOME Menu once animated)
            {"name": "Label", "alphaMode": "MASK", "alphaCutoff": 0.5,
             "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0}},
            {"name": "Body", "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}, "metallicFactor": 0}},
            {"name": "NameText", "alphaMode": "MASK", "alphaCutoff": 0.5, "doubleSided": True,
             "pbrMetallicRoughness": {"baseColorTexture": {"index": 2}, "metallicFactor": 0}},
        ],
        "textures": [{"source": i, "sampler": 0} for i in range(3)],
        "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"name": nm, "mimeType": "image/png",
                    "uri": "data:image/png;base64," + base64.b64encode(png).decode()} for nm, png in images],
        "buffers": [{"byteLength": len(b.buf),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(b.buf)).decode()}],
        "bufferViews": b.views,
        "accessors": b.accessors,
    }
    if animated:
        gltf["animations"] = [{"name": "Bob", "samplers": [
            {"input": t_acc, "output": p_acc, "interpolation": "LINEAR"},
            {"input": t_acc, "output": r_acc, "interpolation": "LINEAR"},
            {"input": t_acc, "output": s_acc, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}},
                         {"sampler": 1, "target": {"node": 0, "path": "rotation"}},
                         {"sampler": 2, "target": {"node": 0, "path": "scale"}}]}]
    return gltf


def jingle_samples(rate=32000):
    """A short, soft chime (1.6 s): two sine-bell notes a fifth apart,
    slow to fade, with a gentle echo; quiet, so hovering over the icon is
    calm rather than loud."""
    notes = [(0.00, 72), (0.16, 79)]           # C5, then G5
    total = int(rate * 1.6) & ~15               # as long as the sound that worked, whole 16-sample blocks
    out = [0.0] * total
    for start, midi in notes:
        f = 440 * 2 ** ((midi - 69) / 12)
        s0 = int(start * rate)
        for i in range(total - s0):
            t = i / rate
            env = min(1.0, t / 0.012) * math.exp(-t * 2.6)
            # a sine with a little of its octave, like a soft bell
            v = math.sin(2 * math.pi * f * t) + 0.18 * math.sin(4 * math.pi * f * t) * math.exp(-t * 6)
            out[s0 + i] += 0.16 * env * v
    d = int(0.21 * rate)
    for i in range(total - 1, d - 1, -1):
        out[i] += 0.22 * out[i - d]
    for i in range(int(0.15 * rate)):           # fade the tail to silence
        out[total - 1 - i] *= i / (0.15 * rate)
    return rate, [int(max(-1, min(1, v)) * 32767) for v in out]


def make_jingle(path):
    """The chime as a WAV, stereo like the banner sounds that play right."""
    rate, smp = jingle_samples()
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<hh", v, v) for v in smp))


def make_cwav(path):
    """The chime as a CWAV (what a banner holds): PCM16 stereo at 32 kHz,
    laid out like the sound in Snes9x 3D's original banner, which plays
    right. (A mono one played as a fast beeping on the HOME Menu.)"""
    rate, smp = jingle_samples()
    n = len(smp)
    pcm = b"".join(struct.pack("<h", v) for v in smp)
    data_size = 0x20 + 2 * len(pcm)
    info = b"INFO" + struct.pack("<I", 0x60) + struct.pack("<BBH", 1, 0, 0) + struct.pack(
        "<IIII", rate, 0, n, 0) + struct.pack("<I", 2) + struct.pack("<HHi", 0x7100, 0, 0x14) + \
        struct.pack("<HHi", 0x7100, 0, 0x28)
    for ch in range(2):
        info += struct.pack("<HHi", 0x1F00, 0, 0x18 + ch * len(pcm)) + struct.pack("<HHi", 0, 0, -1) + \
            struct.pack("<I", 0)
    info += bytes(0x60 - len(info))
    total = 0x40 + 0x60 + data_size
    hdr = b"CWAV" + struct.pack("<HHII", 0xFEFF, 0x40, 0x02010000, total) + struct.pack("<HH", 2, 0) + \
        struct.pack("<HHII", 0x7000, 0, 0x40, 0x60) + struct.pack("<HHII", 0x7001, 0, 0xA0, data_size)
    hdr += bytes(0x40 - len(hdr))
    with open(path, "wb") as f:
        f.write(hdr + info + b"DATA" + struct.pack("<I", data_size) + bytes(0x18) + pcm + pcm)


def hardware_fixes(cgfx):
    """What pycgfx leaves out and the real HOME Menu needs (from the
    hardware-tested banner converters of ClouDS Music FA and ftb): every
    mesh bound to its node's bone, no skinning, and matte materials
    (pycgfx's specular washes the model out under the HOME Menu's light)."""
    from cgfx.mtob import ColorFloat, FragmentLightingFlags
    from cgfx.primitives import VertexAttributeUsage
    model = cgfx.data.models["COMMON"]
    bones = list(model.skeleton.bones)
    materials = list(model.materials)
    for mesh in model.meshes.data.contents:
        owners = [b for b in bones if mesh.name == b or mesh.name.startswith(b + "_")]
        if not owners:
            sys.exit("no bone for mesh " + mesh.name)
        mesh.mesh_node_name = max(owners, key=len)
        # pycgfx names every mesh of a node after its last material: make them unique
        mesh.name = mesh.mesh_node_name + "_" + materials[mesh.material_index]
    for shape, mesh in zip(model.shapes.data.contents, model.meshes.data.contents):
        shape.name = mesh.name
    for shape in model.shapes.data.contents:
        for ps in shape.primitive_sets.data.contents:
            if ps.skinning_mode != 0:
                sys.exit(shape.name + " is skinned")
        for attr in shape.vertex_attributes.data.contents:
            for st in (attr.vertex_streams.data.contents if hasattr(attr, "vertex_streams") else (attr,)):
                if st.usage in (VertexAttributeUsage.BoneIndex, VertexAttributeUsage.BoneWeight):
                    sys.exit(shape.name + " has bone weights")
    from cgfx.sobj import BillboardMode
    model.skeleton.bones["Name"].billboard_mode = BillboardMode.YAxial
    for name in model.materials:
        m = model.materials[name]
        m.material_color.constant[0] = ColorFloat(0, 0, 0, 1)
        spec = m.fragment_shader.texture_combiners[2]
        spec.src_rgb = 0xFFF
        spec.combine_rgb = 0
        m.fragment_shader.fragment_lighting.flags = FragmentLightingFlags(0)
        m.fragment_shader.fragment_lighting_table.distribution_0_sampler = None


def convert(pycgfx_dir, gltf, out, rgb565=True):
    sys.path.insert(0, pycgfx_dir)
    import gltflib
    import main as pycgfx
    from cgfx.txob import TextureFormat
    global _label_fmt
    _label_fmt = TextureFormat.RGB565 if rgb565 else TextureFormat.RGBA4
    to_txob = pycgfx.swizzler.to_txob
    if not getattr(to_txob, "patched", False):
        # the tile's sides are opaque: RGB565; the icon (round corners) and
        # the name strip need RGBA4's transparency
        f = lambda im: to_txob(im, TextureFormat.RGBA4 if im.size in ((256, 64), (128, 128)) else _label_fmt)
        f.patched = True
        pycgfx.swizzler.to_txob = f
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "banner3d.gltf")
        with open(src, "w") as f:
            json.dump(gltf, f)
        cgfx = pycgfx.convert_gltf(gltflib.GLTF.load(src, load_file_resources=True))
    hardware_fixes(cgfx)
    data = pycgfx.write(cgfx)
    if len(data) > 0x80000:
        sys.exit("model too big for the HOME Menu")
    with open(out, "wb") as f:
        f.write(data)
    print("wrote", os.path.relpath(out, os.path.join(RES, "..")), len(data), "bytes")


# ---- the apps' own banners, from the same model ----

def _zo(x, y):
    return sum((((x >> b) & 1) << (2 * b)) | (((y >> b) & 1) << (2 * b + 1)) for b in range(3))


def _put_texture(cgfx, w, h, png):
    """Into the model's w x h texture (RGBA4 or RGB565): 8x8 tiles in Z-order,
    bottom row first (pycgfx's way)."""
    from PIL import Image
    im = Image.open(png).convert("RGBA")
    if im.size != (w, h):
        sys.exit("%s should be %dx%d" % (png, w, h))
    i = 0
    while True:
        i = cgfx.find(b"TXOB", i + 1)
        if i < 0:
            sys.exit("model has no %dx%d texture" % (w, h))
        fmt = struct.unpack_from("<I", cgfx, i + 0x30)[0]
        if i % 4 == 0 and struct.unpack_from("<2I", cgfx, i + 0x14) == (h, w) and fmt in (3, 4):
            break
    off = i + 0x44 + struct.unpack_from("<i", cgfx, i + 0x44)[0]
    if (w, h) == (128, 128):
        im = rounded_alpha(im)
    px = im.load()
    bayer = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, h - 1 - y]
            d = bayer[y & 3][x & 3]
            if fmt == 3:
                v = (r * 31 + 127) // 255 << 11 | (g * 63 + 127) // 255 << 5 | (b * 31 + 127) // 255
            else:
                q = lambda c: min(15, (c * 15 + d * 16) // 255)
                v = q(r) << 12 | q(g) << 8 | q(b) << 4 | (a * 15 + 127) // 255
            struct.pack_into("<H", cgfx, off + (((y // 8) * (w // 8) + x // 8) * 64 + _zo(x & 7, y & 7)) * 2, v)


def rounded_alpha(im):
    """The icon on the tile: opaque inside a square with rounded corners
    (radius CORNER of the width), transparent outside."""
    from PIL import Image, ImageDraw
    w, h = im.size
    k = 4
    m = Image.new("L", (w * k, h * k), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w * k - 1, h * k - 1), int(w * k * CORNER), fill=255)
    m = m.resize((w, h), Image.LANCZOS)
    out = Image.new("RGBA", (w, h), (74, 79, 90, 255))
    out.alpha_composite(im.convert("RGBA"))
    out.putalpha(m)
    return out


def _lz11_stored(data):
    """LZ11 with no matches (every block flag 0): valid, just not smaller."""
    out = bytearray(struct.pack("<I", 0x11 | len(data) << 8))
    for i in range(0, len(data), 8):
        out.append(0)
        out += data[i:i + 8]
    return bytes(out)


def _cwav_of(bnr):
    with open(bnr, "rb") as f:
        d = f.read()
    return d[struct.unpack_from("<I", d, 0x84)[0]:]


def app_banner(model, name, label, cwav, out):
    with open(model, "rb") as f:
        cgfx = bytearray(f.read())
    _put_texture(cgfx, 256, 64, name)
    _put_texture(cgfx, 128, 128, label)
    packed = _lz11_stored(bytes(cgfx))
    cwav_off = (0x88 + len(packed) + 0x1F) & ~0x1F
    hdr = bytearray(0x88)
    hdr[0:4] = b"CBMD"
    struct.pack_into("<I", hdr, 0x08, 0x88)
    struct.pack_into("<I", hdr, 0x84, cwav_off)
    data = bytes(hdr) + packed + bytes(cwav_off - 0x88 - len(packed)) + cwav
    with open(out, "wb") as f:
        f.write(data)
    print("wrote", out, len(data), "bytes")


def make_app_banners():
    root = os.path.abspath(os.path.join(RES, "..", ".."))
    with open(os.path.join(RES, "jingle.bcwav"), "rb") as f:
        jingle = f.read()
    gp = os.path.join(root, "gpsp3ds", "res")
    app_banner(os.path.join(RES, "banner3d.cgfx"), os.path.join(gp, "banner3d-name.png"),
               os.path.join(gp, "banner3d-label.png"), jingle, os.path.join(gp, "banner3d.bnr"))
    app_banner(os.path.join(RES, "banner3d.cgfx"), os.path.join(RES, "banner3d-name.png"),
               os.path.join(RES, "banner3d-label.png"), jingle, os.path.join(RES, "banner3d-app.bnr"))
    snes = os.path.join(root, "..", "snes9x_3ds-pr2", "resources")
    if os.path.isfile(os.path.join(snes, "banner3d-name.png")):
        app_banner(os.path.join(RES, "banner3d.cgfx"), os.path.join(snes, "banner3d-name.png"),
                   os.path.join(snes, "banner3d-label.png"), jingle,
                   os.path.join(snes, "banner.bnr"))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    make_jingle(os.path.join(RES, "jingle.wav"))
    make_cwav(os.path.join(RES, "jingle.bcwav"))
    print("wrote res/jingle.wav and jingle.bcwav")
    if sys.argv[1] == "--jingle":
        return
    pycgfx_dir = os.path.abspath(sys.argv[1])
    convert(pycgfx_dir, make_gltf(True), os.path.join(RES, "banner3d.cgfx"))
    # the same without the sway, in case an animated banner gives trouble
    convert(pycgfx_dir, make_gltf(False), os.path.join(RES, "banner3d-still.cgfx"))
    make_app_banners()


if __name__ == "__main__":
    main()
