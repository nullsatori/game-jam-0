"""The planetoid of Lvl_Jam_Ship: relief, through-caves, craters (Blender, headless).

  blender.exe -b --python Scripts/blender/jam_planet.py -- <out_dir> [voxel_m]

Writes into out_dir (not the repository): SM_Jam_Planet.fbx, SM_Jam_PlanetEye.fbx,
SM_Jam_Tentacle.fbx and preview renders; and Scripts/planet_layout.json next to
this folder (committed: build_ship.py places everything from it).

The planet is a signed distance field sampled on a voxel grid and meshed with
surface nets (one vertex per cell crossing the surface, a quad per crossing
edge), then decimated:
  - a sphere of RADIUS m displaced by height(direction): folds (ridged noise),
    bumps (fbm) and fine grain, scaled into RELIEF;
  - craters: bowls with a flat bottom and a rim, their bottom radially flat;
    a shaft under each bottom down to a tunnel (a living membrane covers it);
  - flats: the nav module's and the shotgun's spots are levelled;
  - tunnels: tubes along great-circle arcs at TUNNEL_DEPTH, a flattened floor
    (the plane FLOOR x radius under the axis, against the radial up), ramps up
    to the surface at mouths, throats (a narrow round section) where a living
    wall plugs them.
All directions are planet-local in UE axes (the planet is placed unrotated);
units: metres here, centimetres in the JSON (UE).
"""
import json
import math
import os
import sys
import time

import bpy
import numpy as np

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = ARGS[0] if ARGS else os.path.join(os.environ.get("TEMP", "."), "jam_planet")
VOXEL = float(ARGS[1]) if len(ARGS) > 1 else 0.5
LAYOUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "planet_layout.json")
SEED = 23
RADIUS = 80.0
RELIEF = (-5.0, 6.0)          # height range of the noise relief, m
TUNNEL_DEPTH = 64.0           # tunnel axis radius from the centre, m
TUNNEL_R = 3.5                # tunnel radius (7 m across)
FLOOR = 0.55                  # floor plane: this x radius under the axis
THROAT_R = 1.85               # plug throats: a 4 m round living wall covers them
THROAT_LEN = 3.0              # half length of a throat's narrowing, m
SHAFT_R = 1.7                 # crater shafts (the 4 m membrane covers them)
CRATER_R, CRATER_FLAT, CRATER_DEPTH, CRATER_RIM = 7.0, 2.4, 2.4, 0.6
RAMP_LEN = 40.0               # mouth ramp length along the surface, m (~25 degrees)
TARGET_TRIS = 280000


def unit(v):
    v = np.asarray(v, dtype=np.float64)
    return v / np.linalg.norm(v)


# --- layout (planet-local directions; -Y faces the ship) --------------------------------------
CRATERS = {"C1": unit((0.35, -0.80, 0.48)), "C2": unit((-0.50, -0.78, 0.15)),
           "C3": unit((0.15, -0.62, -0.77)), "C4": unit((0.62, 0.45, 0.64))}
MOUTHS = {"M1": unit((-0.85, -0.45, -0.25)), "M2": unit((0.55, 0.80, -0.15)), "M3": unit((0.75, -0.35, -0.55))}
JUNCTION = unit((-0.70, -0.65, -0.05))


def slerp(a, b, t):
    angle = math.acos(max(-1.0, min(1.0, float(np.dot(a, b)))))
    if angle < 1e-6:
        return a
    return (math.sin((1 - t) * angle) * a + math.sin(t * angle) * b) / math.sin(angle)


