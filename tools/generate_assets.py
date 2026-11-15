#!/usr/bin/env python3
"""Generates the sample art, models and audio shipped with Pluto Fall.

Everything here is procedural so the repository contains no third-party assets.
Requires Python 3.9+ and Pillow (pip install pillow). Run from the repository root:

    python tools/generate_assets.py

Outputs into PlutoFall/assets/{models,textures,images,sounds,music}.
Replace any of these files with your own art; the JSON data files reference them by path.
"""
import json
import math
import os
import random
import struct
import wave

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "PlutoFall", "assets")
random.seed(1930)  # Pluto was discovered in 1930


def out(*parts):
    path = os.path.join(ROOT, *parts)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    return path


# --------------------------------------------------------------------------- vector helpers

def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    length = math.sqrt(dot(a, a)) or 1.0
    return (a[0] / length, a[1] / length, a[2] / length)


# --------------------------------------------------------------------------- mesh building
# Meshes are built from convex parts. Each triangle is wound counter-clockwise when seen from
# outside (glTF convention) by checking it against the part centroid.

class Part:
    def __init__(self, material, smooth=False):
        self.material = material
        self.smooth = smooth
        self.positions = []
        self.normals = []
        self.uvs = []
        self.indices = []

    def tri(self, a, b, c, centroid, uv=((0, 0), (1, 0), (0, 1))):
        n = cross(sub(b, a), sub(c, a))
        face_center = mul(add(add(a, b), c), 1 / 3)
        if dot(n, sub(face_center, centroid)) < 0:
            b, c = c, b
            uv = (uv[0], uv[2], uv[1])
            n = mul(n, -1)
        n = norm(n)
        base = len(self.positions)
        for p, t in zip((a, b, c), uv):
            self.positions.append(p)
            self.normals.append(norm(sub(p, centroid)) if self.smooth else n)
            self.uvs.append(t)
        self.indices += [base, base + 1, base + 2]


def centroid_of(points):
    n = len(points)
    return (sum(p[0] for p in points) / n, sum(p[1] for p in points) / n, sum(p[2] for p in points) / n)


def ellipsoid(material, center, radii, seg=16, rings=10, noise=0.0, smooth=True, uv_scale=1.0):
    """UV sphere scaled to an ellipsoid. noise>0 makes lumpy asteroids."""
    part = Part(material, smooth=smooth)
    grid = []
    offsets = {}
    for r in range(rings + 1):
        v = r / rings
        phi = math.pi * v
        row = []
        for s in range(seg + 1):
            u = s / seg
            theta = 2 * math.pi * u
            d = (math.sin(phi) * math.cos(theta), math.cos(phi), math.sin(phi) * math.sin(theta))
            key = (r, s % seg) if 0 < r < rings else (r, 0)
            if key not in offsets:
                offsets[key] = 1.0 + (random.uniform(-noise, noise) if noise else 0.0)
            k = offsets[key]
            p = (center[0] + d[0] * radii[0] * k, center[1] + d[1] * radii[1] * k, center[2] + d[2] * radii[2] * k)
            row.append((p, (u * uv_scale, v * uv_scale)))
        grid.append(row)
    for r in range(rings):
        for s in range(seg):
            a, ua = grid[r][s]
            b, ub = grid[r][s + 1]
            c, uc = grid[r + 1][s + 1]
            d, ud = grid[r + 1][s]
            if r != 0:
                part.tri(a, b, c, center, (ua, ub, uc))
            if r != rings - 1:
                part.tri(a, c, d, center, (ua, uc, ud))
    return part


def hull_from_slices(material, slices):
    """Convex solid lofted through polygon slices (each a list of 3D points with equal counts)."""
    part = Part(material)
    pts = [p for s in slices for p in s]
    c = centroid_of(pts)
    for i in range(len(slices) - 1):
        a_s, b_s = slices[i], slices[i + 1]
        n = len(a_s)
        for j in range(n):
            a, b = a_s[j], a_s[(j + 1) % n]
            cc, d = b_s[(j + 1) % n], b_s[j]
            part.tri(a, b, cc, c)
            part.tri(a, cc, d, c)
    for cap in (slices[0], slices[-1]):
        cap_center = centroid_of(cap)
        for j in range(len(cap)):
            part.tri(cap_center, cap[j], cap[(j + 1) % len(cap)], c)
    return part


def box(material, center, size):
    hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    cx, cy, cz = center

    def ring(x):
        return [(x, cy - hy, cz - hz), (x, cy + hy, cz - hz), (x, cy + hy, cz + hz), (x, cy - hy, cz + hz)]

    return hull_from_slices(material, [ring(cx - hx), ring(cx + hx)])


def extrude(material, outline_xy, z0, z1, plane="xz"):
    """Extrudes a convex 2D outline. plane 'xz': outline in XZ, thickness along Y (wings).
    plane 'xy': outline in XY, thickness along Z (fins)."""
    if plane == "xz":
        a = [(x, z0, z) for x, z in outline_xy]
        b = [(x, z1, z) for x, z in outline_xy]
    else:
        a = [(x, y, z0) for x, y in outline_xy]
        b = [(x, y, z1) for x, y in outline_xy]
    return hull_from_slices(material, [a, b])


