#!/usr/bin/env python3
"""Makes Icon Maker's 3D banner template, res/banner3d.cgfx, and its jingle,
res/jingle.wav.

The banner is a model the HOME Menu draws in 3D: a tile with the game's icon
on its face floats out of the screen in front of a flat backdrop that carries
the game's name, and sways gently (the HOME Menu loops a banner's
animation). Icon Maker swaps in the two pictures for each game:
  BACK   256x128 RGBA4  the backdrop: the name, and the background (or none)
  LABEL  128x128 RGBA4  the tile's face: the game's icon
Both start as noise, so the compressed model always has room for any picture.

  python3 tools/make_banner3d.py --jingle          only res/jingle.wav
  python3 tools/make_banner3d.py /path/to/pycgfx   both, then the 3D banners of
      gpSP 3D, Icon Maker and (if it's next to this repo, as snes9x_3ds-pr2)
      Snes9x 3D from their banner3d-back.png / banner3d-label.png

The model needs pycgfx (https://github.com/skyfloogle/pycgfx, glTF to CGFX),
gltflib and Pillow. The camera matches pycgfx's banner-camera.gltf: at
(0, 1, 44.786) with a 30 degree vertical field of view, so the plane z = 0
shows x -20..20, y -11..13.
"""
import base64, io, json, math, os, random, struct, sys, tempfile, wave

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "res")

# ---- layout (like the flat banner: icon left, name right) ----
TILE_X, TILE_Y, TILE_Z = -11.0, 1.0, 4.0   # centre of the tile; z > 0 comes out of the screen
TILE_W, TILE_D = 12.0, 1.6                 # square face, depth
SECONDS = 4.0                              # one sway, looped


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


def box(w, h, d):
    """A box centred on 0: the front face (+z) gets the whole texture, the
    other five faces go in a separate primitive."""
    x, y, z = w / 2, h / 2, d / 2
    front = ([(-x, -y, z), (x, -y, z), (x, y, z), (-x, y, z)], (0, 0, 1),
             [(0, 1), (1, 1), (1, 0), (0, 0)])
    others = [
        ([(x, -y, -z), (-x, -y, -z), (-x, y, -z), (x, y, -z)], (0, 0, -1)),
        ([(x, -y, z), (x, -y, -z), (x, y, -z), (x, y, z)], (1, 0, 0)),
        ([(-x, -y, -z), (-x, -y, z), (-x, y, z), (-x, y, -z)], (-1, 0, 0)),
        ([(-x, y, z), (x, y, z), (x, y, -z), (-x, y, -z)], (0, 1, 0)),
        ([(-x, -y, -z), (x, -y, -z), (x, -y, z), (-x, -y, z)], (0, -1, 0)),
    ]
    return front, others


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


def make_gltf(animated=True):
    b = Builder()
    # the backdrop: 48 x 24 at z = 0, a little wider than the view (40 x 24)
    back = b.prim([([(-24, -11, 0), (24, -11, 0), (24, 13, 0), (-24, 13, 0)], (0, 0, 1),
                    [(0, 1), (1, 1), (1, 0), (0, 0)])], 0)
    front, others = box(TILE_W, TILE_W, TILE_D)
    face = b.prim([front], 1)
    sides = b.prim(others, 2)          # textured too: a material without one crashes the HOME Menu

    # the sway: a little turn and float, looped
    n = 33
    times = [SECONDS * i / (n - 1) for i in range(n)]
    rots, trans = [], []
    for t in times:
        a = 2 * math.pi * t / SECONDS
        ry, rx = math.radians(16) * math.sin(a), math.radians(5) * math.sin(2 * a)
        cy, sy, cx, sx = math.cos(ry / 2), math.sin(ry / 2), math.cos(rx / 2), math.sin(rx / 2)
        rots.append((cy * sx, sy * cx, -sy * sx, cy * cx))      # turn about x, then y
        trans.append((TILE_X, TILE_Y + 0.5 * math.sin(2 * a), TILE_Z + 0.5 * math.cos(a)))
    t_acc = b.add(b"".join(struct.pack("<f", t) for t in times), 5126, "SCALAR", n, None, ([0.0], [SECONDS]))
    r_acc = b.add(b"".join(struct.pack("<4f", *q) for q in rots), 5126, "VEC4", n)
    p_acc = b.add(b"".join(struct.pack("<3f", *v) for v in trans), 5126, "VEC3", n)

    images = [("BACK", noise_png(256, 128)), ("LABEL", noise_png(128, 128)), ("BODY", solid_png(8, 8, (74, 79, 90)))]
    return {
        "asset": {"version": "2.0", "generator": "gpSP 3D make_banner3d.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "Backdrop", "mesh": 0},
            {"name": "Tile", "mesh": 1, "translation": list(trans[0]), "rotation": list(rots[0])},
        ],
        "meshes": [{"name": "Backdrop", "primitives": [back]},
                   {"name": "Tile", "primitives": [face, sides]}],
        "materials": [
            {"name": "Back",
             "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "roughnessFactor": 1,
                                      "metallicFactor": 0}},
            {"name": "Label", "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}, "roughnessFactor": 0.6,
                                                       "metallicFactor": 0}},
            {"name": "Body", "pbrMetallicRoughness": {"baseColorTexture": {"index": 2}, "roughnessFactor": 0.5,
                                                      "metallicFactor": 0}},
        ],
        "textures": [{"source": 0, "sampler": 0}, {"source": 1, "sampler": 0}, {"source": 2, "sampler": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"name": nm, "mimeType": "image/png",
                    "uri": "data:image/png;base64," + base64.b64encode(png).decode()} for nm, png in images],
        "animations": [] if not animated else [{"name": "Sway", "samplers": [
            {"input": t_acc, "output": r_acc, "interpolation": "LINEAR"},
            {"input": t_acc, "output": p_acc, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}},
                         {"sampler": 1, "target": {"node": 1, "path": "translation"}}]}],
        "buffers": [{"byteLength": len(b.buf),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(b.buf)).decode()}],
        "bufferViews": b.views,
        "accessors": b.accessors,
    }


