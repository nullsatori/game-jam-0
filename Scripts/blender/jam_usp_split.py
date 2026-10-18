"""MindKore's USP split into a frame and a moving slide (Blender, headless).

  blender.exe -b <MindKore>/Models/USP_Soviet_20260924/source/USP_Soviet_Game.blend
      --python Scripts/blender/jam_usp_split.py -- <MindKore>/Models/low-poly-usp/UPS-S.blend <out_dir>

USP_Soviet_Game.blend is the baked game mesh MindKore exported as
SM_MK_USP_Soviet (one object, cm, UVs on the 2048 maps); UPS-S.blend is its
source, which still has USP_Frame and USP_Slide as separate objects. Neither
is in this repository (MindKore keeps them under Models/, outside Git, and
packs them in Docs/Art/USP_20260924/USP_Soviet_Model_v01.zip).

Every loose part of the game mesh goes to the slide when it lies on the
source slide's surface (closer to it than to the frame's) or sits on top of
the slide (the front sight, Z >= 5.3 cm); the rest is the frame (with the
barrel, which stays put while the slide runs back and bares it). The
hammer (the loose part behind the slide, HAMMER_BOX) is its own part, its
origin moved to its pin (HAMMER_PIN) so it can turn there. Writes
SM_Jam_USP_Frame.fbx, SM_Jam_USP_Slide.fbx and SM_Jam_USP_Hammer.fbx into
out_dir with MindKore's own export settings, so all land in SM_MK_USP_Soviet's
frame (muzzle -X, grip at the origin) and keep its UVs and material.

SM_Jam_USP_Mag.fbx is new: a plain magazine (no UVs; slots BlackOxide body,
Rubber floorplate) laid along the grip's well (MAG_TOP, MAG_AXIS, measured
from slices of the grip), in the same frame. Blender Y here is UE -Y.
"""
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

ARGS = sys.argv[sys.argv.index("--") + 1:]
SOURCE, OUT = ARGS[0], ARGS[1]
TOP_OF_SLIDE_CM = 5.3
HAMMER_BOX = ((3.1, 1.0, 2.3), (4.8, 1.9, 5.1))  # cm, Blender
HAMMER_PIN = Vector((3.5, 1.45, 2.75))
# The grip's centre runs from x 1.6 at z -6 to x 2.4 at z -8.8 (slices of SM_MK_USP_Soviet).
MAG_AXIS = Vector((0.8, 0.0, -2.8)).normalized()
MAG_TOP = Vector((-0.26, 1.43, 0.5))
MAG_LENGTH, MAG_DEPTH, MAG_WIDTH = 9.0, 2.9, 2.0

with bpy.data.libraries.load(SOURCE) as (src, dst):
    dst.objects = ["USP_Frame", "USP_Slide"]
trees = {}
for o in dst.objects:
    bm = bmesh.new(); bm.from_mesh(o.data); bm.transform(o.matrix_world)
    trees[o.name] = BVHTree.FromBMesh(bm)  # metres

