"""The living wall as a mesh: exposed muscle in a rusty frame (Blender, headless).

  blender.exe -b --python Scripts/blender/os_living_wall_mesh.py -- <out_dir>

Writes SM_OS_LivingWall_Square.fbx (4 x 4 m in a rusty frame) and
SM_OS_LivingWall_Round.fbx (4 m across in a fleshy lip: crater membranes, cave plugs)
plus preview renders into out_dir (not the repository). Procedural, seeded,
built like flayed anatomy (owner, 17-18.10: solid muscle tissue, not a net, not a
set of fibres):
  - behind: one continuous muscular mass across the whole opening;
  - fusiform bellies, each ONE solid fluted body (the fibres are the texture
    along it), wide enough to touch and overlap and sinking into the mass: two
    interleaving fans from opposite corners, short ones in the other corners;
    round: the same, no radial symmetry (owner, 18.10: a sphincter of radial
    bellies read as a flower). No straps on top, no pale tendon ribbons or
    connective threads (owner, 18.10: straps and "bones" looked wrong);
  - a few dark branching vessels;
  - square: the frame with brackets and bolts; round: no metal - a ragged fleshy lip
    in the planet's skin, rolled over both faces and flaring into the ground
    (owner, 18.10: it must grow out of the planet, not look industrial).
Wall space as AOSLivingWall's (UE axes, here in metres): width along X, height
along Z, the front toward +Y, the centre at the origin.

The mesh stays whole: AOSLivingWall cuts its holes in the material (M_OS_Gritty,
LivingHoles) by position in the mesh's own space; UV0 runs along every strand
(u around, v along), so the fibre texture follows it.
Material slots: 0 Muscle, 1 Tendon, 2 Vessel, 3 Membrane, 4 FrameMetal (round: the lip,
given the planet's skin by Scripts/os_flesh.py skin_wall).
"""
import math
import os
import sys

import bmesh
import bpy
import mathutils
import numpy as np

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = ARGS[0] if ARGS else os.path.join(os.environ.get("TEMP", "."), "os_wall")
SIZE = 4.0
INNER = 1.8            # the frame's inner edge (square half-size / ring radius)
FRAME_DEPTH = 0.11     # half depth of the frame
SLOTS = ["Muscle", "Tendon", "Vessel", "Membrane", "FrameMetal"]
MUSCLE, TENDON, VESSEL, MEMBRANE, METAL = range(5)
COLOURS = [(0.55, 0.12, 0.1, 1), (0.85, 0.75, 0.6, 1), (0.35, 0.05, 0.1, 1), (0.8, 0.6, 0.55, 1), (0.3, 0.27, 0.25, 1)]


class Parts:
    """Geometry in UE axes: vertices, per-vertex UV0, faces (vertex index lists), their material, frame flag."""

    def __init__(self):
        self.verts, self.uvs, self.faces, self.mats, self.frame = [], [], [], [], []

    def add(self, verts, uvs, faces, mat, frame=False):
        base = len(self.verts)
        self.verts.extend(map(tuple, verts))
        self.uvs.extend(map(tuple, uvs))
        for f in faces:
            self.faces.append([base + i for i in f])
            self.mats.append(mat)
            self.frame.append(frame)


def spline(points, step=0.04):
    """Catmull-Rom through the points, resampled about every step metres."""
    p = np.asarray(points, dtype=np.float64)
    p = np.vstack([2 * p[0] - p[1], p, 2 * p[-1] - p[-2]])
    out = []
    for i in range(1, len(p) - 2):
        p0, p1, p2, p3 = p[i - 1], p[i], p[i + 1], p[i + 2]
        n = max(2, int(np.linalg.norm(p2 - p1) / step))
        for t in np.linspace(0, 1, n, endpoint=False):
            out.append(0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3))
    out.append(p[-2])
    return np.array(out)


