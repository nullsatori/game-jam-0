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
barrel, which stays put while the slide runs back and bares it). Writes
SM_Jam_USP_Frame.fbx and SM_Jam_USP_Slide.fbx into out_dir with MindKore's
own export settings, so both land in SM_MK_USP_Soviet's frame (muzzle -X,
grip at the origin) and keep its UVs and material.
"""
import sys

import bmesh
import bpy
from mathutils.bvhtree import BVHTree

ARGS = sys.argv[sys.argv.index("--") + 1:]
SOURCE, OUT = ARGS[0], ARGS[1]
TOP_OF_SLIDE_CM = 5.3

with bpy.data.libraries.load(SOURCE) as (src, dst):
    dst.objects = ["USP_Frame", "USP_Slide"]
trees = {}
for o in dst.objects:
    bm = bmesh.new(); bm.from_mesh(o.data); bm.transform(o.matrix_world)
    trees[o.name] = BVHTree.FromBMesh(bm)  # metres

game = bpy.data.objects["SM_MK_USP_Soviet"]
bm = bmesh.new(); bm.from_mesh(game.data); bm.verts.ensure_lookup_table()
slide_verts, seen = set(), set()
counts = {"slide": 0, "frame": 0}
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
    on_slide = median(trees["USP_Slide"]) < median(trees["USP_Frame"]) or min(a.co.z for a in part) >= TOP_OF_SLIDE_CM
    counts["slide" if on_slide else "frame"] += 1
    if on_slide:
        slide_verts.update(a.index for a in part)
print("JAMUSP: loose parts", counts, "slide verts", len(slide_verts), "of", len(bm.verts))

for name, node, keep_slide in (("SM_Jam_USP_Frame", "PistolFrame", False), ("SM_Jam_USP_Slide", "PistolSlide", True)):
    part = game.copy(); part.data = game.data.copy(); part.name = node
    bpy.context.scene.collection.objects.link(part)
    pbm = bmesh.new(); pbm.from_mesh(part.data); pbm.verts.ensure_lookup_table()
    bmesh.ops.delete(pbm, geom=[pv for pv in pbm.verts if (pv.index in slide_verts) != keep_slide], context="VERTS")
    pbm.to_mesh(part.data); pbm.free()
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
