"""The flyer: a low-poly cliff-racer-like pterosaur in parts (Blender, headless).

  blender.exe -b --python Scripts/blender/jam_flyer.py -- <out_dir>

Writes into out_dir (not the repository): SM_Jam_Flyer_Body.fbx, SM_Jam_Flyer_Wing.fbx,
SM_Jam_Flyer_Tail.fbx, SM_Jam_Flyer_TailTip.fbx, the colour atlas T_Jam_Flyer_BC.png
(256 px, painted here with numpy) and preview renders. Procedural, seeded, no
third-party model or texture.

A beaked head with an open mouth, small teeth and a swept-back crest, a long
S neck, a thin keeled body, dangling legs; broad membrane wings on an arm bone
(dark leathery top, reddish-brown underside with dark veins); a long whip tail in
two parts ending in a vertical bone blade. About 5 m nose to blade, 3.5 m span.

AJamFlyer (Source/MindKore/JamFlyer.cpp) flaps the wings and swings the tail by
turning the parts, so each part's origin is its joint. UE axes (X forward,
Y right, Z up), metres here; the joints (keep in step with JamFlyer.cpp):
  wing (the right one; the left is the same mesh mirrored) - shoulder at (0.18, 0.09, 0.07)
  tail - root at (-0.42, 0, 0.02); tail tip - at (-1.55, 0, 0) from the tail root.
One material slot; the atlas: wing underside (top half), skin (bottom left),
wing top (bottom right, upper), bone and the eyes (bottom right, lower).
"""
import math
import os
import sys

import bmesh
import bpy
import mathutils
import numpy as np

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = ARGS[0] if ARGS else os.path.join(os.environ.get("TEMP", "."), "jam_flyer")
TEX = 256
# Atlas rectangles in pixels (x0, y0, x1, y1), y up from the bottom as Blender's UVs.
WING_BOTTOM = (0, 128, 256, 256)
SKIN = (0, 0, 128, 128)
WING_TOP = (128, 64, 256, 128)
BONE = (128, 16, 256, 64)
EYE = (128, 0, 256, 16)
SHOULDER = (0.18, 0.09, 0.07)
TAIL_ROOT = (-0.42, 0.0, 0.02)
TAIL_SPLIT = 1.55
rng = np.random.default_rng(31)


class Parts:
    """Vertices (UE axes, metres), per-vertex UV0 and faces (vertex index lists)."""

    def __init__(self):
        self.verts, self.uvs, self.faces = [], [], []

    def add(self, verts, uvs, faces):
        base = len(self.verts)
        self.verts.extend(map(tuple, verts))
        self.uvs.extend(map(tuple, uvs))
        self.faces.extend([base + i for i in f] for f in faces)


def rect_uv(rect, u, v):
    """(u, v) in 0..1 into an atlas rectangle, half a texel in from its edges."""
    x0, y0, x1, y1 = rect
    return ((x0 + 0.5 + u * (x1 - x0 - 1)) / TEX, (y0 + 0.5 + v * (y1 - y0 - 1)) / TEX)


def spline(points, step=0.05):
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


def tube(parts, points, radii, sides, rect, flatten=1.0, step=0.05):
    """A capped tube through points; radii given per point and interpolated along the length.
    UV: u around (0 = the top, 0.5 = the belly), v along, both into rect. flatten squeezes it along Y."""
    pts = np.asarray(points, dtype=np.float64)
    path = spline(pts, step) if len(pts) > 2 else np.array([pts[0] + (pts[1] - pts[0]) * t for t in np.linspace(0, 1, 4)])
    knots = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(pts, axis=0), axis=1))])
    seg = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))])
    r = np.interp(seg / seg[-1] * knots[-1], knots, radii)
    t = np.gradient(path, axis=0)
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    up = np.array([0.0, 0.0, 1.0])
    verts, uvs, faces = [], [], []
    for k in range(len(path)):
        n = up - t[k] * np.dot(up, t[k])
        if np.linalg.norm(n) < 1e-3:
            n = np.array([1.0, 0.0, 0.0]) - t[k] * t[k][0]
        n /= np.linalg.norm(n)
        b = np.cross(t[k], n)
        for j in range(sides + 1):
            a = 2 * math.pi * j / sides
            off = r[k] * (math.cos(a) * n + math.sin(a) * b)
            off[1] *= flatten
            verts.append(path[k] + off)
            uvs.append(rect_uv(rect, j / sides, seg[k] / seg[-1]))
    cols = sides + 1
    for k in range(len(path) - 1):
        for j in range(sides):
            a = k * cols + j
            faces.append([a, a + 1, a + cols + 1, a + cols])
    for k in (0, len(path) - 1):
        centre = len(verts)
        verts.append(path[k])
        uvs.append(rect_uv(rect, 0.5, seg[k] / seg[-1]))
        for j in range(sides):
            faces.append([k * cols + j, k * cols + j + 1, centre])
    parts.add(verts, uvs, faces)