def frames(path):
    """Tangents and a parallel-transported normal/binormal along a path."""
    t = np.gradient(path, axis=0)
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    n = np.cross(t[0], (0.0, 1.0, 0.0))
    if np.linalg.norm(n) < 1e-3:
        n = np.cross(t[0], (1.0, 0.0, 0.0))
    n /= np.linalg.norm(n)
    ns = []
    for k in range(len(path)):
        n = n - t[k] * np.dot(n, t[k])
        n /= np.linalg.norm(n)
        ns.append(n.copy())
    ns = np.array(ns)
    return t, ns, np.cross(t, ns)


def tube(parts, path, radii, sides, mat, flatten=1.0, cap=True, frame=False):
    """A tube along path; UV0: u around, v along (length over the mean circumference). Ends closed by fans."""
    path = np.asarray(path, dtype=np.float64)
    radii = np.broadcast_to(np.asarray(radii, dtype=np.float64), (len(path),))
    t, n, b = frames(path)
    seg = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))])
    circ = 2 * math.pi * max(float(radii.mean()), 1e-3)
    verts, uvs = [], []
    for k in range(len(path)):
        for j in range(sides + 1):
            a = 2 * math.pi * j / sides
            off = radii[k] * (math.cos(a) * n[k] + math.sin(a) * b[k])
            off[1] *= flatten
            verts.append(path[k] + off)
            uvs.append((j / sides, seg[k] / circ))
    faces = []
    cols = sides + 1
    for k in range(len(path) - 1):
        for j in range(sides):
            a = k * cols + j
            faces.append([a, a + 1, a + cols + 1, a + cols])
    if cap:
        for k, flip in ((0, True), (len(path) - 1, False)):
            centre = len(verts)
            verts.append(path[k])
            uvs.append((0.5, seg[k] / circ))
            for j in range(sides):
                f = [k * cols + j, k * cols + j + 1, centre]
                faces.append(f[::-1] if flip else f)
    parts.add(verts, uvs, faces, mat, frame)


def box(parts, centre, half, rot=0.0, mat=METAL):
    """An axis box (half extents, UE axes) turned by rot around Y; box-mapped UV0 (one tile per 0.5 m)."""
    c, s = math.cos(rot), math.sin(rot)
    R = np.array([[c, 0, -s], [0, 1, 0], [s, 0, c]])
    hx, hy, hz = half
    for axis in range(3):
        for sign in (-1, 1):
            u_ax, v_ax = [a for a in range(3) if a != axis]
            quad = []
            for du, dv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                p = np.zeros(3)
                p[axis] = sign * half[axis]
                p[u_ax] = du * half[u_ax]
                p[v_ax] = dv * half[v_ax]
                quad.append(p)
            out = np.zeros(3)
            out[axis] = sign
            if np.dot(np.cross(quad[1] - quad[0], quad[2] - quad[0]), out) < 0:
                quad = quad[::-1]
            world = [np.asarray(centre) + R @ q for q in quad]
            uvs = [(w[u_ax] * 2.0, w[v_ax] * 2.0) for w in world]
            parts.add(world, uvs, [[0, 1, 2, 3]], mat, frame=True)


def bolt(parts, at):
    head = np.array([[0.0, 0.0, 0.0], [0.0, 0.03, 0.0]]) + np.asarray(at)
    tube(parts, head, 0.025, 8, METAL, frame=True)


# --- the frame ----------------------------------------------------------------------------------
def frame_square(parts):
    w = (2.0 - INNER) / 2
    for z in (-1, 1):
        box(parts, (0.0, 0.0, z * (INNER + w)), (2.0, FRAME_DEPTH, w))
    for x in (-1, 1):
        box(parts, (x * (INNER + w), 0.0, 0.0), (w, FRAME_DEPTH, INNER))
    for a in anchors_square():
        d = -a / np.linalg.norm(a)
        rot = math.atan2(d[0], d[2])
        box(parts, a * 1.0 + d * 0.0, (0.13, FRAME_DEPTH * 1.25, 0.05), rot)
    for x in (-1.9, -0.95, 0.95, 1.9):
        for z in (-1.9, 1.9):
            bolt(parts, (x, FRAME_DEPTH, z))
            bolt(parts, (z, FRAME_DEPTH, x))