def cone(material, base_center, radius, tip, seg=12, axis="x"):
    ring = []
    for i in range(seg):
        t = 2 * math.pi * i / seg
        if axis == "x":
            ring.append((base_center[0], base_center[1] + math.cos(t) * radius, base_center[2] + math.sin(t) * radius))
        elif axis == "y":
            ring.append((base_center[0] + math.cos(t) * radius, base_center[1], base_center[2] + math.sin(t) * radius))
    part = Part(material)
    c = centroid_of(ring + [tip])
    bc = centroid_of(ring)
    for i in range(seg):
        part.tri(ring[i], ring[(i + 1) % seg], tip, c)
        part.tri(ring[i], ring[(i + 1) % seg], bc, c)
    return part


def torus(material, center, major, minor, seg=24, sides=8):
    """Approximated by convex segments so winding stays correct."""
    parts = []
    for i in range(seg):
        t0 = 2 * math.pi * i / seg
        t1 = 2 * math.pi * (i + 1) / seg
        slices = []
        for t in (t0, t1):
            ring = []
            for j in range(sides):
                s = 2 * math.pi * j / sides
                r = major + math.cos(s) * minor
                ring.append((center[0] + math.cos(t) * r, center[1] + math.sin(s) * minor, center[2] + math.sin(t) * r))
            slices.append(ring)
        parts.append(hull_from_slices(material, slices))
    return parts


# --------------------------------------------------------------------------- glTF writer

def write_glb(path, parts, materials, images=None):
    """parts: list of Part. materials: list of glTF material dicts.
    images: optional list of dicts: {"uri": "..."} or {"png": bytes} (embedded)."""
    bin_data = bytearray()
    buffer_views, accessors, primitives = [], [], []

    def align():
        while len(bin_data) % 4:
            bin_data.append(0)

    def add_view(data, target=None):
        align()
        offset = len(bin_data)
        bin_data.extend(data)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target:
            view["target"] = target
        buffer_views.append(view)
        return len(buffer_views) - 1

    # Merge parts by material.
    merged = {}
    for p in parts:
        m = merged.setdefault(p.material, Part(p.material))
        base = len(m.positions)
        m.positions += p.positions
        m.normals += p.normals
        m.uvs += p.uvs
        m.indices += [i + base for i in p.indices]

    for material, p in sorted(merged.items()):
        pos = b"".join(struct.pack("<3f", *v) for v in p.positions)
        nrm = b"".join(struct.pack("<3f", *v) for v in p.normals)
        uvs = b"".join(struct.pack("<2f", *v) for v in p.uvs)
        idx = b"".join(struct.pack("<I", i) for i in p.indices)
        mn = [min(v[k] for v in p.positions) for k in range(3)]
        mx = [max(v[k] for v in p.positions) for k in range(3)]
        accessors.append({"bufferView": add_view(pos, 34962), "componentType": 5126, "count": len(p.positions),
                          "type": "VEC3", "min": mn, "max": mx})
        a_pos = len(accessors) - 1
        accessors.append({"bufferView": add_view(nrm, 34962), "componentType": 5126, "count": len(p.normals), "type": "VEC3"})
        a_nrm = len(accessors) - 1
        accessors.append({"bufferView": add_view(uvs, 34962), "componentType": 5126, "count": len(p.uvs), "type": "VEC2"})
        a_uv = len(accessors) - 1
        accessors.append({"bufferView": add_view(idx, 34963), "componentType": 5125, "count": len(p.indices), "type": "SCALAR"})
        a_idx = len(accessors) - 1
        primitives.append({"attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv}, "indices": a_idx,
                           "material": material})

    gltf = {
        "asset": {"version": "2.0", "generator": "Pluto Fall generate_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": os.path.splitext(os.path.basename(path))[0]}],
        "meshes": [{"primitives": primitives}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": buffer_views,
    }
    extensions = set()
    for m in materials:
        extensions.update(m.get("extensions", {}).keys())
    if extensions:
        gltf["extensionsUsed"] = sorted(extensions)
    if images:
        gltf["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
        gltf["images"] = []
        gltf["textures"] = []
        for i, img in enumerate(images):
            if "png" in img:
                gltf["images"].append({"bufferView": add_view(img["png"]), "mimeType": "image/png"})
            else:
                gltf["images"].append({"uri": img["uri"]})
            gltf["textures"].append({"sampler": 0, "source": i})
    align()
    gltf["buffers"] = [{"byteLength": len(bin_data)}]

    json_bytes = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    while len(json_bytes) % 4:
        json_bytes += b" "
    total = 12 + 8 + len(json_bytes) + 8 + len(bin_data)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(json_bytes), 0x4E4F534A))
        f.write(json_bytes)
        f.write(struct.pack("<II", len(bin_data), 0x004E4942))
        f.write(bin_data)


def pbr(color, metallic=0.3, roughness=0.45, emissive=None, texture=None, unlit=False, name=None, double=False):
    m = {"pbrMetallicRoughness": {"baseColorFactor": list(color) + ([1.0] if len(color) == 3 else []),
                                  "metallicFactor": metallic, "roughnessFactor": roughness}}
    if name:
        m["name"] = name
    if emissive:
        m["emissiveFactor"] = list(emissive)
    if texture is not None:
        m["pbrMetallicRoughness"]["baseColorTexture"] = {"index": texture}
    if unlit:
        m["extensions"] = {"KHR_materials_unlit": {}}
    if double:
        m["doubleSided"] = True
    return m


# --------------------------------------------------------------------------- models
# Convention: nose along +X, top along +Y, wings spread along Z. Units are game units.

