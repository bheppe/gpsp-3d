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
  python3 tools/make_banner3d.py /path/to/pycgfx   both

The model needs pycgfx (https://github.com/skyfloogle/pycgfx, glTF to CGFX),
gltflib and Pillow. The camera matches pycgfx's banner-camera.gltf: at
(0, 1, 44.786) with a 30 degree vertical field of view, so the plane z = 0
shows x -20..20, y -11..13.
"""
import base64, io, json, math, os, random, struct, subprocess, sys, tempfile, wave

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


def make_gltf():
    b = Builder()
    # the backdrop: 48 x 24 at z = 0, a little wider than the view (40 x 24)
    back = b.prim([([(-24, -11, 0), (24, -11, 0), (24, 13, 0), (-24, 13, 0)], (0, 0, 1),
                    [(0, 1), (1, 1), (1, 0), (0, 0)])], 0)
    front, others = box(TILE_W, TILE_W, TILE_D)
    face = b.prim([front], 1)
    sides = b.prim(others, 2, uv=False)

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

    images = [("BACK", noise_png(256, 128)), ("LABEL", noise_png(128, 128))]
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
            {"name": "Back", "alphaMode": "BLEND",
             "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "roughnessFactor": 1,
                                      "metallicFactor": 0}},
            {"name": "Label", "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}, "roughnessFactor": 0.6,
                                                       "metallicFactor": 0}},
            {"name": "Body", "pbrMetallicRoughness": {"baseColorFactor": [0.29, 0.31, 0.35, 1],
                                                      "roughnessFactor": 0.5, "metallicFactor": 0}},
        ],
        "textures": [{"source": 0, "sampler": 0}, {"source": 1, "sampler": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"name": nm, "mimeType": "image/png",
                    "uri": "data:image/png;base64," + base64.b64encode(png).decode()} for nm, png in images],
        "animations": [{"name": "Sway", "samplers": [
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


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    make_jingle(os.path.join(RES, "jingle.wav"))
    print("wrote res/jingle.wav")
    if sys.argv[1] == "--jingle":
        return
    pycgfx = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "banner3d.gltf")
        with open(src, "w") as f:
            json.dump(make_gltf(), f)
        subprocess.run([sys.executable, os.path.join(pycgfx, "main.py"), src], check=True, cwd=pycgfx)
        with open(os.path.join(tmp, "banner3d.cgfx"), "rb") as f:
            data = f.read()
    with open(os.path.join(RES, "banner3d.cgfx"), "wb") as f:
        f.write(data)
    print("wrote res/banner3d.cgfx,", len(data), "bytes")


if __name__ == "__main__":
    main()