def anchors_square():
    c = INNER * 0.96
    return [np.array(p) for p in ((-c, 0, -c), (0, 0, -INNER), (c, 0, -c), (INNER, 0, 0), (c, 0, c), (0, 0, INNER), (-c, 0, c), (-INNER, 0, 0))]


def lip_round(parts, rng):
    """The round wall's rim: a ragged fleshy lip over the edge of the tissue, rolled over both faces
    and flaring out and back into the ground (crater floor, tunnel wall) around it. A closed profile
    turned around the axis; its crest height and radii wander with the angle."""
    segs = 160
    mid = -0.11  # the tissue's mid-depth: the back half mirrors the front about it
    front = [(1.62, -0.06), (1.70, 0.04), (1.79, 0.13), (1.91, 0.18), (2.03, 0.14), (2.16, 0.04), (2.30, -0.04), (2.45, -0.11)]  # the outer edge at mid-depth: under the ground
    prof = front + [(r, 2 * mid - y) for r, y in front[::-1]]
    waves = [(k, rng.uniform(0.3, 1.0) / k ** 0.6, rng.uniform(0, 2 * math.pi), rng.uniform(0, 2 * math.pi)) for k in range(2, 9)]
    verts, uvs = [], []
    for i in range(segs + 1):
        th = 2 * math.pi * i / segs
        swell = sum(a * math.sin(k * th + p) for k, a, p, _ in waves) / 2.2       # ~-1..1, crest height
        drift = sum(a * math.sin(k * th + q) for k, a, _, q in waves) / 2.2       # ~-1..1, how far it reaches in
        for j, (r, y) in enumerate(prof):
            rim = 1.0 - min(abs(r - 1.91) / 0.55, 1.0)                           # 1 at the crest, 0 at the ends
            rr = r + 0.14 * drift * (1.0 - (r - 1.62) / 0.83)
            yy = mid + (y - mid) * (1.0 + 0.9 * swell * rim)  # swollen lobes, and places it nearly sinks away
            verts.append((rr * math.cos(th), yy, rr * math.sin(th)))
            uvs.append((th / (2 * math.pi) * 12.0, j / (len(prof) - 1) * 2.0))
    cols = len(prof)
    faces = [[i * cols + j, (i + 1) * cols + j, (i + 1) * cols + j + 1, i * cols + j + 1]
             for i in range(segs) for j in range(cols - 1)]
    parts.add(verts, uvs, faces, METAL)


# --- tissue: exposed anatomy, in layers ---------------------------------------------------------------
Y = np.array((0.0, 1.0, 0.0))


def flat_tube(parts, path, r_w, r_d, sides, mat, grooves=0, groove=0.0):
    """A tube whose section is an ellipse: r_w across in the wall plane, r_d in depth (Y). UV0: u around, v along.
    grooves > 0: the surface is fluted along the tube (that many ridges, relative depth groove), slightly twisting."""
    path = np.asarray(path, dtype=np.float64)
    r_w = np.broadcast_to(np.asarray(r_w, dtype=np.float64), (len(path),))
    r_d = np.broadcast_to(np.asarray(r_d, dtype=np.float64), (len(path),))
    t = np.gradient(path, axis=0)
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    w = np.cross(t, Y)
    w /= np.maximum(np.linalg.norm(w, axis=1, keepdims=True), 1e-6)
    d = np.cross(w, t)
    seg = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))])
    circ = 2 * math.pi * max(float(np.mean(np.maximum(r_w, r_d))), 1e-3)
    verts, uvs = [], []
    for k in range(len(path)):
        for j in range(sides + 1):
            a = 2 * math.pi * j / sides
            flute = 1.0 + groove * math.sin(grooves * a + 0.8 * seg[k]) if grooves else 1.0
            verts.append(path[k] + flute * (r_w[k] * math.cos(a) * w[k] + r_d[k] * math.sin(a) * d[k]))
            uvs.append((j / sides, seg[k] / circ))
    cols = sides + 1
    faces = [[k * cols + j, k * cols + j + 1, (k + 1) * cols + j + 1, (k + 1) * cols + j] for k in range(len(path) - 1) for j in range(sides)]
    for k, flip in ((0, True), (len(path) - 1, False)):
        centre = len(verts)
        verts.append(path[k])
        uvs.append((0.5, seg[k] / circ))
        for j in range(sides):
            f = [k * cols + j, k * cols + j + 1, centre]
            faces.append(f[::-1] if flip else f)
    parts.add(verts, uvs, faces, mat)
    return w, d


