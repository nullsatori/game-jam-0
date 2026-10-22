"""Join stationary pieces for the game's body + moving-action weapon contract."""
import bpy
import hashlib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
native = root / 'Jam_RMB93_v02.blend'
before = hashlib.sha256(native.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(native))
parts = [o for o in bpy.context.scene.objects if o.type == 'MESH' and 'assembly' in o and o['assembly'] != 'Action']
assert {o['assembly'] for o in parts} == {'Body', 'Stock', 'LoadingLid', 'Counter'}
bpy.ops.object.select_all(action='DESELECT')
for o in parts:
    o.hide_set(False)
    o.select_set(True)
bpy.context.view_layer.objects.active = next(o for o in parts if o['assembly'] == 'Body')
bpy.ops.object.join()
body = bpy.context.object
body.name = 'SM_Jam_RMB93_ViewBody'
bpy.context.scene.cursor.location = (0, 0, 0)
bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
assert [m.name for m in body.data.materials] == ['Body', 'Counter']
bpy.ops.export_scene.fbx(filepath=str(root/'exports/SM_Jam_RMB93_ViewBody.fbx'), use_selection=True,
    object_types={'MESH'}, apply_unit_scale=True, axis_forward='X', axis_up='Z',
    use_mesh_modifiers=True, add_leaf_bones=False, path_mode='RELATIVE')
assert hashlib.sha256(native.read_bytes()).hexdigest() == before
print('GAME_BODY_EXPORT_PASS', len(body.data.polygons), 'Body=0 Counter=1; source unchanged')