PLUGS = {"P1": slerp(CRATERS["C2"], CRATERS["C1"], 0.5), "P2": slerp(CRATERS["C1"], CRATERS["C4"], 0.35)}
# T1 runs from the near side to the far side: M1, junction, under C2, plug P1, under
# C1 (section B: the prize, gated by P1, P2 and C1's membrane), P2, under C4, M2.
TUNNELS = {
    "T1": ["M1", "J", "C2", "P1", "C1", "P2", "C4", "M2"],
    "T2": ["M3", "C3", "J"],
}
FLATS = {"nav": unit((0.03, -1.0, 0.2)), "shotgun": unit((0.75, -0.6, 0.1))}
SPOTS = {
    # the surface things build_ship.py places (directions); flats included above
    "probe_body": unit((0.0, -1.0, 0.2)), "probe_wing": unit((0.15, -1.0, 0.25)), "nav": FLATS["nav"],
    "shotgun": FLATS["shotgun"], "eye": unit((-0.3, -0.9, -0.2)),
    "tentacle0": unit((0.5, -0.6, -0.5)), "tentacle1": unit((-0.6, -0.5, 0.4)),
    "tentacle2": unit((0.7, 0.2, 0.6)), "tentacle3": unit((-0.4, 0.3, -0.8)),
    "glow0": unit((0.0, -1.0, 0.25)), "glow1": unit((0.5, -0.8, -0.3)), "glow2": unit((-0.6, -0.6, 0.5)),
    "glow3": unit((-0.2, -0.95, -0.4)), "glow4": unit((0.6, -0.7, 0.4)), "top": unit((0.0, 0.0, 1.0)),
}
POINTS = dict(CRATERS, **MOUTHS, **PLUGS, J=JUNCTION)


# --- noise --------------------------------------------------------------------------------------
_rng = np.random.default_rng(SEED)
_PERM = np.concatenate([_rng.permutation(256)] * 2)
_GRAD = np.array([[1, 1, 0], [-1, 1, 0], [1, -1, 0], [-1, -1, 0], [1, 0, 1], [-1, 0, 1], [1, 0, -1], [-1, 0, -1],
                  [0, 1, 1], [0, -1, 1], [0, 1, -1], [0, -1, -1]], dtype=np.float64)


def perlin(p):
    """Gradient noise of points (N, 3), about -1..1."""
    i = np.floor(p).astype(np.int64)
    f = p - i
    i &= 255
    u = f * f * f * (f * (f * 6 - 15) + 10)
    out = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                h = _PERM[_PERM[_PERM[i[:, 0] + dx] + i[:, 1] + dy] + i[:, 2] + dz] % 12
                g = _GRAD[h]
                dot = g[:, 0] * (f[:, 0] - dx) + g[:, 1] * (f[:, 1] - dy) + g[:, 2] * (f[:, 2] - dz)
                w = (u[:, 0] if dx else 1 - u[:, 0]) * (u[:, 1] if dy else 1 - u[:, 1]) * (u[:, 2] if dz else 1 - u[:, 2])
                out = out + w * dot
    return out


def fbm(p, octaves, gain=0.5):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total = total + amp * perlin(p * (2.0 ** o) + o * 17.3)
        norm += amp
        amp *= gain
    return total / norm


def raw_relief(d):
    """Unscaled relief of unit directions (N, 3)."""
    # Folds: a rounded ridge along the zero lines of a low noise (a gaussian of it).
    folds = np.exp(-(fbm(d * 1.4 + 3.1, 2) / 0.4) ** 2)
    bumps = fbm(d * 2.2 + 9.7, 3, gain=0.4)
    grain = perlin(d * 14.0 + 1.3)
    return 1.6 * folds + 1.2 * bumps + 0.06 * grain


# Scale the relief into RELIEF from a sample of directions.
_sample = _rng.normal(size=(200000, 3))
_sample /= np.linalg.norm(_sample, axis=1, keepdims=True)
_raw = raw_relief(_sample)
_lo, _hi = np.percentile(_raw, 0.2), np.percentile(_raw, 99.8)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def relief(d):
    r = RELIEF[0] + (raw_relief(d) - _lo) / (_hi - _lo) * (RELIEF[1] - RELIEF[0])
    return np.clip(r, RELIEF[0] - 1.0, RELIEF[1] + 1.0)