def player_arrow(path, hull, accent):
    parts = [
        hull_from_slices(0, [
            [(-0.8, -0.12, -0.18), (-0.8, 0.16, -0.18), (-0.8, 0.16, 0.18), (-0.8, -0.12, 0.18)],
            [(0.1, -0.16, -0.22), (0.1, 0.22, -0.22), (0.1, 0.22, 0.22), (0.1, -0.16, 0.22)],
            [(0.95, -0.02, -0.02), (0.95, 0.03, -0.02), (0.95, 0.03, 0.02), (0.95, -0.02, 0.02)],
        ]),
        extrude(1, [(-0.7, -0.95), (0.05, -0.22), (0.05, 0.22), (-0.7, 0.95), (-0.85, 0.95), (-0.85, -0.95)], -0.03, 0.03),
        extrude(1, [(-0.8, 0.12), (-0.35, 0.12), (-0.7, 0.62), (-0.85, 0.62)], -0.025, 0.025, plane="xy"),
        ellipsoid(2, (0.15, 0.2, 0.0), (0.32, 0.12, 0.13), seg=12, rings=8),
        box(3, (-0.86, 0.02, -0.12), (0.08, 0.14, 0.14)),
        box(3, (-0.86, 0.02, 0.12), (0.08, 0.14, 0.14)),
    ]
    mats = [pbr(hull, 0.55, 0.35, name="hull"), pbr(accent, 0.3, 0.5, name="wings"),
            pbr((0.2, 0.6, 1.0), 0.1, 0.1, emissive=(0.05, 0.25, 0.5), name="canopy"),
            pbr((1.0, 0.6, 0.2), 0.0, 1.0, emissive=(1.0, 0.55, 0.15), name="engine")]
    write_glb(path, parts, mats)


def player_delta(path, hull, accent):
    parts = [
        extrude(0, [(1.0, 0.0), (-0.7, 0.85), (-0.85, 0.7), (-0.85, -0.7), (-0.7, -0.85)], -0.08, 0.08),
        hull_from_slices(1, [
            [(-0.75, -0.05, -0.2), (-0.75, 0.25, -0.2), (-0.75, 0.25, 0.2), (-0.75, -0.05, 0.2)],
            [(0.6, 0.0, -0.05), (0.6, 0.12, -0.05), (0.6, 0.12, 0.05), (0.6, 0.0, 0.05)],
        ]),
        ellipsoid(2, (-0.05, 0.22, 0.0), (0.3, 0.1, 0.12), seg=12, rings=8),
        box(3, (-0.88, 0.05, 0.0), (0.06, 0.12, 0.5)),
    ]
    mats = [pbr(hull, 0.6, 0.3, name="hull"), pbr(accent, 0.4, 0.4, name="spine"),
            pbr((1.0, 0.8, 0.3), 0.1, 0.1, emissive=(0.4, 0.3, 0.05), name="canopy"),
            pbr((0.3, 0.8, 1.0), 0.0, 1.0, emissive=(0.3, 0.8, 1.0), name="engine")]
    write_glb(path, parts, mats)


def player_twin(path, hull, accent):
    parts = []
    for z in (-0.45, 0.45):
        parts.append(hull_from_slices(0, [
            [(-0.85, -0.1, z - 0.13), (-0.85, 0.12, z - 0.13), (-0.85, 0.12, z + 0.13), (-0.85, -0.1, z + 0.13)],
            [(0.75, -0.04, z - 0.06), (0.75, 0.06, z - 0.06), (0.75, 0.06, z + 0.06), (0.75, -0.04, z + 0.06)],
        ]))
        parts.append(box(3, (-0.9, 0.01, z), (0.06, 0.16, 0.2)))
    parts.append(extrude(1, [(-0.3, -0.45), (0.25, -0.45), (0.25, 0.45), (-0.3, 0.45)], -0.03, 0.04))
    parts.append(ellipsoid(2, (0.0, 0.08, 0.0), (0.38, 0.14, 0.18), seg=12, rings=8))
    mats = [pbr(hull, 0.5, 0.35, name="hull"), pbr(accent, 0.4, 0.4, name="wing"),
            pbr((0.3, 1.0, 0.5), 0.1, 0.1, emissive=(0.05, 0.4, 0.15), name="canopy"),
            pbr((1.0, 0.3, 0.6), 0.0, 1.0, emissive=(1.0, 0.3, 0.6), name="engine")]
    write_glb(path, parts, mats)


def enemy_drone(path):
    parts = [ellipsoid(0, (0, 0, 0), (0.55, 0.55, 0.55), seg=8, rings=4, smooth=False),
             ellipsoid(1, (0.38, 0, 0), (0.18, 0.18, 0.18), seg=10, rings=6)]
    parts += torus(2, (0, 0, 0), 0.75, 0.07, seg=16, sides=6)
    mats = [pbr((0.55, 0.15, 0.6), 0.6, 0.35, name="body"),
            pbr((1.0, 0.2, 0.2), 0.0, 0.3, emissive=(1.0, 0.1, 0.1), name="eye"),
            pbr((0.8, 0.8, 0.85), 0.9, 0.25, name="ring")]
    write_glb(path, parts, mats)