def belly(parts, rng, a, b, r_max, bend=0.0, depth=0.0, flat=0.45, path=None):
    """A fusiform muscle from a to b: ONE closed solid body, thick in the middle, tapering to both
    ends, fluted along its length (the fibres are the texture along it and these grooves). It grows
    straight out of the frame or lip at a: no pale tendon ribbons (owner, 18.10: they read as
    bones). Returns its path."""
    if path is None:
        a, b = np.asarray(a, float), np.asarray(b, float)
        ab = b - a
        side = np.cross(ab, Y)
        side /= np.linalg.norm(side)
        ctrl = [a, a + ab * 0.33 + side * bend * 0.8 + Y * depth, a + ab * 0.66 + side * bend + Y * depth, b]
        path = spline(ctrl, 0.04)
    seg = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))])
    s = seg / seg[-1]
    u = s
    # Fusiform: the belly swells off-centre a little, like a real one.
    peak = rng.uniform(0.4, 0.6)
    shape = np.where(u < peak, np.sin(0.5 * math.pi * u / peak), np.sin(0.5 * math.pi * (1 - u) / (1 - peak))) ** 0.7
    r = 0.015 + r_max * shape
    flat_tube(parts, path, r, r * flat, 24, MUSCLE, grooves=int(rng.integers(11, 16)), groove=0.0)  # no flutes (in game they read as yarn); the draw keeps the layout seed
    return path

def cords(parts, rng, paths, count):
    """Vessels and nerves: a few branching cords along the borders between muscles, in front."""
    for _ in range(count):
        i, j = rng.choice(len(paths), 2, replace=False)
        A, B = paths[i], paths[j]
        n = min(len(A), len(B))
        line = []
        for k in np.linspace(0.1 * n, 0.9 * n, 12).astype(int):
            p = A[min(k, len(A) - 1)]
            q = B[int(np.argmin(np.linalg.norm(B - p, axis=1)))]
            if np.linalg.norm(q - p) > 0.8:
                continue
            line.append((p + q) / 2 + Y * 0.09)
        if len(line) < 3:
            continue
        main = spline(line, 0.05)
        tube(parts, main, np.linspace(0.016, 0.008, len(main)), 6, VESSEL)
        for _ in range(int(rng.integers(2, 4))):
            k = int(rng.integers(1, len(main) - 1))
            t = main[min(k + 1, len(main) - 1)] - main[k - 1]
            out = np.cross(t, Y)
            out /= max(np.linalg.norm(out), 1e-6)
            out *= rng.choice((-1, 1))
            end = main[k] + out * rng.uniform(0.15, 0.35) + t * rng.uniform(0.5, 2.0)
            br = spline([main[k], (main[k] + end) / 2 + out * 0.04, end], 0.04)
            tube(parts, br, np.linspace(0.009, 0.004, len(br)), 5, VESSEL)


def fascia(parts, rng, round_):
    """The back layer: a continuous muscular mass across the whole opening (front and back
    surfaces), bulging, its fibres diagonal in UV0 (clipped to a disc on a round wall). The
    bellies sink into it."""
    n = 72
    lim = INNER + 0.04
    front, back, uvs = [], [], []
    for i in range(n + 1):
        for k in range(n + 1):
            x, z = -lim + 2 * lim * i / n, -lim + 2 * lim * k / n
            r = math.hypot(x, z)
            bulge = 0.025 * math.sin(x * 3.3 + 0.4) * math.sin(z * 2.9 - 0.8) + 0.012 * math.sin(x * 8.1 - z * 6.7)
            y = -0.06 + bulge
            uvs.append(((x - z) * 0.7071 * 0.8, (x + z) * 0.7071 * 1.6))
            # Flutes along the fibres on the front surface.
            y += 0.006 * math.sin((x - z) * 0.7071 * 55.0)
            front.append((x, y, z))
            back.append((x, -0.17 - 0.02 * (1 - min(1.0, r / lim)), z))
    faces = []
    for i in range(n):
        for k in range(n):
            x, z = -lim + 2 * lim * (i + 0.5) / n, -lim + 2 * lim * (k + 0.5) / n
            if round_ and x * x + z * z > lim * lim:
                continue
            q = i * (n + 1) + k
            faces.append([q, q + n + 1, q + n + 2, q + 1])
    parts.add(front, uvs, [f[::-1] for f in faces], MUSCLE)
    parts.add(back, uvs, faces, MUSCLE)