_CRATER_BASE = {k: float(relief(v[None])[0]) for k, v in CRATERS.items()}
_FLAT_BASE = {k: float(relief(v[None])[0]) for k, v in FLATS.items()}


def height(d):
    """Surface height over RADIUS of unit directions (N, 3), m."""
    h = relief(d)
    for name, c in CRATERS.items():
        t = np.arccos(np.clip(d @ c, -1, 1)) * RADIUS
        near = t < CRATER_R + 6.0
        if not near.any():
            continue
        tn = t[near]
        bowl = -CRATER_DEPTH * (1.0 - smoothstep(CRATER_FLAT, CRATER_R, tn)) + CRATER_RIM * np.exp(-((tn - CRATER_R) / 1.6) ** 2)
        w = 1.0 - smoothstep(CRATER_R, CRATER_R + 6.0, tn)
        h[near] = (1 - w) * h[near] + w * (_CRATER_BASE[name] + bowl)
    for name, c in FLATS.items():
        t = np.arccos(np.clip(d @ c, -1, 1)) * RADIUS
        w = 1.0 - smoothstep(5.0, 11.0, t)
        h = (1 - w) * h + w * _FLAT_BASE[name]
    return h


def surface(d):
    d = unit(d)
    return d * (RADIUS + float(height(d[None])[0]))


# --- tunnels: sampled paths -----------------------------------------------------------------------
def tunnel_samples(names, step=1.0):
    """Points (N, 3), radius (N), floor fraction (N) along a tunnel; mouths ramp to the surface."""
    dirs = [POINTS[n] for n in names]
    pts = []
    for a, b in zip(dirs[:-1], dirs[1:]):
        angle = math.acos(float(np.clip(np.dot(a, b), -1, 1)))
        n = max(2, int(angle * TUNNEL_DEPTH / step))
        side = unit(np.cross(a, b))
        for k in range(n):
            t = k / n
            d = slerp(a, b, t)
            # A gentle sideways S, zero at every waypoint (craters and plugs sit right over/at the axis).
            wiggle = 3.0 * math.sin(math.pi * t) * math.sin(2 * math.pi * t * max(1, round(angle * TUNNEL_DEPTH / 40)))
            pts.append(unit(d + side * wiggle / TUNNEL_DEPTH))
    pts.append(dirs[-1])
    pts = np.array(pts)
    # Arc length along the path (at the tunnel depth).
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1) * TUNNEL_DEPTH
    s = np.concatenate([[0.0], np.cumsum(seg)])
    radius_axis = np.full(len(pts), TUNNEL_DEPTH)
    for end, at in ((names[0], 0.0), (names[-1], s[-1])):
        if end in MOUTHS:
            top = RADIUS + float(height(MOUTHS[end][None])[0]) + 3.0
            ramp = np.clip(1.0 - np.abs(s - at) / RAMP_LEN, 0.0, 1.0)
            ramp = ramp * ramp * (3 - 2 * ramp) * 0.25 + ramp * 0.75   # nearly linear, eased at the foot
            radius_axis = np.maximum(radius_axis, TUNNEL_DEPTH + (top - TUNNEL_DEPTH) * ramp)
    r = np.full(len(pts), TUNNEL_R)
    for k, name in enumerate(names):
        if name in PLUGS:
            at = s[np.argmin(np.linalg.norm(pts - POINTS[name], axis=1))]
            w = 1.0 - smoothstep(0.6, THROAT_LEN + 1.5, np.abs(s - at))
            r = r + (THROAT_R - r) * w
    # The floor stays at one depth under the axis, throats included: a throat is
    # round down to its own bottom, 7 cm over the floor. (Blending the floor away
    # toward a throat left a 1 m dip before it that the pawn could not climb out of.)
    floor = FLOOR * TUNNEL_R / r
    return pts * radius_axis[:, None], r, floor, s