def make_jingle(path):
    """A short original chime (about 1.6 s): a rising arpeggio on a soft
    pulse wave with an echo, 32 kHz mono, quiet enough for the HOME Menu."""
    rate = 32730
    notes = [(0.00, 72, 0.3), (0.11, 76, 0.3), (0.22, 79, 0.3), (0.33, 84, 0.3), (0.55, 83, 0.3),
             (0.66, 84, 0.9)]
    total = int(rate * 1.6)
    out = [0.0] * total
    for start, midi, length in notes:
        f = 440 * 2 ** ((midi - 69) / 12)
        s0 = int(start * rate)
        for i in range(int(length * rate)):
            if s0 + i >= total:
                break
            t = i / rate
            env = min(1, t * 200) * math.exp(-t * (3.5 if length > 0.5 else 9))
            ph = (f * t) % 1
            v = (0.6 if ph < 0.25 else -0.2) + 0.4 * math.sin(2 * math.pi * f * t)
            out[s0 + i] += 0.22 * env * v
    d = int(0.18 * rate)
    for i in range(total - 1, d - 1, -1):          # one echo
        out[i] += 0.3 * out[i - d]
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, v)) * 32767)) for v in out))


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
    for name in model.materials:
        m = model.materials[name]
        m.material_color.constant[0] = ColorFloat(0, 0, 0, 1)
        spec = m.fragment_shader.texture_combiners[2]
        spec.src_rgb = 0xFFF
        spec.combine_rgb = 0
        m.fragment_shader.fragment_lighting.flags = FragmentLightingFlags(0)
        m.fragment_shader.fragment_lighting_table.distribution_0_sampler = None


def convert(pycgfx_dir, gltf, out):
    sys.path.insert(0, pycgfx_dir)
    import gltflib
    import main as pycgfx
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
    """RGBA4, 8x8 tiles in Z-order, bottom row first (pycgfx's way)."""
    from PIL import Image
    im = Image.open(png).convert("RGBA")
    if im.size != (w, h):
        sys.exit("%s should be %dx%d" % (png, w, h))
    i = 0
    while True:
        i = cgfx.find(b"TXOB", i + 1)
        if i < 0:
            sys.exit("model has no %dx%d texture" % (w, h))
        if i % 4 == 0 and struct.unpack_from("<2I", cgfx, i + 0x14) == (h, w) and \
                struct.unpack_from("<I", cgfx, i + 0x30)[0] == 4:
            break
    off = i + 0x44 + struct.unpack_from("<i", cgfx, i + 0x44)[0]
    px = im.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, h - 1 - y]
            v = (r * 15 + 127) // 255 << 12 | (g * 15 + 127) // 255 << 8 | (b * 15 + 127) // 255 << 4 | (a * 15 + 127) // 255
            struct.pack_into("<H", cgfx, off + (((y // 8) * (w // 8) + x // 8) * 64 + _zo(x & 7, y & 7)) * 2, v)


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


def app_banner(model, back, label, cwav, out):
    with open(model, "rb") as f:
        cgfx = bytearray(f.read())
    _put_texture(cgfx, 256, 128, back)
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
    app_banner(os.path.join(RES, "banner3d.cgfx"), os.path.join(gp, "banner3d-back.png"),
               os.path.join(gp, "banner3d-label.png"), jingle, os.path.join(gp, "banner3d.bnr"))
    # Icon Maker: the still model for now, to tell an animation problem apart
    app_banner(os.path.join(RES, "banner3d-still.cgfx"), os.path.join(RES, "banner3d-back.png"),
               os.path.join(RES, "banner3d-label.png"), jingle, os.path.join(RES, "banner3d-app.bnr"))
    snes = os.path.join(root, "..", "snes9x_3ds-pr2", "resources")
    if os.path.isfile(os.path.join(snes, "banner3d-back.png")):
        app_banner(os.path.join(RES, "banner3d.cgfx"), os.path.join(snes, "banner3d-back.png"),
                   os.path.join(snes, "banner3d-label.png"), _cwav_of(os.path.join(snes, "banner.bnr")),
                   os.path.join(snes, "banner.bnr"))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    make_jingle(os.path.join(RES, "jingle.wav"))
    print("wrote res/jingle.wav")
    if sys.argv[1] == "--jingle":
        return
    pycgfx_dir = os.path.abspath(sys.argv[1])
    convert(pycgfx_dir, make_gltf(True), os.path.join(RES, "banner3d.cgfx"))
    # the same without the sway, in case an animated banner gives trouble
    convert(pycgfx_dir, make_gltf(False), os.path.join(RES, "banner3d-still.cgfx"))
    make_app_banners()


if __name__ == "__main__":
    main()