# --- the parts -------------------------------------------------------------------------------
def body():
    p = Parts()
    tube(p, [(-0.46, 0, 0.0), (-0.25, 0, 0.03), (0.05, 0, 0.05), (0.33, 0, 0.06)], [0.04, 0.11, 0.13, 0.075], 10, SKIN, flatten=0.8)
    tube(p, [(0.28, 0, 0.06), (0.55, 0, 0.13), (0.78, 0, 0.17), (0.94, 0, 0.16)], [0.065, 0.048, 0.045, 0.055], 8, SKIN)
    tube(p, [(0.88, 0, 0.15), (0.99, 0, 0.18), (1.1, 0, 0.165)], [0.055, 0.085, 0.07], 8, SKIN, flatten=0.85)
    # The beak, open: an upper jaw hooked down at the tip and a shorter lower one.
    tube(p, [(1.06, 0, 0.18), (1.25, 0, 0.155), (1.42, 0, 0.11), (1.52, 0, 0.05)], [0.055, 0.036, 0.016, 0.002], 6, BONE, flatten=0.75)
    tube(p, [(1.05, 0, 0.12), (1.22, 0, 0.075), (1.38, 0, 0.045)], [0.04, 0.022, 0.002], 6, BONE, flatten=0.8)
    for i in range(5):
        x = 1.12 + i * 0.065
        z = 0.155 - 0.11 * ((x - 1.06) / 0.46) ** 2
        for side in (-1, 1):
            tube(p, [(x, side * 0.022, z - 0.01), (x + 0.008, side * 0.02, z - 0.055)], [0.011, 0.0], 4, BONE)
    # A swept-back crest: one tall spike and two short ones behind it.
    for (x0, z0, x1, z1, r) in ((1.02, 0.22, 0.78, 0.42, 0.04), (0.93, 0.2, 0.72, 0.3, 0.025), (0.86, 0.19, 0.66, 0.24, 0.018)):
        tube(p, [(x0, 0, z0), ((x0 + x1) / 2, 0, (z0 + z1) / 2 + 0.015), (x1, 0, z1)], [r, r * 0.6, 0.0], 6, SKIN, flatten=0.35)
    for side in (-1, 1):
        tube(p, [(1.04, side * 0.065, 0.19), (1.07, side * 0.067, 0.19)], [0.022, 0.018], 6, EYE)
        # Legs folded back under the body, three claws each.
        leg = [(-0.2, side * 0.06, -0.06), (-0.4, side * 0.09, -0.15), (-0.62, side * 0.08, -0.14)]
        tube(p, leg, [0.03, 0.022, 0.014], 6, SKIN)
        for c in (-1, 0, 1):
            tube(p, [leg[-1], (-0.72, side * 0.08 + c * 0.03, -0.17)], [0.01, 0.0], 4, BONE)
    return p