# --- the field -----------------------------------------------------------------------------------
def build_field():
    t0 = time.time()
    half = RADIUS + RELIEF[1] + 4.0
    n = int(math.ceil(2 * half / VOXEL)) + 3
    origin = -VOXEL * (n - 1) / 2.0
    axis = origin + VOXEL * np.arange(n)
    F = np.empty((n, n, n), dtype=np.float32)
    shell_in, shell_out = RADIUS + RELIEF[0] - 2.5, RADIUS + RELIEF[1] + 2.5
    yy, zz = np.meshgrid(axis, axis, indexing="ij")
    for i in range(n):
        x = axis[i]
        r = np.sqrt(x * x + yy * yy + zz * zz)
        f = np.where(r < shell_in, -1.0, 1.0).astype(np.float32)
        band = (r >= shell_in) & (r <= shell_out)
        if band.any():
            p = np.stack([np.full(band.sum(), x), yy[band], zz[band]], axis=1)
            d = p / r[band][:, None]
            f[band] = (r[band] - (RADIUS + height(d))).astype(np.float32)
        F[i] = f
    print("JAMPLANET: base field %d^3 in %.1f s" % (n, time.time() - t0))

    def carve(a, b, ra, rb, fa, fb):
        """Carve the inside of a tapered capsule from a to b with a floor plane (fraction f of the radius under the axis)."""
        reach = max(ra, rb) + VOXEL
        lo = np.floor((np.minimum(a, b) - reach - origin) / VOXEL).astype(int).clip(0, n - 1)
        hi = np.ceil((np.maximum(a, b) + reach - origin) / VOXEL).astype(int).clip(0, n - 1) + 1
        gx, gy, gz = np.meshgrid(axis[lo[0]:hi[0]], axis[lo[1]:hi[1]], axis[lo[2]:hi[2]], indexing="ij")
        p = np.stack([gx, gy, gz], axis=-1)
        ab = b - a
        t = np.clip(((p - a) @ ab) / max(float(ab @ ab), 1e-9), 0.0, 1.0)
        c = a + t[..., None] * ab
        rad = ra + (rb - ra) * t
        fl = fa + (fb - fa) * t
        cap = np.linalg.norm(p - c, axis=-1) - rad
        # The floor plane contains the axis (it tilts with a ramp; a level floor per
        # segment made a ramp into 0.5 m stairs): its normal is the radial up minus its
        # part along the axis.
        up = c / np.linalg.norm(c, axis=-1, keepdims=True)
        along = ab / max(float(np.linalg.norm(ab)), 1e-9)
        up = up - (up @ along)[..., None] * along
        up /= np.linalg.norm(up, axis=-1, keepdims=True)
        above_floor = ((p - c) * up).sum(-1) + fl * rad
        inside = np.maximum(cap, -above_floor)
        box = F[lo[0]:hi[0], lo[1]:hi[1], lo[2]:hi[2]]
        np.maximum(box, (-inside).astype(np.float32), out=box)

    paths = {}
    for name, names in TUNNELS.items():
        pts, r, fl, s = tunnel_samples(names)
        paths[name] = (pts, r, fl, s)
        for k in range(len(pts) - 1):
            carve(pts[k], pts[k + 1], r[k], r[k + 1], fl[k], fl[k + 1])
    for name, c in CRATERS.items():
        bottom = c * (RADIUS + _CRATER_BASE[name] - CRATER_DEPTH)
        carve(bottom + c * 1.5, c * TUNNEL_DEPTH, SHAFT_R, SHAFT_R, 2.0, 2.0)
    print("JAMPLANET: carved in %.1f s" % (time.time() - t0))
    # Check: every tunnel axis point (lifted to 1 m over its floor) must be air.
    for name, (pts, r, fl, s) in paths.items():
        lift = pts - pts / np.linalg.norm(pts, axis=1, keepdims=True) * (fl.clip(max=1.0) * r - 1.0)[:, None]
        ijk = np.rint((lift - origin) / VOXEL).astype(int).clip(0, n - 1)
        solid = F[ijk[:, 0], ijk[:, 1], ijk[:, 2]] < 0
        print("JAMPLANET: tunnel %s: %d samples, %.0f m long, %d of them 1 m over the floor in solid" % (name, len(pts), s[-1], solid.sum()))
    return F, origin, paths