def enemy_saucer(path):
    parts = [ellipsoid(0, (0, 0, 0), (0.85, 0.22, 0.85), seg=20, rings=8),
             ellipsoid(1, (0, 0.18, 0), (0.38, 0.28, 0.38), seg=14, rings=8)]
    for i in range(6):
        t = 2 * math.pi * i / 6
        parts.append(box(2, (math.cos(t) * 0.7, -0.05, math.sin(t) * 0.7), (0.12, 0.08, 0.12)))
    mats = [pbr((0.45, 0.5, 0.55), 0.85, 0.3, name="hull"),
            pbr((0.3, 1.0, 0.6), 0.1, 0.1, emissive=(0.1, 0.5, 0.25), name="dome"),
            pbr((1.0, 0.9, 0.3), 0.0, 1.0, emissive=(1.0, 0.8, 0.2), name="lights")]
    write_glb(path, parts, mats)


def enemy_fighter(path):
    parts = [
        hull_from_slices(0, [
            [(-0.8, -0.1, -0.15), (-0.8, 0.12, -0.15), (-0.8, 0.12, 0.15), (-0.8, -0.1, 0.15)],
            [(0.9, -0.03, -0.04), (0.9, 0.04, -0.04), (0.9, 0.04, 0.04), (0.9, -0.03, 0.04)],
        ]),
        extrude(1, [(0.3, 0.1), (-0.5, 0.1), (-0.85, 0.9), (-0.7, 0.9)], -0.03, 0.03),
        extrude(1, [(0.3, -0.1), (-0.7, -0.9), (-0.85, -0.9), (-0.5, -0.1)], -0.03, 0.03),
        ellipsoid(2, (0.1, 0.12, 0), (0.25, 0.08, 0.1), seg=10, rings=6),
    ]
    mats = [pbr((0.75, 0.2, 0.15), 0.5, 0.4, name="hull"), pbr((0.25, 0.25, 0.3), 0.7, 0.3, name="wings"),
            pbr((1.0, 0.85, 0.2), 0.0, 0.2, emissive=(0.8, 0.6, 0.1), name="canopy")]
    write_glb(path, parts, mats)


def boss_cruiser(path):
    parts = [
        hull_from_slices(0, [
            [(-2.6, -0.7, -0.8), (-2.6, 0.6, -0.8), (-2.6, 0.6, 0.8), (-2.6, -0.7, 0.8)],
            [(1.2, -0.8, -1.0), (1.2, 0.7, -1.0), (1.2, 0.7, 1.0), (1.2, -0.8, 1.0)],
            [(2.6, -0.3, -0.3), (2.6, 0.2, -0.3), (2.6, 0.2, 0.3), (2.6, -0.3, 0.3)],
        ]),
        box(1, (-0.6, 0.95, 0.0), (1.4, 0.5, 0.8)),
        box(1, (-0.4, 1.35, 0.0), (0.6, 0.4, 0.5)),
        box(1, (-1.0, -1.0, 0.0), (2.0, 0.4, 1.2)),
        extrude(1, [(-2.4, 0.6), (-1.4, 0.6), (-2.2, 1.9), (-2.7, 1.9)], -0.08, 0.08, plane="xy"),
        extrude(1, [(-2.4, -0.7), (-1.4, -0.7), (-2.2, -1.9), (-2.7, -1.9)], -0.08, 0.08, plane="xy"),
        ellipsoid(2, (1.6, 0.1, 0.0), (0.5, 0.25, 0.4), seg=12, rings=8),
        box(3, (-2.68, 0.25, -0.45), (0.12, 0.35, 0.35)),
        box(3, (-2.68, 0.25, 0.45), (0.12, 0.35, 0.35)),
        box(3, (-2.68, -0.35, 0.0), (0.12, 0.35, 0.35)),
    ]
    for x in (-1.6, -0.2, 0.8):
        parts.append(cone(4, (x, -1.2, 0.0), 0.15, (x - 0.6, -1.35, 0.0), seg=8, axis="x"))
    mats = [pbr((0.35, 0.33, 0.4), 0.8, 0.35, name="hull"), pbr((0.55, 0.15, 0.2), 0.5, 0.4, name="armor"),
            pbr((1.0, 0.25, 0.15), 0.0, 0.2, emissive=(1.0, 0.2, 0.1), name="core"),
            pbr((0.4, 0.9, 1.0), 0.0, 1.0, emissive=(0.3, 0.8, 1.0), name="engines"),
            pbr((0.8, 0.8, 0.85), 0.9, 0.2, name="guns")]
    write_glb(path, parts, mats)


def boss_sentinel(path):
    parts = [ellipsoid(0, (0, 0, 0), (1.6, 1.6, 1.6), seg=10, rings=6, smooth=False),
             ellipsoid(1, (1.3, 0, 0), (0.6, 0.6, 0.6), seg=16, rings=10)]
    parts += torus(2, (0, 0, 0), 2.3, 0.18, seg=24, sides=6)
    for i in range(6):
        t = 2 * math.pi * i / 6
        d = (math.cos(t), math.sin(t), 0)
        parts.append(box(2, (d[0] * 1.8, d[1] * 1.8, 0), (0.4, 0.4, 0.4)))
    mats = [pbr((0.2, 0.22, 0.3), 0.85, 0.3, name="shell"),
            pbr((0.9, 0.2, 1.0), 0.0, 0.2, emissive=(0.8, 0.1, 1.0), name="eye"),
            pbr((0.75, 0.75, 0.8), 0.95, 0.2, name="ring")]
    write_glb(path, parts, mats)


def item_capsule(path, color, stripe_color):
    parts = [ellipsoid(0, (0, 0, 0), (0.45, 0.45, 0.45), seg=16, rings=10, uv_scale=1.0),
             ellipsoid(1, (0, 0, 0), (0.62, 0.12, 0.62), seg=16, rings=4)]
    mats = [pbr(color, 0.2, 0.3, emissive=tuple(c * 0.35 for c in color), texture=0, name="core"),
            pbr((0.85, 0.85, 0.9), 0.9, 0.2, name="band")]
    write_glb(path, parts, mats, images=[{"png": stripe_png(stripe_color)}])


