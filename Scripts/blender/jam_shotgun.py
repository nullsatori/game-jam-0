"""The jam's shotgun: MindKore's pump gun from the hero's back (Blender, headless).

  blender.exe -b --factory-startup --python Scripts/blender/jam_shotgun.py -- <out_dir>

Writes SM_Jam_Shotgun.fbx into out_dir (not the repository); Scripts/jam_weapons.py
imports it. The shape is MindKore's own: the 92 cm Soviet pump gun that
gun_r17.py builds from primitives for the operator's back (concept 01,
"дворовый гранж"), the same sections in centimetres, here laid in gun space for
a first-person view: UE axes (in metres), X butt -> muzzle, Z up, origin on the
trigger. No UVs (plain colour materials). Material slots: 0 Walnut (stock,
pump, pump ribs), 1 BlackOxide (receiver, barrel, magazine, guard), 2 Rubber
(recoil pad).
"""
import math
import os
import sys

import bpy
import numpy as np

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = ARGS[0] if ARGS else os.path.join(os.environ.get("TEMP", "."), "jam_weapons")
ORIGIN = np.array([48.4, 0.0, -4.5])  # the trigger, cm


# --- MindKore's gun_r17.py geometry, unchanged (cm: X butt -> muzzle, Y across, Z up, bore axis Z=0) ---
def section(x, top, bot, hw, m=20, p=2.6):
    t = np.linspace(0, 2 * math.pi, m, endpoint=False)
    c, s = np.cos(t), np.sin(t)
    return np.c_[np.full(m, x), hw * np.sign(c) * np.abs(c) ** (2 / p), (top + bot) / 2 + (top - bot) / 2 * np.sign(s) * np.abs(s) ** (2 / p)]


def loft(secs, cap=True):
    m = len(secs[0]); V = np.vstack(secs); F = []
    for i in range(len(secs) - 1):
        F += [(i * m + j, i * m + (j + 1) % m, (i + 1) * m + (j + 1) % m, (i + 1) * m + j) for j in range(m)]
    if cap:
        F.append(tuple(range(m))[::-1]); F.append(tuple((len(secs) - 1) * m + j for j in range(m)))
    return V, F


def cyl(x0, x1, r, z=0.0, m=20):
    xs = np.linspace(x0, x1, max(2, int((x1 - x0) / 2) + 1))
    return loft([section(x, z + r, z - r, r, m, 2.0) for x in xs])


def tube(path, r, closed=False, ring=8):
    path = np.asarray(path, float); n = len(path); V, F = [], []
    for i in range(n):
        a = path[(i - 1) % n if closed else max(i - 1, 0)]; b = path[(i + 1) % n if closed else min(i + 1, n - 1)]
        t = (b - a) / np.linalg.norm(b - a)
        k = np.array([0, 1.0, 0]) if abs(t[1]) < 0.9 else np.array([1.0, 0, 0])
        e1 = np.cross(t, k); e1 /= np.linalg.norm(e1); e2 = np.cross(t, e1)
        V += [path[i] + r * (math.cos(q) * e1 + math.sin(q) * e2) for q in np.linspace(0, 2 * math.pi, ring, endpoint=False)]
    for i in range(n if closed else n - 1):
        i2 = (i + 1) % n
        F += [(i * ring + j, i2 * ring + j, i2 * ring + (j + 1) % ring, i * ring + (j + 1) % ring) for j in range(ring)]
    if not closed:
        F.append(tuple(range(ring))[::-1]); F.append(tuple((n - 1) * ring + j for j in range(ring)))
    return np.array(V), F


def ring_path(x, top, bot, hw, grow=0.12, m=24):
    return section(x, top + grow, bot - grow, hw + grow, m)


def merge(parts):
    V, F, off = [], [], 0
    for v, f in parts:
        V.append(v); F += [tuple(i + off for i in face) for face in f]; off += len(v)
    return np.vstack(V), F


STOCK = [(0.0, -1.0, -13.5, 2.1), (10, -0.7, -11.6, 2.05), (22, -0.2, -8.4, 1.9), (30, 0.2, -5.0, 1.6),
         (34, 0.5, -4.4, 1.5), (38.5, 1.0, -3.8, 1.45)]
PUMP = [(61, -0.1, -3.7, 1.75), (62.2, 0.2, -4.0, 2.0), (76.8, 0.2, -4.0, 2.0), (78, -0.1, -3.7, 1.75)]
PARTS = {
    "Walnut": [loft([section(*s) for s in STOCK]), loft([section(*s) for s in PUMP])]
              + [tube(ring_path(x, 0.2, -4.0, 2.0), 0.16, True) for x in np.arange(63.5, 76, 2.4)],
    "BlackOxide": [loft([section(38.5, 1.25, -4.0, 1.45), section(58.0, 1.25, -4.0, 1.45)]),
                   cyl(58.0, 92.0, 1.05),
                   cyl(58.0, 83.0, 1.1, z=-2.3),
                   loft([section(83.0, 1.2, -3.6, 1.35), section(85.6, 1.2, -3.6, 1.35)]),
                   tube([[43, 0, -4.0], [44, 0, -6.1], [48, 0, -6.8], [52, 0, -6.0], [53.6, 0, -4.0]], 0.35),
                   tube([[48.6, 0, -4.0], [48.4, 0, -5.0], [47.8, 0, -5.6]], 0.25),
                   tube([[8 + 0.9 * math.cos(q), 0, -12.6 + 0.9 * math.sin(q)] for q in np.linspace(0, 2 * math.pi, 12, endpoint=False)], 0.18, True),
                   tube([[84.3 + 0.9 * math.cos(q), 0, -4.3 + 0.9 * math.sin(q)] for q in np.linspace(0, 2 * math.pi, 12, endpoint=False)], 0.18, True),
                   cyl(91.3, 91.7, 0.2, z=1.15, m=8)],
    "Rubber": [loft([section(-1.6, -1.0, -13.5, 2.1), section(0.0, -1.0, -13.5, 2.1)])],
}

bpy.ops.wm.read_factory_settings(use_empty=True)
me = bpy.data.meshes.new("SM_Jam_Shotgun")
verts, faces, mats = [], [], []
for slot, (name, group) in enumerate(PARTS.items()):
    V, F = merge(group)
    off = len(verts)
    verts += [tuple((v - ORIGIN) * 0.01) for v in V]
    faces += [tuple(i + off for i in f) for f in F]
    mats += [slot] * len(F)
    me.materials.append(bpy.data.materials.new(name))
me.from_pydata(verts, [], faces)
me.polygons.foreach_set("material_index", mats)
me.update()
me.shade_smooth()
me.set_sharp_from_angle(angle=math.radians(40.0))
ob = bpy.data.objects.new("SM_Jam_Shotgun", me)
bpy.context.scene.collection.objects.link(ob)
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
os.makedirs(OUT, exist_ok=True)
path = os.path.join(OUT, "SM_Jam_Shotgun.fbx")
bpy.ops.export_scene.fbx(filepath=path, use_selection=True, mesh_smooth_type="FACE", use_triangles=True,
                         add_leaf_bones=False, bake_anim=False, axis_forward="-Z", axis_up="Y")
lo, hi = np.min(verts, 0), np.max(verts, 0)
print("JAMGUN: %s, %d verts, %d faces, bounds %s .. %s m" % (path, len(verts), len(faces), lo.round(3), hi.round(3)))