def surface_nets(F, origin):
    t0 = time.time()
    n = F.shape[0]
    inside = F < 0
    cell_sum = {}
    edges = []
    for axis in range(3):
        a = [slice(None)] * 3
        b = [slice(None)] * 3
        a[axis] = slice(0, n - 1)
        b[axis] = slice(1, n)
        cross = inside[tuple(a)] != inside[tuple(b)]
        idx = np.argwhere(cross)
        f0 = F[tuple(a)][cross]
        f1 = F[tuple(b)][cross]
        t = f0 / (f0 - f1)
        pos = origin + VOXEL * idx.astype(np.float64)
        pos[:, axis] += VOXEL * t
        edges.append((axis, idx, pos, f0 < 0))
    # Cells around every crossing edge collect its crossing point.
    keys, points = [], []
    quads_cells = []
    for axis, idx, pos, from_inside in edges:
        o1, o2 = [(1, 2), (0, 2), (0, 1)][axis]
        ring = []
        for d1, d2 in ((0, 0), (1, 0), (1, 1), (0, 1)):
            c = idx.copy()
            c[:, o1] -= d1
            c[:, o2] -= d2
            ring.append(c)
            keys.append((c[:, 0] * n + c[:, 1]) * n + c[:, 2])
            points.append(pos)
        quads_cells.append((axis, ring, from_inside))
    keys = np.concatenate(keys)
    points = np.concatenate(points)
    cells, inverse = np.unique(keys, return_inverse=True)
    count = np.bincount(inverse, minlength=len(cells)).astype(np.float64)
    verts = np.stack([np.bincount(inverse, weights=points[:, k], minlength=len(cells)) for k in range(3)], axis=1) / count[:, None]
    quads = []
    for axis, ring, from_inside in quads_cells:
        q = np.stack([np.searchsorted(cells, (c[:, 0] * n + c[:, 1]) * n + c[:, 2]) for c in ring], axis=1)
        # Outward = from the inside sample to the outside one along this axis.
        normal = np.cross(verts[q[:, 2]] - verts[q[:, 0]], verts[q[:, 3]] - verts[q[:, 1]])
        want = np.where(from_inside, 1.0, -1.0)
        flip = normal[:, axis] * want < 0
        q[flip] = q[flip][:, ::-1]
        quads.append(q)
    quads = np.concatenate(quads)
    print("JAMPLANET: surface nets %d verts, %d quads in %.1f s" % (len(verts), len(quads), time.time() - t0))
    return verts, quads


# --- Blender helpers --------------------------------------------------------------------------------
def make_mesh(name, verts, faces, uvs=None, smooth=True):
    """faces: (N, k) int array, all k-gons. verts are in UE axes: the FBX round trip
    turns Blender's +Y into UE's -Y, so Y is mirrored here (and the winding reversed)."""
    verts = np.asarray(verts, dtype=np.float64) * np.array([1.0, -1.0, 1.0])
    faces = np.asarray(faces)[:, ::-1]
    mesh = bpy.data.meshes.new(name)
    mesh.vertices.add(len(verts))
    mesh.vertices.foreach_set("co", np.asarray(verts, dtype=np.float32).ravel())
    k = faces.shape[1]
    mesh.loops.add(len(faces) * k)
    mesh.loops.foreach_set("vertex_index", faces.astype(np.int32).ravel())
    mesh.polygons.add(len(faces))
    mesh.polygons.foreach_set("loop_start", (np.arange(len(faces)) * k).astype(np.int32))
    mesh.polygons.foreach_set("loop_total", np.full(len(faces), k, dtype=np.int32))
    if uvs is not None:
        layer = mesh.uv_layers.new(name="UVMap")
        layer.data.foreach_set("uv", np.asarray(uvs, dtype=np.float32)[faces.ravel()].ravel())
    mesh.update(calc_edges=True)
    mesh.validate()
    mesh.polygons.foreach_set("use_smooth", np.full(len(mesh.polygons), smooth))
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def apply_decimate(obj, ratio):
    bpy.context.view_layer.objects.active = obj
    mod = obj.modifiers.new("Decimate", "DECIMATE")
    mod.ratio = ratio
    mod.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=mod.name)