def tissue_square(parts, rng):
    fascia(parts, rng, False)
    c = INNER * 0.95
    paths = []
    # Fan A from the lower-left corner and fan B from the upper-right: wide bellies that touch and
    # overlap each other and sink into the mass behind; their fingers interleave in the middle.
    for ang, length, r in ((12, 3.0, 0.33), (27, 2.75, 0.36), (42, 2.9, 0.32), (57, 3.05, 0.35), (72, 2.75, 0.31), (85, 2.3, 0.27)):
        a = np.array((-c, 0.0, -c))
        dirn = np.array((math.cos(math.radians(ang)), 0.0, math.sin(math.radians(ang))))
        paths.append(belly(parts, rng, a, a + dirn * length * rng.uniform(0.95, 1.05), r, bend=rng.uniform(-0.1, 0.1),
                           depth=rng.uniform(-0.02, 0.02)))
    for ang, length, r in ((191, 2.8, 0.34), (206, 2.65, 0.37), (221, 2.95, 0.33), (237, 2.7, 0.32), (252, 2.5, 0.3), (265, 2.1, 0.26)):
        a = np.array((c, 0.0, c))
        dirn = np.array((math.cos(math.radians(ang)), 0.0, math.sin(math.radians(ang))))
        paths.append(belly(parts, rng, a, a + dirn * length * rng.uniform(0.95, 1.05), r, bend=rng.uniform(-0.1, 0.1),
                           depth=rng.uniform(0.0, 0.04)))
    # The other two corners: short fused bellies.
    for corner, angles in (((-c, 0.0, c), (-18, -45, -72)), ((c, 0.0, -c), (162, 135, 108))):
        for ang in angles:
            dirn = np.array((math.cos(math.radians(ang)), 0.0, math.sin(math.radians(ang))))
            paths.append(belly(parts, rng, corner, np.array(corner) + dirn * rng.uniform(1.1, 1.5), rng.uniform(0.27, 0.32),
                               bend=rng.uniform(-0.08, 0.08), depth=rng.uniform(-0.03, 0.0)))
    cords(parts, rng, paths, 6)

def tissue_round(parts, rng):
    """The square wall's anatomy in a disc, nothing radial: two opposed fans whose fingers
    interleave, their origins spread along the rim under the lip; short fused bellies from the other
    two sides."""
    fascia(parts, rng, True)
    R = INNER * 0.97
    def rim(deg):
        return np.array((R * math.cos(math.radians(deg)), 0.0, R * math.sin(math.radians(deg))))
    def along(origin, dev):
        inward = math.atan2(-origin[2], -origin[0]) + math.radians(dev)
        d = np.array((math.cos(inward), 0.0, math.sin(inward)))
        return d, -2.0 * float(np.dot(origin, d))  # the chord to the far rim
    paths = []
    for base, devs in ((228, (-34, -19, -5, 9, 23, 37)), (41, (-37, -22, -8, 6, 20, 33))):
        for dev in devs:
            a = rim(base + dev * 0.4 + rng.uniform(-5, 5))
            d, chord = along(a, dev)
            paths.append(belly(parts, rng, a, a + d * chord * rng.uniform(0.7, 0.86), rng.uniform(0.3, 0.36),
                               bend=rng.uniform(-0.12, 0.12), depth=rng.uniform(-0.02, 0.04)))
    for base in (133, 312):
        # Side by side along the rim, nearly parallel (from one point they made a star).
        for step in (-1, 0, 1):
            a = rim(base + step * 22 + rng.uniform(-4, 4))
            d, chord = along(a, -step * 14 + rng.uniform(-6, 6))
            paths.append(belly(parts, rng, a, a + d * chord * rng.uniform(0.35, 0.5), rng.uniform(0.26, 0.32),
                               bend=rng.uniform(-0.08, 0.08), depth=rng.uniform(-0.03, 0.0)))
    cords(parts, rng, paths, 4)