game = bpy.data.objects["SM_MK_USP_Soviet"]
bm = bmesh.new(); bm.from_mesh(game.data); bm.verts.ensure_lookup_table()
slide_verts, hammer_verts, seen = set(), set(), set()
counts = {"slide": 0, "frame": 0, "hammer": 0}
for v in bm.verts:
    if v.index in seen:
        continue
    part, stack = [], [v]
    seen.add(v.index)
    while stack:
        a = stack.pop(); part.append(a)
        for e in a.link_edges:
            b = e.other_vert(a)
            if b.index not in seen:
                seen.add(b.index); stack.append(b)
    def median(tree):
        d = sorted(tree.find_nearest(a.co * 0.01)[3] for a in part)
        return d[len(d) // 2]
    if all(HAMMER_BOX[0][i] <= min(a.co[i] for a in part) and max(a.co[i] for a in part) <= HAMMER_BOX[1][i] for i in range(3)):
        counts["hammer"] += 1
        hammer_verts.update(a.index for a in part)
        continue
    on_slide = median(trees["USP_Slide"]) < median(trees["USP_Frame"]) or min(a.co.z for a in part) >= TOP_OF_SLIDE_CM
    counts["slide" if on_slide else "frame"] += 1
    if on_slide:
        slide_verts.update(a.index for a in part)
print("JAMUSP: loose parts", counts, "slide verts", len(slide_verts), "hammer verts", len(hammer_verts), "of", len(bm.verts))


def magazine():
    """A box along the well with a wider floorplate, both in the USP's frame (cm)."""
    me = bpy.data.meshes.new("PistolMag")
    mbm = bmesh.new()
    across = Vector((-MAG_AXIS.z, 0.0, MAG_AXIS.x))  # in the XZ plane, square to the axis
    frame = Matrix((across, Vector((0, 1, 0)), -MAG_AXIS)).transposed()  # local X across, Y, Z up the magazine
    for slot, (start, length, depth, width) in enumerate(((0.0, MAG_LENGTH - 0.5, MAG_DEPTH, MAG_WIDTH),
                                                           (MAG_LENGTH - 0.5, 0.6, MAG_DEPTH + 0.45, MAG_WIDTH + 0.35))):
        geom = bmesh.ops.create_cube(mbm, size=1.0)["verts"]
        centre = MAG_TOP + MAG_AXIS * (start + length / 2)
        bmesh.ops.transform(mbm, verts=geom, matrix=Matrix.Translation(centre) @ frame.to_4x4() @ Matrix.Diagonal((depth, width, length, 1.0)))
        for f in {f for v in geom for f in v.link_faces}:
            f.material_index = slot
    mbm.to_mesh(me); mbm.free()
    for name in ("BlackOxide", "Rubber"):
        me.materials.append(bpy.data.materials.new(name))
    ob = bpy.data.objects.new("PistolMag", me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def keep_only(node, keep):
    part = game.copy(); part.data = game.data.copy(); part.name = node
    bpy.context.scene.collection.objects.link(part)
    pbm = bmesh.new(); pbm.from_mesh(part.data); pbm.verts.ensure_lookup_table()
    bmesh.ops.delete(pbm, geom=[pv for pv in pbm.verts if not keep(pv.index)], context="VERTS")
    pbm.to_mesh(part.data); pbm.free()
    return part


hammer = keep_only("PistolHammer", lambda i: i in hammer_verts)
hammer.data.transform(Matrix.Translation(-HAMMER_PIN))
for name, part in (("SM_Jam_USP_Frame", keep_only("PistolFrame", lambda i: i not in slide_verts and i not in hammer_verts)),
                   ("SM_Jam_USP_Slide", keep_only("PistolSlide", lambda i: i in slide_verts)),
                   ("SM_Jam_USP_Hammer", hammer), ("SM_Jam_USP_Mag", magazine())):
    bpy.ops.object.select_all(action="DESELECT")
    part.select_set(True); bpy.context.view_layer.objects.active = part
    # SM_MK_USP_Soviet's export (MindKore bake_export_usp.py); node names without "USP_" (an FBX collision token).
    bpy.ops.export_scene.fbx(filepath="%s/%s.fbx" % (OUT, name), use_selection=True, object_types={"MESH"}, apply_unit_scale=True,
                             global_scale=1, axis_forward="-Y", axis_up="Z", bake_anim=False, mesh_smooth_type="FACE",
                             use_mesh_modifiers=True, add_leaf_bones=False, path_mode="STRIP")
    xs = [p.co for p in part.data.vertices]
    print("JAMUSP: %s, %d verts, %d faces, bounds cm %s .. %s" % (name, len(xs), len(part.data.polygons),
          [round(min(c[i] for c in xs), 2) for i in range(3)], [round(max(c[i] for c in xs), 2) for i in range(3)]))
    bpy.data.objects.remove(part, do_unlink=True)
print("JAMUSP: hammer pin %s, magazine top %s, axis %s (Blender cm; UE: Y negated)" % (
    tuple(round(c, 2) for c in HAMMER_PIN), tuple(round(c, 2) for c in MAG_TOP), tuple(round(c, 3) for c in MAG_AXIS)))