def export(obj, path):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, mesh_smooth_type="FACE", use_triangles=True,
                             add_leaf_bones=False, bake_anim=False, axis_forward="-Z", axis_up="Y")
    print("JAMPLANET: wrote %s" % path)


def tube(path, radii, sides, name, v_scale=1.0):
    """A closed-ended tube along path (N, 3) with radius per point; UV: u around, v along (length / circumference)."""
    path = np.asarray(path, dtype=np.float64)
    tang = np.gradient(path, axis=0)
    tang /= np.linalg.norm(tang, axis=1, keepdims=True)
    ref = np.array([0.0, 0.0, 1.0])
    normal = np.cross(tang[0], ref)
    if np.linalg.norm(normal) < 1e-3:
        normal = np.cross(tang[0], [1.0, 0.0, 0.0])
    normal /= np.linalg.norm(normal)
    verts, uvs = [], []
    length = 0.0
    mean_c = 2 * math.pi * float(np.mean(radii))
    for k, p in enumerate(path):
        if k:
            length += float(np.linalg.norm(p - path[k - 1]))
            # Parallel transport of the ring frame.
            normal = normal - tang[k] * np.dot(normal, tang[k])
            normal /= np.linalg.norm(normal)
        bin_ = np.cross(tang[k], normal)
        for j in range(sides + 1):
            a = 2 * math.pi * j / sides
            verts.append(p + radii[k] * (math.cos(a) * normal + math.sin(a) * bin_))
            uvs.append((j / sides, length / mean_c * v_scale))
    rows, cols = len(path), sides + 1
    faces = []
    for k in range(rows - 1):
        for j in range(sides):
            a = k * cols + j
            faces.append((a, a + 1, a + cols + 1, a + cols))
    obj = make_mesh(name, np.array(verts), np.array(faces), np.array(uvs))
    return obj


# --- the planet ------------------------------------------------------------------------------------
def build_planet():
    F, origin, paths = build_field()
    verts, quads = surface_nets(F, origin)
    del F
    d = verts / np.linalg.norm(verts, axis=1, keepdims=True)
    uv = np.stack([np.arctan2(d[:, 1], d[:, 0]) / (2 * math.pi) + 0.5, np.arccos(np.clip(d[:, 2], -1, 1)) / math.pi], axis=1)
    obj = make_mesh("SM_Jam_Planet", verts, quads, uv)
    tris = len(quads) * 2
    if tris > TARGET_TRIS:
        apply_decimate(obj, TARGET_TRIS / tris)
    print("JAMPLANET: planet %d faces after decimation" % len(obj.data.polygons))
    return obj, paths


def slope_report(obj):
    """Share of the outer surface area steeper than 35, 40, 45 degrees against the radial up."""
    mesh = obj.data
    n = len(mesh.polygons)
    normals = np.empty(n * 3)
    centres = np.empty(n * 3)
    areas = np.empty(n)
    mesh.polygons.foreach_get("normal", normals)
    mesh.polygons.foreach_get("center", centres)
    mesh.polygons.foreach_get("area", areas)
    normals = normals.reshape(-1, 3)
    centres = centres.reshape(-1, 3)
    r = np.linalg.norm(centres, axis=1)
    up = centres / r[:, None]
    outer = r > RADIUS + RELIEF[0] - 1.5
    slope = np.degrees(np.arccos(np.clip((normals * up).sum(1), -1, 1)))
    total = areas[outer].sum()
    shares = {a: float(areas[outer & (slope > a)].sum() / total) for a in (35, 40, 45, 50)}
    return shares, float(r[outer].max()), float(r[outer].min())


