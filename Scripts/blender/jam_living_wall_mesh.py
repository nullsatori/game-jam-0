"""The living wall as a mesh: exposed muscle in a rusty frame (Blender, headless).

  blender.exe -b --python Scripts/blender/jam_living_wall_mesh.py -- <out_dir>

Writes SM_Jam_LivingWall_Square.fbx (4 x 4 m in a rusty frame) and
SM_Jam_LivingWall_Round.fbx (4 m across in a ring: crater membranes, cave plugs)
plus preview renders into out_dir (not the repository). Procedural, seeded,
built like flayed anatomy in layers (owner, 17.10: muscle tissue, not a net):
  - back: a sagging fascia membrane across the whole opening;
  - fusiform muscle bellies (many thin, slightly twisted fibres over a core,
    fibres along the belly) in two interleaving fans from opposite corners and
    two straps across on top; round: a radial fan converging on a ring knot;
  - pale flat aponeuroses where bellies meet the frame, widening onto it;
  - stringy connective threads across the gaps, a few branching vessels and
    nerves along the borders between muscles;
  - the frame with brackets and bolts.
Wall space as AJamLivingWall's (UE axes, here in metres): width along X, height
along Z, the front toward +Y, the centre at the origin.

The mesh stays whole: AJamLivingWall cuts its holes in the material (M_Jam_Gritty,
LivingHoles) by position in the mesh's own space; UV0 runs along every strand
(u around, v along), so the fibre texture follows it.
Material slots: 0 Muscle, 1 Tendon, 2 Vessel, 3 Membrane, 4 FrameMetal.
"""
import math
import os
import sys

import bmesh
import bpy
import mathutils
import numpy as np

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = ARGS[0] if ARGS else os.path.join(os.environ.get("TEMP", "."), "jam_wall")
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


def frame_round(parts):
    segs = 64
    verts, uvs, faces = [], [], []
    prof = [(INNER, -FRAME_DEPTH), (INNER, FRAME_DEPTH), (2.0, FRAME_DEPTH), (2.0, -FRAME_DEPTH)]
    for k in range(segs + 1):
        a = 2 * math.pi * k / segs
        for r, y in prof + [prof[0]]:
            verts.append((r * math.cos(a), y, r * math.sin(a)))
            uvs.append((k / segs * 24.0, (r + y) * 2.0))
    cols = len(prof) + 1
    for k in range(segs):
        for j in range(len(prof)):
            i = k * cols + j
            faces.append([i, i + cols, i + cols + 1, i + 1])
    parts.add(verts, uvs, faces, METAL, frame=True)
    for a in anchors_round():
        d = -a / np.linalg.norm(a)
        box(parts, a, (0.13, FRAME_DEPTH * 1.25, 0.05), math.atan2(d[0], d[2]))
    for k in range(16):
        a = 2 * math.pi * (k + 0.5) / 16
        bolt(parts, (1.9 * math.cos(a), FRAME_DEPTH, 1.9 * math.sin(a)))


def anchors_square():
    c = INNER * 0.96
    return [np.array(p) for p in ((-c, 0, -c), (0, 0, -INNER), (c, 0, -c), (INNER, 0, 0), (c, 0, c), (0, 0, INNER), (-c, 0, c), (-INNER, 0, 0))]


def anchors_round():
    return [np.array((INNER * math.cos(a), 0.0, INNER * math.sin(a))) for a in np.radians(np.arange(225, 585, 45))]


# --- tissue: exposed anatomy, in layers ---------------------------------------------------------------
Y = np.array((0.0, 1.0, 0.0))


def flat_tube(parts, path, r_w, r_d, sides, mat):
    """A tube whose section is an ellipse: r_w across in the wall plane, r_d in depth (Y). UV0: u around, v along."""
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
            verts.append(path[k] + r_w[k] * math.cos(a) * w[k] + r_d[k] * math.sin(a) * d[k])
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


