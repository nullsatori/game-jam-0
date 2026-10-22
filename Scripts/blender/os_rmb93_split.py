"""The RMB-93's loading lid as its own part (Docs/RecoilWeapons-Rmb93.md).

GPT's game body (export_game_body.py) joined the lid into SM_OS_RMB93_ViewBody, so it could not move.
This rebuilds the two view meshes from the separate assembly exports of ArtSource/Weapons/RMB93/v02:

  SM_OS_RMB93_ViewBody.fbx  Body + Stock + Counter (slots Body 0, Counter 1), the origin as before
  SM_OS_RMB93_Lid.fbx       LoadingLid alone, its origin moved onto the hinge (LoadingLid_Hinge,
                             0.105 / 0 / 0.055 m): the game turns it about its own Y

Run with Blender 4+/bpy:  blender -b -P Scripts/blender/os_rmb93_split.py -- <v02 folder>
(or `python os_rmb93_split.py <v02 folder>` with the bpy module). It only reads the exports and
writes the two FBX next to them; the .blend sources are untouched. Then Scripts/os_rmb93_assets.py.
"""
import sys
from pathlib import Path

import bpy

HINGE = (0.105, 0.0, 0.055)  # exports/pose_contract.json, loading_lid_hinge


def args():
    tail = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
    return Path(tail[0]) if tail else Path(__file__).resolve().parents[2] / 'ArtSource/Weapons/RMB93/v02'


def load(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path))
    new = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
    assert len(new) == 1, (path, [o.name for o in new])
    return new[0]


def export(obj, path):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    # As export_game_body.py: metres, +X forward, +Z up; Unreal imports with Force Front X and yaw +90.
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                             axis_forward='X', axis_up='Z', use_mesh_modifiers=True, add_leaf_bones=False,
                             path_mode='RELATIVE')


def main():
    root = args()
    exports = root / 'exports'
    bpy.ops.wm.read_factory_settings(use_empty=True)
    parts = [load(exports / f'SM_OS_RMB93_{name}.fbx') for name in ('Body', 'Stock', 'Counter')]
    lid = load(exports / 'SM_OS_RMB93_LoadingLid.fbx')
    for obj in parts + [lid]:
        # The FBX importer's own axis turn, applied: the vertices hold the export's coordinates.
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # Every import brings its own copy of the materials (Body.001, ...): one Body, one Counter.
    for obj in parts + [lid]:
        for slot in obj.material_slots:
            slot.material = bpy.data.materials[slot.material.name.split('.')[0]]
    bpy.ops.object.select_all(action='DESELECT')
    for obj in parts:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    body = bpy.context.object
    body.name = body.data.name = 'SM_OS_RMB93_ViewBody'
    # The join keeps a slot per source material: fold the duplicates (Body, Body, Counter -> Body, Counter).
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.material_slot_remove_unused()
    assert [m.name for m in body.data.materials] == ['Body', 'Counter'], [m.name for m in body.data.materials]
    lid.name = lid.data.name = 'SM_OS_RMB93_Lid'
    lid.data.transform(__import__('mathutils').Matrix.Translation([-c for c in HINGE]))
    export(body, exports / 'SM_OS_RMB93_ViewBody.fbx')
    export(lid, exports / 'SM_OS_RMB93_Lid.fbx')
    print('RMB93_SPLIT_PASS', 'ViewBody', len(body.data.polygons), 'Lid', len(lid.data.polygons))


main()