def asteroid(path):
    parts = [ellipsoid(0, (0, 0, 0), (1.0, 0.85, 0.9), seg=14, rings=9, noise=0.22, smooth=False, uv_scale=2.0)]
    # The texture is referenced by file name only; the game finds it in the model's textureFolder.
    write_glb(path, parts, [pbr((1, 1, 1), 0.0, 0.95, texture=0, name="rock")], images=[{"uri": "rock.png"}])


def planet(path, texture_name):
    parts = [ellipsoid(0, (0, 0, 0), (1.0, 1.0, 1.0), seg=40, rings=24)]
    write_glb(path, parts, [pbr((1, 1, 1), 0.0, 0.9, texture=0, name="surface")], images=[{"uri": texture_name}])


def station(path):
    parts = torus(0, (0, 0, 0), 2.0, 0.25, seg=24, sides=6)
    parts.append(ellipsoid(1, (0, 0, 0), (0.5, 0.5, 0.5), seg=14, rings=8))
    for i in range(4):
        t = math.pi / 2 * i
        c = (math.cos(t) * 1.0, 0.0, math.sin(t) * 1.0)
        parts.append(box(0, c, (abs(math.cos(t)) * 1.6 + 0.12, 0.12, abs(math.sin(t)) * 1.6 + 0.12)))
    mats = [pbr((0.7, 0.72, 0.78), 0.8, 0.35, name="frame"),
            pbr((0.4, 0.8, 1.0), 0.0, 0.3, emissive=(0.2, 0.5, 0.8), name="core")]
    write_glb(path, parts, mats)


# --------------------------------------------------------------------------- images