def lap_report():
    """The jam.Ship.Lap route: the great circle through the top heading +X; slope along it and distance to holes."""
    th = np.linspace(0, 2 * math.pi, 2000, endpoint=False)
    d = np.stack([np.sin(th), np.zeros_like(th), np.cos(th)], axis=1)
    h = height(d)
    ds = 2 * math.pi * RADIUS / len(th)
    grade = np.degrees(np.arctan(np.abs(np.diff(np.concatenate([h, h[:1]]))) / ds))
    holes = list(CRATERS.values()) + list(MOUTHS.values())
    clear = min(float(np.min(np.arccos(np.clip(d @ c, -1, 1)))) * RADIUS for c in holes)
    return float(grade.max()), float(np.percentile(grade, 95)), clear


# --- the eye and a tentacle --------------------------------------------------------------------------
def build_eye():
    """A 4 m eyeball looking along +X; the iris texture projected from the front (planar on Y, Z)."""
    rings, sides, r = 32, 48, 2.0
    verts, uvs = [], []
    for i in range(rings + 1):
        a = math.pi * i / rings
        for j in range(sides + 1):
            b = 2 * math.pi * j / sides
            p = (r * math.cos(a), r * math.sin(a) * math.cos(b), r * math.sin(a) * math.sin(b))
            verts.append(p)
            uvs.append((0.5 + p[1] / (2 * r), 0.5 - p[2] / (2 * r)))
    faces = []
    for i in range(rings):
        for j in range(sides):
            a0 = i * (sides + 1) + j
            faces.append((a0, a0 + sides + 1, a0 + sides + 2, a0 + 1))
    return make_mesh("SM_Jam_PlanetEye", np.array(verts), np.array(faces), np.array(uvs))


def build_tentacle():
    """An arch out of the ground: local X along it, Z up; both ends 1 m under the ground plane, 10 m apart, 4 m high, a curl."""
    t = np.linspace(0, 1, 140)
    x = 10.0 * t
    z = 5.0 * np.sin(math.pi * t) - 1.0
    y = 0.8 * np.sin(2 * math.pi * t)
    path = np.stack([x, y, z], axis=1)
    radii = 0.55 * (1.0 - 0.75 * t) + 0.05
    return tube(path, radii, 24, "SM_Jam_Tentacle", v_scale=1.0)