def to_bmesh(parts):
    """Into a bmesh, Y mirrored (the FBX round trip turns Blender +Y into UE -Y) and the winding reversed."""
    bm = bmesh.new()
    uv0 = bm.loops.layers.uv.new("UV0")
    verts = [bm.verts.new((v[0], -v[1], v[2])) for v in parts.verts]
    for f, mat, is_frame in zip(parts.faces, parts.mats, parts.frame):
        idx = f[::-1]
        try:
            face = bm.faces.new([verts[i] for i in idx])
        except ValueError:
            continue
        face.material_index = mat
        face.smooth = not is_frame
        for loop, i in zip(face.loops, idx):
            loop[uv0].uv = parts.uvs[i]
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    return bm


def raw_meat(bm, seed):
    """Raw meat, not smooth bellies (owner's reference, 18.10): every muscle vertex pushed along its
    normal - lumps a few cm to a hand across, sunk along torn cracks between them."""
    bm.normal_update()
    off = mathutils.Vector((seed * 7.1, seed * 3.3, seed * 5.7))
    moves = []
    for v in bm.verts:
        if not any(f.material_index == MUSCLE for f in v.link_faces):
            continue
        lumps = mathutils.noise.noise(v.co * 7.0 + off, noise_basis="PERLIN_ORIGINAL")
        crack = mathutils.noise.noise(v.co * 5.5 + off, noise_basis="VORONOI_F2F1")  # 0 on a chunk border
        moves.append((v, v.normal * (0.02 * lumps + 0.04 * math.sqrt(min(max(crack, 0.0) * 2.5, 1.0)) - 0.03)))
    for v, move in moves:
        v.co += move


def build(name, round_, seed):
    rng = np.random.default_rng(seed)
    parts = Parts()
    if round_:
        lip_round(parts, rng)
    else:
        frame_square(parts)
    (tissue_round if round_ else tissue_square)(parts, rng)
    bm = to_bmesh(parts)
    raw_meat(bm, seed)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    for slot, colour in zip(SLOTS, COLOURS):
        mat = bpy.data.materials.get(slot) or bpy.data.materials.new(slot)
        mat.diffuse_color = colour
        mesh.materials.append(mat)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    print("OSWALL: %s %d triangles, %d vertices" % (name, len(mesh.polygons), len(mesh.vertices)))
    return obj


def export(obj, path):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, mesh_smooth_type="FACE", use_triangles=True,
                             add_leaf_bones=False, bake_anim=False, axis_forward="-Z", axis_up="Y")
    print("OSWALL: wrote %s" % path)


def preview(obj, path):
    """Workbench render from the front (Blender -Y is UE +Y)."""
    scene = bpy.context.scene
    for o in scene.collection.objects:
        o.hide_render = o is not obj and o.type == "MESH"
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.render.resolution_x = scene.render.resolution_y = 900
    cam = bpy.data.objects.get("Cam") or bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    if cam.name not in scene.collection.objects:
        scene.collection.objects.link(cam)
    cam.data.lens = 50
    cam.location = (0.0, -8.0, 0.0)
    cam.rotation_euler = (math.radians(90), 0.0, 0.0)
    scene.camera = cam
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for name, round_, seed in (("SM_OS_LivingWall_Square", False, 3), ("SM_OS_LivingWall_Round", True, 4)):
        obj = build(name, round_, seed)
        export(obj, os.path.join(OUT, name + ".fbx"))
        preview(obj, os.path.join(OUT, name + ".png"))


main()