def noise_layer(w, h, cell, wrap=True):
    small = Image.new("L", (max(2, w // cell), max(2, h // cell)))
    small.putdata([random.randint(0, 255) for _ in range(small.width * small.height)])
    if wrap:
        # Tile 3x3 then crop the centre so edges wrap seamlessly.
        tiled = Image.new("L", (small.width * 3, small.height * 3))
        for i in range(3):
            for j in range(3):
                tiled.paste(small, (i * small.width, j * small.height))
        big = tiled.resize((w * 3, h * 3), Image.BICUBIC)
        return big.crop((w, h, 2 * w, 2 * h))
    return small.resize((w, h), Image.BICUBIC)


def fractal(w, h, octaves=(64, 32, 16, 8), wrap=True):
    acc = [0.0] * (w * h)
    total = 0.0
    amp = 1.0
    for cell in octaves:
        layer = list(noise_layer(w, h, cell, wrap).tobytes())
        for i, v in enumerate(layer):
            acc[i] += v * amp
        total += amp
        amp *= 0.5
    return [v / total / 255.0 for v in acc]


def starfield(w, h, count, wrap=True, nebula=None):
    img = Image.new("RGB", (w, h), (2, 3, 10))
    if nebula:
        n = fractal(w, h, (w // 4, w // 8, w // 16), wrap)
        px = img.load()
        for y in range(h):
            for x in range(w):
                v = max(0.0, n[y * w + x] - 0.45) * 2.2
                r, g, b = px[x, y]
                px[x, y] = (int(r + nebula[0] * v), int(g + nebula[1] * v), int(b + nebula[2] * v))
    d = ImageDraw.Draw(img)
    for _ in range(count):
        x, y = random.randrange(w), random.randrange(h)
        b = random.randint(120, 255)
        tint = random.choice([(1, 1, 1), (0.8, 0.9, 1), (1, 0.9, 0.8), (0.9, 0.9, 1)])
        c = (int(b * tint[0]), int(b * tint[1]), int(b * tint[2]))
        if random.random() < 0.06:
            d.ellipse((x - 1, y - 1, x + 1, y + 1), fill=c)
        else:
            d.point((x, y), fill=c)
    return img


def draw_planet(img, cx, cy, r, base, shade=0.25, light=(-0.6, -0.5)):
    w, h = img.size
    tex = fractal(256, 256, (32, 16, 8), True)
    px = img.load()
    lx, ly = light
    lz = math.sqrt(max(0.0, 1 - lx * lx - ly * ly))
    for y in range(max(0, cy - r - 30), min(h, cy + r + 30)):
        for x in range(max(0, cx - r - 30), min(w, cx + r + 30)):
            dx, dy = (x - cx) / r, (y - cy) / r
            d2 = dx * dx + dy * dy
            if d2 <= 1.0:
                dz = math.sqrt(1 - d2)
                lam = max(0.0, dx * lx + dy * ly + dz * lz)
                u = int((math.atan2(dx, dz) / math.pi * 0.5 + 0.5) * 255) % 256
                v = int((dy * 0.5 + 0.5) * 255) % 256
                t = tex[v * 256 + u]
                k = (0.08 + 0.92 * lam) * (0.75 + 0.5 * t)
                heart = math.exp(-((dx + 0.15) ** 2 + (dy - 0.25) ** 2) * 9)
                col = [base[i] * k + heart * 120 * lam for i in range(3)]
                px[x, y] = tuple(int(min(255, c)) for c in col)
            elif d2 < 1.12:
                # Thin atmosphere glow.
                a = (1.12 - d2) / 0.12
                r0, g0, b0 = px[x, y]
                px[x, y] = (int(r0 + 40 * a), int(g0 + 60 * a), int(b0 + 110 * a))


def title_image(path):
    w, h = 1600, 900
    img = starfield(w, h, 1600, wrap=False, nebula=(70, 30, 110))
    draw_planet(img, 1150, 640, 420, (205, 170, 140))
    draw_planet(img, 360, 230, 70, (150, 150, 160), light=(-0.2, -0.6))
    img = img.filter(ImageFilter.SMOOTH)
    img.save(path, optimize=True)


def dialog_space(path):
    w, h = 1600, 900
    img = starfield(w, h, 1200, wrap=False, nebula=(30, 60, 120))
    draw_planet(img, 800, 1250, 700, (190, 160, 135), light=(-0.3, -0.8))
    img.save(path, optimize=True)


def dialog_bridge(path):
    w, h = 1600, 900
    img = Image.new("RGB", (w, h), (12, 16, 26))
    view = starfield(1100, 420, 500, wrap=False, nebula=(40, 20, 80))
    draw_planet(view, 760, 300, 180, (205, 170, 140))
    img.paste(view, (250, 90))
    d = ImageDraw.Draw(img)
    d.rectangle((250, 90, 1350, 510), outline=(70, 90, 120), width=14)
    for x in (617, 984):
        d.rectangle((x - 6, 90, x + 6, 510), fill=(60, 75, 100))
    d.polygon([(0, 900), (0, 640), (380, 560), (1220, 560), (1600, 640), (1600, 900)], fill=(28, 34, 48))
    for i in range(14):
        x = 120 + i * 100
        col = random.choice([(40, 200, 255), (255, 170, 40), (80, 255, 120), (255, 60, 60)])
        d.rectangle((x, 600 + (i % 3) * 18, x + 46, 612 + (i % 3) * 18), fill=col)
    d.rectangle((0, 0, w, 60), fill=(20, 26, 38))
    img = img.filter(ImageFilter.SMOOTH)
    img.save(path, optimize=True)


def dialog_surface(path):
    w, h = 1600, 900
    img = starfield(w, h, 900, wrap=False)
    d = ImageDraw.Draw(img)
    n = fractal(512, 128, (64, 32, 16), False)
    pts = [(0, h)]
    for x in range(0, w + 1, 8):
        v = n[64 * 512 + min(511, x * 512 // w)]
        pts.append((x, int(600 + v * 160)))
    pts.append((w, h))
    d.polygon(pts, fill=(170, 150, 135))
    for _ in range(40):
        x, y, r = random.randrange(w), random.randint(700, 880), random.randint(8, 40)
        d.ellipse((x - r, y - r // 3, x + r, y + r // 3), fill=(140, 120, 110))
    # A burning research station on the horizon.
    d.rectangle((1100, 560, 1260, 640), fill=(80, 85, 95))
    d.ellipse((1150, 470, 1260, 580), fill=(255, 120, 40))
    img = img.filter(ImageFilter.SMOOTH)
    img.save(path, optimize=True)


def portrait(path, skin, suit, visor=None, ai=False):
    s = 512
    img = Image.new("RGB", (s, s), (18, 24, 40))
    d = ImageDraw.Draw(img)
    for i in range(0, s, 8):
        d.line((0, i, s, i), fill=(22, 30, 50))
    if ai:
        for r, c in ((200, (30, 10, 50)), (150, (90, 20, 140)), (95, (200, 60, 255)), (50, (255, 220, 255))):
            d.ellipse((256 - r, 256 - r, 256 + r, 256 + r), fill=c)
        for a in range(0, 360, 30):
            t = math.radians(a)
            d.line((256 + math.cos(t) * 210, 256 + math.sin(t) * 210, 256 + math.cos(t) * 245, 256 + math.sin(t) * 245),
                   fill=(200, 60, 255), width=6)
        img = img.filter(ImageFilter.SMOOTH_MORE)
        img.save(path, optimize=True)
        return
    d.rectangle((96, 400, 416, 512), fill=suit)
    d.ellipse((60, 380, 452, 600), fill=suit)
    d.rectangle((216, 330, 296, 420), fill=skin)
    d.ellipse((136, 110, 376, 380), fill=skin)
    d.ellipse((120, 80, 392, 250), fill=tuple(int(c * 0.5) for c in suit))
    d.rectangle((136, 170, 376, 210), fill=skin)
    d.ellipse((186, 230, 226, 262), fill=(255, 255, 255))
    d.ellipse((286, 230, 326, 262), fill=(255, 255, 255))
    d.ellipse((198, 236, 218, 258), fill=(30, 30, 40))
    d.ellipse((298, 236, 318, 258), fill=(30, 30, 40))
    d.arc((216, 290, 296, 340), 20, 160, fill=(90, 40, 40), width=5)
    if visor:
        d.rectangle((150, 220, 362, 272), fill=visor)
    d.rectangle((150, 440, 230, 470), fill=(230, 200, 60))
    img = img.filter(ImageFilter.SMOOTH)
    img.save(path, optimize=True)


def rock_texture(path):
    w = h = 256
    n = fractal(w, h, (32, 16, 8, 4), True)
    img = Image.new("RGB", (w, h))
    img.putdata([(int(80 + v * 110), int(70 + v * 95), int(65 + v * 80)) for v in n])
    img.save(path, optimize=True)


def planet_texture(path, base, spots):
    w, h = 512, 256
    n = fractal(w, h, (64, 32, 16, 8), True)
    img = Image.new("RGB", (w, h))
    data = []
    for y in range(h):
        for x in range(w):
            v = n[y * w + x]
            heart = math.exp(-(((x - w * 0.55) / (w * 0.08)) ** 2 + ((y - h * 0.55) / (h * 0.15)) ** 2)) if spots else 0
            data.append(tuple(int(min(255, base[i] * (0.6 + 0.7 * v) + heart * 70)) for i in range(3)))
    img.putdata(data)
    img.save(path, optimize=True)


def stripe_png(color):
    img = Image.new("RGB", (64, 64), (255, 255, 255))
    d = ImageDraw.Draw(img)
    for y in range(0, 64, 16):
        d.rectangle((0, y, 64, y + 6), fill=tuple(int(c * 255) for c in color))
    import io
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


# --------------------------------------------------------------------------- audio

RATE = 22050


def write_wav(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 30000)) for s in samples))


def env(i, n, attack=0.01, release=0.3):
    t = i / n
    a = min(1.0, t / attack) if attack > 0 else 1.0
    r = min(1.0, (1 - t) / release) if release > 0 else 1.0
    return a * r


def tone(freq_fn, dur, wave_fn="square", vol=0.5, attack=0.005, release=0.5):
    n = int(RATE * dur)
    out_ = []
    phase = 0.0
    for i in range(n):
        f = freq_fn(i / n)
        phase += f / RATE
        p = phase % 1.0
        if wave_fn == "square":
            v = 1.0 if p < 0.5 else -1.0
        elif wave_fn == "saw":
            v = 2 * p - 1
        elif wave_fn == "tri":
            v = 4 * abs(p - 0.5) - 1
        else:
            v = math.sin(2 * math.pi * p)
        out_.append(v * vol * env(i, n, attack, release))
    return out_


def noise(dur, vol=0.6, lowpass=0.3, release=0.9, pitch_drop=False):
    n = int(RATE * dur)
    out_, y = [], 0.0
    for i in range(n):
        k = lowpass * (1 - i / n * 0.8) if pitch_drop else lowpass
        y += (random.uniform(-1, 1) - y) * k
        out_.append(y * vol * env(i, n, 0.002, release))
    return out_


def mix(*tracks):
    n = max(len(t) for t in tracks)
    return [sum(t[i] for t in tracks if i < len(t)) for i in range(n)]


def sounds():
    write_wav(out("sounds", "shoot.wav"), tone(lambda t: 1400 - 900 * t, 0.09, "square", 0.25))
    write_wav(out("sounds", "hit.wav"), noise(0.05, 0.5, 0.6))
    write_wav(out("sounds", "explosion.wav"), noise(0.6, 0.9, 0.25, pitch_drop=True))
    write_wav(out("sounds", "big_explosion.wav"),
              mix(noise(1.4, 1.0, 0.12, pitch_drop=True), tone(lambda t: 90 - 50 * t, 1.2, "sine", 0.5)))
    write_wav(out("sounds", "bomb.wav"), tone(lambda t: 200 + 500 * t, 0.3, "saw", 0.35))
    write_wav(out("sounds", "player_hit.wav"), tone(lambda t: 300 - 200 * t, 0.25, "square", 0.4))
    notes = [523, 659, 784, 1047]
    write_wav(out("sounds", "powerup.wav"), tone(lambda t: notes[min(3, int(t * 4))], 0.35, "square", 0.3, release=0.2))
    notes2 = [392, 523, 659, 784, 1047, 1319]
    write_wav(out("sounds", "extra_life.wav"), tone(lambda t: notes2[min(5, int(t * 6))], 0.7, "tri", 0.45, release=0.15))
    write_wav(out("sounds", "menu_move.wav"), tone(lambda t: 880, 0.04, "square", 0.2, release=0.5))
    write_wav(out("sounds", "menu_select.wav"), tone(lambda t: 660 if t < 0.5 else 990, 0.14, "square", 0.25))
    notes3 = [392, 349, 311, 262]
    write_wav(out("sounds", "game_over.wav"), tone(lambda t: notes3[min(3, int(t * 4))], 1.6, "tri", 0.5, release=0.2))
    # Typewriter loop: one short click per 0.11 s cycle.
    click = noise(0.012, 0.6, 0.9, release=0.8) + tone(lambda t: 1800, 0.006, "square", 0.08)
    write_wav(out("sounds", "type.wav"), click + [0.0] * (int(RATE * 0.11) - len(click)))


def music(path, bpm, root, progression, lead_scale, bars=8, mood="drive"):
    beat = 60.0 / bpm
    total = int(RATE * beat * 4 * bars)
    buf = [0.0] * total

    def add_note(start, dur, freq, vol, shape):
        s = int(start * RATE)
        n = int(dur * RATE)
        phase = 0.0
        for i in range(n):
            if s + i >= total:
                break
            phase += freq / RATE
            p = phase % 1.0
            if shape == "square":
                v = 0.6 if p < 0.5 else -0.6
            elif shape == "tri":
                v = 4 * abs(p - 0.5) - 1
            elif shape == "saw":
                v = (2 * p - 1) * 0.7
            else:
                v = math.sin(2 * math.pi * p)
            buf[s + i] += v * vol * env(i, n, 0.01, 0.4)

    def add_drum(start, kind):
        s = int(start * RATE)
        if kind == "kick":
            n = int(0.15 * RATE)
            for i in range(n):
                if s + i < total:
                    f = 120 - 80 * i / n
                    buf[s + i] += math.sin(2 * math.pi * f * i / RATE) * 0.5 * (1 - i / n)
        else:
            n = int((0.04 if kind == "hat" else 0.12) * RATE)
            y = 0.0
            for i in range(n):
                if s + i < total:
                    y += (random.uniform(-1, 1) - y) * (0.9 if kind == "hat" else 0.4)
                    buf[s + i] += y * (0.12 if kind == "hat" else 0.3) * (1 - i / n)

    freq = lambda semis: root * 2 ** (semis / 12)
    for bar in range(bars):
        chord = progression[bar % len(progression)]
        t0 = bar * 4 * beat
        if mood == "ambient":
            for k in chord:
                add_note(t0, 4 * beat, freq(k), 0.08, "tri")
            add_note(t0, 4 * beat, freq(chord[0] - 12), 0.12, "sine")
            for step in range(8):
                if random.random() < 0.55:
                    add_note(t0 + step * beat / 2, beat * 0.9, freq(random.choice(lead_scale) + 12), 0.05, "sine")
        else:
            for step in range(8):
                add_note(t0 + step * beat / 2, beat / 2 * 0.9, freq(chord[0] - 12), 0.16, "saw")
            for step in range(16):
                add_note(t0 + step * beat / 4, beat / 4 * 0.8, freq(chord[step % len(chord)] + 12), 0.05, "square")
            for b in range(4):
                add_drum(t0 + b * beat, "kick" if b % 2 == 0 else "snare")
                add_drum(t0 + b * beat + beat / 2, "hat")
            if bar % 2 == 1:
                for step in range(4):
                    add_note(t0 + step * beat, beat * 0.9, freq(random.choice(lead_scale) + 12), 0.07, "tri")
    peak = max(abs(v) for v in buf) or 1.0
    write_wav(path, [v / peak * 0.8 for v in buf])


def music_tracks():
    minor = [0, 3, 5, 7, 10, 12]
    music(out("music", "title.wav"), 70, 110.0, [[0, 3, 7], [-4, 0, 3], [-2, 2, 5], [-5, -2, 2]], minor, bars=4, mood="ambient")
    music(out("music", "level1.wav"), 132, 110.0, [[0, 3, 7], [0, 3, 7], [-4, 0, 3], [-2, 2, 5]], minor, bars=8)
    music(out("music", "level2.wav"), 140, 98.0, [[0, 3, 7], [5, 8, 12], [3, 7, 10], [-2, 2, 5]], minor, bars=8)
    music(out("music", "dialog.wav"), 60, 130.8, [[0, 4, 7], [-3, 0, 4], [-7, -3, 0], [-5, -1, 2]], [0, 2, 4, 7, 9], bars=4,
          mood="ambient")


# --------------------------------------------------------------------------- main

def main():
    print("models...")
    player_arrow(out("models", "player_arrow.glb"), (0.75, 0.8, 0.88), (0.2, 0.55, 0.95))
    player_delta(out("models", "player_delta.glb"), (0.85, 0.82, 0.75), (0.9, 0.4, 0.15))
    player_twin(out("models", "player_twin.glb"), (0.3, 0.35, 0.4), (0.25, 0.85, 0.45))
    enemy_drone(out("models", "enemy_drone.glb"))
    enemy_saucer(out("models", "enemy_saucer.glb"))
    enemy_fighter(out("models", "enemy_fighter.glb"))
    boss_cruiser(out("models", "boss_cruiser.glb"))
    boss_sentinel(out("models", "boss_sentinel.glb"))
    item_capsule(out("models", "item_health.glb"), (0.3, 1.0, 0.4), (0.1, 0.5, 0.15))
    item_capsule(out("models", "item_life.glb"), (1.0, 0.85, 0.25), (0.6, 0.4, 0.0))
    item_capsule(out("models", "item_bomb.glb"), (1.0, 0.4, 0.15), (0.5, 0.1, 0.0))
    item_capsule(out("models", "item_maxhealth.glb"), (0.4, 0.6, 1.0), (0.1, 0.2, 0.6))
    asteroid(out("models", "asteroid.glb"))
    planet(out("models", "pluto.glb"), "pluto.png")
    planet(out("models", "charon.glb"), "charon.png")
    station(out("models", "station.glb"))

    print("textures...")
    rock_texture(out("textures", "rock.png"))
    planet_texture(out("textures", "pluto.png"), (200, 170, 140), True)
    planet_texture(out("textures", "charon.png"), (140, 140, 150), False)

    print("images...")
    title_image(out("images", "title.png"))
    dialog_space(out("images", "bg_orbit.png"))
    dialog_bridge(out("images", "bg_bridge.png"))
    dialog_surface(out("images", "bg_surface.png"))
    starfield(1024, 576, 700, True, (25, 15, 60)).save(out("images", "space_landscape.png"), optimize=True)
    starfield(576, 1024, 700, True, (15, 30, 60)).save(out("images", "space_portrait.png"), optimize=True)
    portrait(out("images", "portrait_commander.png"), (205, 160, 120), (40, 60, 110))
    portrait(out("images", "portrait_pilot.png"), (120, 85, 60), (110, 50, 40), visor=(60, 200, 255))
    portrait(out("images", "portrait_ai.png"), None, None, ai=True)

    print("audio...")
    sounds()
    music_tracks()
    print("done ->", os.path.normpath(ROOT))


if __name__ == "__main__":
    main()