# --- previews ----------------------------------------------------------------------------------------
def preview(obj, path, location, target, lens=30):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "SINGLE"
    scene.render.resolution_x, scene.render.resolution_y = 1200, 800
    cam = bpy.data.objects.get("PreviewCam")
    if cam is None:
        cam = bpy.data.objects.new("PreviewCam", bpy.data.cameras.new("PreviewCam"))
        scene.collection.objects.link(cam)
    cam.data.lens = lens
    cam.data.clip_end = 5000
    cam.location = location
    direction = np.asarray(target) - np.asarray(location)
    import mathutils
    cam.rotation_euler = mathutils.Vector(direction.tolist()).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    planet, paths = build_planet()
    shares, rmax, rmin = slope_report(planet)
    print("JAMPLANET: outer surface radius %.2f..%.2f m; area steeper than 35/40/45/50 deg: %s" %
          (rmin, rmax, ", ".join("%.1f%%" % (100 * shares[a]) for a in (35, 40, 45, 50))))
    grade_max, grade95, lap_clear = lap_report()
    print("JAMPLANET: lap circle: grade max %.1f deg, 95th %.1f deg, nearest crater or mouth %.1f m" % (grade_max, grade95, lap_clear))
    export(planet, os.path.join(OUT, "SM_Jam_Planet.fbx"))
    # Blender's +Y is UE's -Y (the side facing the ship).
    preview(planet, os.path.join(OUT, "planet_near.png"), (0, 220, 60), (0, 0, 0))
    preview(planet, os.path.join(OUT, "planet_far.png"), (160, 120, 150), (0, 0, 0))
    preview(planet, os.path.join(OUT, "planet_top.png"), (0, 40, 200), (0, 0, 0))
    # Mouths and a look down a tunnel (Blender axes: Y mirrored).
    mirror = np.array([1.0, -1.0, 1.0])
    for name in ("M1", "M3"):
        at = surface(MOUTHS[name])
        preview(planet, os.path.join(OUT, "mouth_%s.png" % name), tuple(at * 1.35 * mirror), tuple(at * mirror), lens=24)
    pts = paths["T1"][0]
    k = int(np.argmin(np.linalg.norm(pts / np.linalg.norm(pts, axis=1, keepdims=True) - PLUGS["P1"], axis=1)))
    eye = pts[k - 12] - unit(pts[k - 12]) * 0.4
    preview(planet, os.path.join(OUT, "tunnel_to_P1.png"), tuple(eye * mirror), tuple(pts[k] * mirror), lens=16)
    for name, obj in (("SM_Jam_PlanetEye", build_eye()), ("SM_Jam_Tentacle", build_tentacle())):
        export(obj, os.path.join(OUT, name + ".fbx"))

    cm = lambda v: [round(float(c) * 100.0, 1) for c in v]
    layout = {"radius_cm": RADIUS * 100, "surface_min_cm": round(rmin * 100), "surface_max_cm": round(rmax * 100),
              "spots": {}, "craters": [], "plugs": [], "prizes": [], "mouths": {}}
    for name, d in SPOTS.items():
        layout["spots"][name] = {"pos": cm(surface(d)), "up": [round(float(c), 4) for c in unit(d)]}
    for name, c in CRATERS.items():
        bottom = c * (RADIUS + _CRATER_BASE[name] - CRATER_DEPTH)
        layout["craters"].append({"name": name, "pos": cm(bottom), "up": [round(float(v), 4) for v in c],
                                  "tunnel_floor_cm": round((TUNNEL_DEPTH - FLOOR * TUNNEL_R) * 100)})
    for name, p in PLUGS.items():
        for tname, (pts, r, fl, s) in paths.items():
            k = int(np.argmin(np.linalg.norm(pts / np.linalg.norm(pts, axis=1, keepdims=True) - p, axis=1)))
            if np.linalg.norm(pts[k] / np.linalg.norm(pts[k]) - p) < 1e-3:
                fwd = unit(pts[min(k + 1, len(pts) - 1)] - pts[max(k - 1, 0)])
                layout["plugs"].append({"name": name, "tunnel": tname, "pos": cm(pts[k]), "forward": [round(float(v), 4) for v in fwd],
                                        "up": [round(float(v), 4) for v in unit(pts[k])], "throat_radius_cm": THROAT_R * 100})
    # The prize of section B: on T1's floor 4 m from C1's shaft, toward P2.
    pts, r, fl, s = paths["T1"]
    dirs = pts / np.linalg.norm(pts, axis=1, keepdims=True)
    k = int(np.argmin(np.linalg.norm(dirs - CRATERS["C1"], axis=1)))
    for name, ahead in (("cargo", 4.0), ("oxygen", 6.0)):
        j = int(np.argmin(np.abs(s - (s[k] + ahead))))
        up = unit(pts[j])
        layout["prizes"].append({"name": name, "pos": cm(pts[j] - up * (FLOOR * TUNNEL_R - 0.5)), "up": [round(float(v), 4) for v in up]})
    for name, d in MOUTHS.items():
        layout["mouths"][name] = {"pos": cm(surface(d)), "up": [round(float(v), 4) for v in d]}
    layout["lap"] = {"grade_max_deg": round(grade_max, 1), "grade_p95_deg": round(grade95, 1), "nearest_hole_m": round(lap_clear, 1)}
    layout["slope_share"] = {str(a): round(v, 4) for a, v in shares.items()}
    with open(LAYOUT, "w", encoding="utf-8", newline="\n") as f:
        json.dump(layout, f, indent=1)
    print("JAMPLANET: wrote %s" % LAYOUT)


main()