def wing():
    """The right wing, shoulder at the origin: a membrane slab under an arm bone."""
    p = Parts()
    arm = [(0.0, 0.0, 0.0), (0.09, 0.55, 0.04), (-0.04, 1.15, 0.03), (-0.36, 1.66, 0.0)]
    tube(p, arm, [0.04, 0.028, 0.016, 0.004], 6, SKIN)
    lead = spline(arm, 0.02)
    lead_s = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(lead, axis=0), axis=1))])
    lead_s /= lead_s[-1]
    # Trailing edge from the wing tip back to the hip, scalloped between the "fingers".
    trail_pts = [(-0.36, 1.66, 0.0), (-0.5, 1.3, -0.01), (-0.44, 1.02, 0.0), (-0.6, 0.72, -0.01), (-0.55, 0.45, 0.0),
                 (-0.66, 0.22, -0.01), (-0.64, -0.04, -0.02)]
    trail = spline(trail_pts[::-1], 0.02)
    trail_s = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(trail, axis=0), axis=1))])
    trail_s /= trail_s[-1]
    stations, chord, thick = 22, 6, 0.008

    def at(curve, s_curve, s):
        return np.array([np.interp(s, s_curve, curve[:, i]) for i in range(3)])

    grid = []
    for i in range(stations + 1):
        s = i / stations
        a, b = at(lead, lead_s, s), at(trail, trail_s, s)
        row = []
        for j in range(chord + 1):
            c = j / chord
            q = a + (b - a) * c
            q[2] += 0.05 * math.sin(math.pi * c) * (1.0 - 0.6 * s)  # camber
            row.append(q)
        grid.append(row)
    xs = [q[0] for row in grid for q in row]
    ys = [q[1] for row in grid for q in row]
    x_min, x_max, y_min, y_max = min(xs), max(xs), min(ys), max(ys)

    def planar(q, rect):
        return rect_uv(rect, (q[1] - y_min) / (y_max - y_min), (q[0] - x_min) / (x_max - x_min))

    cols = chord + 1
    for rect, dz, flip in ((WING_TOP, 0.0, False), (WING_BOTTOM, -thick, True)):
        verts = [q + (0, 0, dz) for row in grid for q in row]
        uvs = [planar(q, rect) for row in grid for q in row]
        faces = []
        for i in range(stations):
            for j in range(chord):
                a = i * cols + j
                f = [a, a + 1, a + cols + 1, a + cols]
                faces.append(f[::-1] if flip else f)
        p.add(verts, uvs, faces)
    # Close the slab's rim (normals are made consistent afterwards).
    outline = [(i, 0) for i in range(stations + 1)] + [(stations, j) for j in range(1, chord + 1)] + \
              [(i, chord) for i in range(stations - 1, -1, -1)] + [(0, j) for j in range(chord - 1, 0, -1)]
    verts, uvs, faces = [], [], []
    for k, (i, j) in enumerate(outline):
        q = grid[i][j]
        verts += [q, q + (0, 0, -thick)]
        uvs += [planar(q, WING_TOP), planar(q, WING_TOP)]
    n = len(outline)
    for k in range(n):
        a, b = 2 * k, 2 * ((k + 1) % n)
        faces.append([a, b, b + 1, a + 1])
    p.add(verts, uvs, faces)
    return p


def tail():
    p = Parts()
    tube(p, [(0.02, 0, 0), (-0.5, 0, 0.01), (-1.0, 0, 0.0), (-TAIL_SPLIT - 0.03, 0, 0)], [0.065, 0.042, 0.03, 0.023], 7, SKIN)
    return p


def tail_tip():
    p = Parts()
    tube(p, [(0.02, 0, 0), (-0.6, 0, 0), (-1.18, 0, 0)], [0.023, 0.016, 0.011], 6, SKIN)
    # The blade: a vertical leaf of bone with two barbs.
    tube(p, [(-1.1, 0, 0), (-1.22, 0, 0.0), (-1.36, 0, 0.0), (-1.5, 0, 0.0), (-1.62, 0, 0.0)], [0.012, 0.075, 0.09, 0.05, 0.0], 8, BONE, flatten=0.1)
    for side in (-1, 1):
        tube(p, [(-1.2, 0, side * 0.05), (-1.12, 0, side * 0.13)], [0.012, 0.0], 4, BONE, flatten=0.4)
    return p