def belly(parts, rng, a, b, r_max, bend=0.0, depth=0.0, tendon_a=True, tendon_b=True, fibres=13, flat=0.45):
    """A fusiform muscle from a to b: thick in the middle, tapering into tendons; many thin, slightly
    twisted fibres over a core, fibres along it. A tendon end becomes a pale flat ribbon fanning out
    onto its attachment. Returns the belly's path (for vessels and connective threads)."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    ab = b - a
    side = np.cross(ab, Y)
    side /= np.linalg.norm(side)
    ctrl = [a, a + ab * 0.33 + side * bend * 0.8 + Y * depth, a + ab * 0.66 + side * bend + Y * depth, b]
    path = spline(ctrl, 0.04)
    seg = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))])
    s = seg / seg[-1]
    ta = rng.uniform(0.13, 0.2) if tendon_a else 0.0
    tb = 1.0 - (rng.uniform(0.13, 0.2) if tendon_b else 0.0)
    u = np.clip((s - ta) / (tb - ta), 0.0, 1.0)
    # Fusiform: the belly swells off-centre a little, like a real one.
    peak = rng.uniform(0.4, 0.6)
    shape = np.where(u < peak, np.sin(0.5 * math.pi * u / peak), np.sin(0.5 * math.pi * (1 - u) / (1 - peak))) ** 0.8
    r = 0.012 + r_max * shape
    inside = (s >= ta) & (s <= tb)
    idx = np.nonzero(inside)[0]
    core_path = path[idx]
    w, d = flat_tube(parts, core_path, r[idx] * 0.8, r[idx] * 0.8 * flat, 10, MUSCLE)
    twist = rng.uniform(0.3, 0.7) * math.pi
    golden = math.pi * (3 - math.sqrt(5))
    for k in range(fibres):
        rad = math.sqrt((k + 0.5) / fibres)
        ang = k * golden + twist * u[idx]
        off = (r[idx] * 0.85 * rad)[:, None] * (np.cos(ang)[:, None] * w + flat * np.sin(ang)[:, None] * d)
        fr = np.maximum(r[idx] * rng.uniform(0.26, 0.34), 0.006)
        flat_tube(parts, core_path + off, fr, fr * 0.9, 5, MUSCLE)
    # Aponeuroses: pale flat ribbons from the belly to the attachment, widening onto it.
    for end, rng_idx in ((tendon_a, np.nonzero(s <= ta + 0.02)[0]), (tendon_b, np.nonzero(s >= tb - 0.02)[0])):
        if not end or len(rng_idx) < 2:
            continue
        p = path[rng_idx]
        frac = np.linspace(0, 1, len(p))
        at_frame = frac if rng_idx[0] != 0 else frac[::-1]           # 1 at the frame
        width = r[rng_idx].max() * 0.6 + (0.22 - r[rng_idx].max() * 0.6) * at_frame ** 2
        flat_tube(parts, p, width, 0.008 + 0.012 * (1 - at_frame), 8, TENDON)
    return path


def threads(parts, rng, paths, count):
    """Stringy connective fibres stretched across the gaps between neighbouring bellies."""
    made = 0
    for _ in range(count * 6):
        if made >= count:
            break
        i, j = rng.choice(len(paths), 2, replace=False)
        p = paths[i][int(rng.integers(len(paths[i]) // 5, 4 * len(paths[i]) // 5))]
        q = paths[j][int(np.argmin(np.linalg.norm(paths[j] - p, axis=1)))]
        gap = np.linalg.norm(q - p)
        if not 0.12 < gap < 0.6:
            continue
        mid = (p + q) / 2 + Y * rng.uniform(-0.03, 0.02)
        path = spline([p, mid, q], 0.04)
        tube(parts, path, rng.uniform(0.006, 0.014), 5, VESSEL if rng.random() < 0.3 else TENDON)
        made += 1


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
    """The back layer: a sagging translucent membrane across the opening (its thin places are the material's)."""
    n = 72
    lim = INNER + 0.02
    verts, uvs = [], []
    for i in range(n + 1):
        for k in range(n + 1):
            x, z = -lim + 2 * lim * i / n, -lim + 2 * lim * k / n
            rr = min(1.0, (x * x + z * z) / (lim * lim)) if round_ else max(abs(x), abs(z)) ** 2 / lim ** 2
            y = -0.13 - 0.07 * (1 - rr) + 0.012 * math.sin(x * 7.3 + z * 2.1) * math.cos(z * 5.7)
            verts.append((x, y, z))
            uvs.append((x * 0.6, z * 0.6))
    faces = []
    for i in range(n):
        for k in range(n):
            x, z = -lim + 2 * lim * (i + 0.5) / n, -lim + 2 * lim * (k + 0.5) / n
            if round_ and x * x + z * z > lim * lim:
                continue
            a = i * (n + 1) + k
            faces.append([a, a + n + 1, a + n + 2, a + 1])
    parts.add(verts, uvs, faces, MEMBRANE)


def tissue_square(parts, rng):
    fascia(parts, rng, False)
    c = INNER * 0.95
    paths = []
    # Fan A from the lower-left corner and fan B from the upper-right: fingers that interleave
    # across the middle (serratus against an oblique), uneven widths and lengths.
    # The fingers end free in the middle, tapering, overlapping the other fan's.
    for ang, length, r in ((14, 3.0, 0.27), (31, 2.7, 0.31), (48, 2.85, 0.26), (64, 3.05, 0.3), (80, 2.5, 0.22)):
        a = np.array((-c, 0.0, -c))
        dirn = np.array((math.cos(math.radians(ang)), 0.0, math.sin(math.radians(ang))))
        paths.append(belly(parts, rng, a, a + dirn * length * rng.uniform(0.93, 1.05), r, bend=rng.uniform(-0.15, 0.15),
                           depth=rng.uniform(-0.04, 0.0), tendon_b=False))
    for ang, length, r in ((193, 2.8, 0.28), (210, 2.6, 0.32), (227, 2.95, 0.27), (246, 2.65, 0.25), (261, 2.2, 0.2)):
        a = np.array((c, 0.0, c))
        dirn = np.array((math.cos(math.radians(ang)), 0.0, math.sin(math.radians(ang))))
        paths.append(belly(parts, rng, a, a + dirn * length * rng.uniform(0.93, 1.05), r, bend=rng.uniform(-0.15, 0.15),
                           depth=rng.uniform(0.01, 0.06), tendon_b=False))
    # The other two corners: short bellies from each, so the corners are muscle, not a window.
    for corner, angles in (((-c, 0.0, c), (-20, -48)), ((c, 0.0, -c), (160, 128))):
        for ang in angles:
            dirn = np.array((math.cos(math.radians(ang)), 0.0, math.sin(math.radians(ang))))
            paths.append(belly(parts, rng, corner, np.array(corner) + dirn * rng.uniform(1.1, 1.4), rng.uniform(0.2, 0.25),
                               bend=rng.uniform(-0.1, 0.1), depth=rng.uniform(-0.05, -0.01), tendon_b=False, fibres=10))
    # Two long straps on top, crossing diagonally (sartorius-like), and a short one off the left side.
    paths.append(belly(parts, rng, (-c, 0.0, c), (c * 0.9, 0.0, -c), 0.12, bend=0.35, depth=0.16, fibres=9, flat=0.5))
    paths.append(belly(parts, rng, (0.1, 0.0, INNER), (c, 0.0, -0.5), 0.1, bend=-0.2, depth=0.13, fibres=8, flat=0.5))
    paths.append(belly(parts, rng, (-INNER, 0.0, 0.25), (-0.2, 0.0, -INNER), 0.16, bend=0.15, depth=0.08, fibres=10))
    threads(parts, rng, paths, 70)
    cords(parts, rng, paths, 6)


def tissue_round(parts, rng):
    fascia(parts, rng, True)
    paths = []
    # A radial fan of bellies converging on a sphincter-like knot in the middle.
    angles = np.radians(np.array([0, 47, 88, 140, 181, 222, 268, 311]) + rng.uniform(-8, 8, 8))
    for k, ang in enumerate(angles):
        if k == 5:
            continue  # one gap: not too regular
        a = np.array((INNER * 0.97 * math.cos(ang), 0.0, INNER * 0.97 * math.sin(ang)))
        b = a / np.linalg.norm(a) * rng.uniform(0.22, 0.32)
        paths.append(belly(parts, rng, a, b, rng.uniform(0.2, 0.28), bend=rng.uniform(-0.15, 0.15),
                           depth=rng.uniform(-0.02, 0.04), tendon_b=False, fibres=10))
    # The knot: fibres running round the middle, a thick twisted ring.
    for k in range(7):
        ring = [np.array((0.28 * math.cos(t) + 0.03 * math.sin(3 * t + k), 0.05 + 0.03 * math.sin(2 * t + k), 0.28 * math.sin(t)))
                for t in np.linspace(0, 2 * math.pi, 64)]
        tube(parts, ring, 0.07 + 0.02 * math.sin(k), 6, MUSCLE)
    paths.append(belly(parts, rng, (-INNER * 0.9, 0.0, 0.6), (INNER * 0.8, 0.0, -0.8), 0.075, bend=0.3, depth=0.13, fibres=6, flat=0.6))
    threads(parts, rng, paths, 50)
    cords(parts, rng, paths, 4)


# --- mesh -----------------------------------------------------------------------------------------
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


def build(name, round_, seed):
    rng = np.random.default_rng(seed)
    parts = Parts()
    (frame_round if round_ else frame_square)(parts)
    (tissue_round if round_ else tissue_square)(parts, rng)
    bm = to_bmesh(parts)
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
    print("JAMWALL: %s %d triangles, %d vertices" % (name, len(mesh.polygons), len(mesh.vertices)))
    return obj


def export(obj, path):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, mesh_smooth_type="FACE", use_triangles=True,
                             add_leaf_bones=False, bake_anim=False, axis_forward="-Z", axis_up="Y")
    print("JAMWALL: wrote %s" % path)


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
    for name, round_, seed in (("SM_Jam_LivingWall_Square", False, 3), ("SM_Jam_LivingWall_Round", True, 4)):
        obj = build(name, round_, seed)
        export(obj, os.path.join(OUT, name + ".fbx"))
        preview(obj, os.path.join(OUT, name + ".png"))


main()