# --- texture ---------------------------------------------------------------------------------
def value_noise(h, w, cell, seed):
    g = np.random.default_rng(seed).random((h // cell + 2, w // cell + 2))
    y, x = np.mgrid[0:h, 0:w] / cell
    x0, y0 = x.astype(int), y.astype(int)
    fx, fy = x - x0, y - y0
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a = g[y0, x0] * (1 - fx) + g[y0, x0 + 1] * fx
    b = g[y0 + 1, x0] * (1 - fx) + g[y0 + 1, x0 + 1] * fx
    return a * (1 - fy) + b * fy


def veins(h, w, seed, origin, count):
    """Dark branching veins from origin (pixels), as a 0..1 mask: random walks that fork."""
    m = np.zeros((h, w))
    g = np.random.default_rng(seed)
    stack = [(origin[0], origin[1], g.uniform(-0.9, 0.9) - math.pi / 2 + i * (1.8 / count), 2.2) for i in range(count)]
    while stack:
        x, y, a, width = stack.pop()
        for _ in range(400):
            a += g.normal(0, 0.12)
            x, y = x + math.cos(a), y + math.sin(a)
            if not (0 <= x < w and 0 <= y < h) or width < 0.6:
                break
            r = max(width, 0.8)
            ix0, iy0 = max(int(x) - 4, 0), max(int(y) - 4, 0)
            yy, xx = np.ogrid[iy0:min(int(y) + 5, h), ix0:min(int(x) + 5, w)]
            d = np.hypot(xx - x, yy - y)
            stamp = np.clip(1.0 - d / r, 0, 1) if r > 1.0 else (d < 0.75).astype(float)
            window = m[iy0:iy0 + stamp.shape[0], ix0:ix0 + stamp.shape[1]]
            np.maximum(window, stamp, out=window)
            width *= 0.993
            if g.random() < 0.025 and width > 0.9:
                stack.append((x, y, a + g.choice((-1, 1)) * g.uniform(0.4, 0.8), width * 0.7))
    return m


def paint():
    img = np.zeros((TEX, TEX, 3))
    # Wing underside: reddish-brown leather, lighter where thin, dark veins from the shoulder (UV u=0: the root).
    x0, y0, x1, y1 = WING_BOTTOM
    h, w = y1 - y0, x1 - x0
    n = 0.6 * value_noise(h, w, 16, 1) + 0.4 * value_noise(h, w, 5, 2)
    base = np.array([0.42, 0.15, 0.08]) * (0.75 + 0.5 * n[..., None])
    thin = np.clip(value_noise(h, w, 24, 3) * 1.6 - 0.7, 0, 1)[..., None]
    base = base * (1 - thin) + np.array([0.62, 0.3, 0.16]) * thin
    v = veins(h, w, 4, (2.0, h * 0.8), 7)
    base = base * (1 - 0.85 * v[..., None]) + np.array([0.12, 0.03, 0.03]) * 0.85 * v[..., None]
    img[y0:y1, x0:x1] = base
    # Wing top: dark grey-brown leather, mottled, faint veins.
    x0, y0, x1, y1 = WING_TOP
    h, w = y1 - y0, x1 - x0
    n = 0.6 * value_noise(h, w, 8, 5) + 0.4 * value_noise(h, w, 3, 6)
    base = np.array([0.12, 0.095, 0.085]) * (0.7 + 0.6 * n[..., None])
    v = veins(h, w, 7, (1.0, h * 0.8), 5)
    img[y0:y1, x0:x1] = base * (1 - 0.35 * v[..., None])
    # Skin: dark on the back (u 0 and 1), tan-red on the belly (u 0.5), blotched.
    x0, y0, x1, y1 = SKIN
    h, w = y1 - y0, x1 - x0
    u = np.linspace(0, 1, w)[None, :, None]
    belly = np.sin(math.pi * u) ** 2
    n = 0.5 * value_noise(h, w, 10, 8) + 0.5 * value_noise(h, w, 3, 9)
    back, front = np.array([0.15, 0.11, 0.095]), np.array([0.5, 0.27, 0.17])
    spots = (value_noise(h, w, 6, 10) > 0.72)[..., None]
    skin = (back * (1 - belly) + front * belly) * (0.75 + 0.5 * n[..., None])
    img[y0:y1, x0:x1] = np.where(spots, skin * 0.6, skin)
    # Bone: a dirty ivory. Eyes: sickly yellow with a dark middle row.
    x0, y0, x1, y1 = BONE
    h, w = y1 - y0, x1 - x0
    img[y0:y1, x0:x1] = np.array([0.7, 0.62, 0.46]) * (0.7 + 0.45 * value_noise(h, w, 4, 11)[..., None])
    x0, y0, x1, y1 = EYE
    img[y0:y1, x0:x1] = np.array([0.85, 0.7, 0.12])
    img[y0 + 6:y0 + 10, x0:x1] = np.array([0.05, 0.03, 0.02])
    image = bpy.data.images.new("T_Jam_Flyer_BC", TEX, TEX, alpha=False)
    rgba = np.concatenate([np.clip(img, 0, 1), np.ones((TEX, TEX, 1))], axis=2)
    image.pixels.foreach_set(rgba.astype(np.float32).ravel())
    image.filepath_raw = os.path.join(OUT, "T_Jam_Flyer_BC.png")
    image.file_format = "PNG"
    image.save()
    print("JAMFLYER: wrote %s" % image.filepath_raw)
    return image


# --- Blender objects, export, previews ------------------------------------------------------------
def to_object(name, parts, material):
    """Y mirrored (the FBX round trip turns Blender +Y into UE -Y); normals made outward per closed piece."""
    bm = bmesh.new()
    uv0 = bm.loops.layers.uv.new("UV0")
    verts = [bm.verts.new((v[0], -v[1], v[2])) for v in parts.verts]
    for f in parts.faces:
        try:
            face = bm.faces.new([verts[i] for i in f])
        except ValueError:
            continue
        for loop, i in zip(face.loops, f):
            loop[uv0].uv = parts.uvs[i]
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    # A thin curved slab (the wing) can come out inside out: its top (the WING_TOP texels) must face up.
    top = [f for f in bm.faces if WING_TOP[1] / TEX < f.loops[0][uv0].uv[1] < WING_TOP[3] / TEX and f.loops[0][uv0].uv[0] > 0.5]
    if top and sum(f.normal.z * f.calc_area() for f in top) < 0.0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    for poly in mesh.polygons:
        poly.use_smooth = True
    mesh.materials.append(material)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    print("JAMFLYER: %s %d triangles" % (name, len(mesh.polygons)))
    return obj


def export(obj, path):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, mesh_smooth_type="FACE", use_triangles=True,
                             add_leaf_bones=False, bake_anim=False, axis_forward="-Z", axis_up="Y")
    print("JAMFLYER: wrote %s" % path)


def assemble(objs, flap_deg):
    """Place the parts as AJamFlyer does, Blender axes (Y mirrored): the previews."""
    body_o, wing_r, wing_l, tail_o, tip_o = objs
    def ue(v):
        return mathutils.Vector((v[0], -v[1], v[2]))
    wing_r.location = ue(SHOULDER)
    wing_l.location = ue((SHOULDER[0], -SHOULDER[1], SHOULDER[2]))
    wing_l.scale = (1, -1, 1)
    # UE +Y (right) is Blender -Y: the right wing's tip lifts with a negative turn about X here.
    wing_r.rotation_euler = (math.radians(-flap_deg), 0, 0)
    wing_l.rotation_euler = (math.radians(flap_deg), 0, 0)
    tail_o.location = ue(TAIL_ROOT)
    tip_o.location = ue((TAIL_ROOT[0] - TAIL_SPLIT, 0, TAIL_ROOT[2]))


def preview(path, location, target, lens=35):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "TEXTURE"
    scene.display.shading.show_backface_culling = True
    scene.render.resolution_x, scene.render.resolution_y = 1000, 700
    cam = bpy.data.objects.get("Cam") or bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    if cam.name not in scene.collection.objects:
        scene.collection.objects.link(cam)
    cam.data.lens = lens
    cam.location = location
    cam.rotation_euler = (mathutils.Vector(target) - mathutils.Vector(location)).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    image = paint()
    mat = bpy.data.materials.new("Flyer")
    mat.use_nodes = True
    node = mat.node_tree.nodes.new("ShaderNodeTexImage")
    node.image = image
    node.interpolation = "Closest"
    mat.node_tree.links.new(node.outputs["Color"], mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"])
    objs = [to_object("SM_Jam_Flyer_Body", body(), mat), to_object("SM_Jam_Flyer_Wing", wing(), mat)]
    for name, obj in (("SM_Jam_Flyer_Body", objs[0]), ("SM_Jam_Flyer_Wing", objs[1])):
        export(obj, os.path.join(OUT, name + ".fbx"))
    tail_o = to_object("SM_Jam_Flyer_Tail", tail(), mat)
    tip_o = to_object("SM_Jam_Flyer_TailTip", tail_tip(), mat)
    export(tail_o, os.path.join(OUT, "SM_Jam_Flyer_Tail.fbx"))
    export(tip_o, os.path.join(OUT, "SM_Jam_Flyer_TailTip.fbx"))
    wing_l = objs[1].copy()
    bpy.context.scene.collection.objects.link(wing_l)
    assemble([objs[0], objs[1], wing_l, tail_o, tip_o], 15)
    # Blender -Y is UE +Y: "side" looks from UE +Y (the right), "below" from under the belly.
    preview(os.path.join(OUT, "preview_side.png"), (-0.6, -6.5, 0.6), (-0.6, 0, 0.1))
    preview(os.path.join(OUT, "preview_top.png"), (-0.6, 0, 7.5), (-0.6, 0.001, 0))
    preview(os.path.join(OUT, "preview_below.png"), (1.5, 2.5, -3.0), (-0.3, 0, 0))
    preview(os.path.join(OUT, "preview_head.png"), (2.6, -1.4, 0.7), (1.0, 0, 0.15), lens=50)


main()
