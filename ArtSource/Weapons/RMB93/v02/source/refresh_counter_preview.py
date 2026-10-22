"""Refresh packed counter artwork after a glyph edit; preserve mesh and UV data."""
from pathlib import Path
import hashlib
import json
import bpy

R = Path(__file__).resolve().parents[1]

def geometry_signature():
    values = []
    for ob in sorted((o for o in bpy.context.scene.objects if o.type == 'MESH' and 'assembly' in o), key=lambda o:o.name):
        values.append((ob.name, str(ob.matrix_world),
                       [tuple(v.co) for v in ob.data.vertices],
                       [tuple(p.vertices) for p in ob.data.polygons],
                       [[tuple(x.uv) for x in layer.data] for layer in ob.data.uv_layers]))
    return hashlib.sha256(repr(values).encode()).hexdigest()

updated = []
for filename in ('source/Jam_RMB93_Editable_v02.blend', 'Jam_RMB93_v02.blend'):
    path = R / filename
    bpy.ops.wm.open_mainfile(filepath=str(path))
    before = geometry_signature()
    changed = []
    for im in bpy.data.images:
        name = Path(im.filepath).name
        if not name.startswith('Counter_Emission4'):
            continue
        source = R / 'counter' / name
        assert source.is_file(), source
        if im.packed_file:
            im.unpack(method='REMOVE')
        im.filepath = str(source)
        im.reload()
        im.pack()
        changed.append(name)
    assert geometry_signature() == before
    if changed:
        bpy.ops.wm.save_as_mainfile(filepath=str(path))
    updated.append({'file':filename,'images':changed,'geometry_uv_unchanged':True})

assert updated[-1]['images'], 'Final native must contain the static digit preview'
# Export only the GLB, whose embedded preview image changes; FBX geometry stays.
objects = [o for o in bpy.context.scene.objects if o.type == 'MESH' and 'assembly' in o]
exports = []
groups = {group:[o for o in objects if o['assembly'] == group] for group in ('Body','Action','Stock','LoadingLid','Counter')}
for group in ('Body','Action','Stock','LoadingLid','Counter'):
    subset = groups[group]
    bpy.ops.object.select_all(action='DESELECT')
    for ob in subset: ob.select_set(True)
    bpy.context.view_layer.objects.active = subset[0]
    bpy.ops.object.join()
    ob = bpy.context.object
    ob.name = 'SM_Jam_RMB93_' + group
    ob['assembly'] = group
    exports.append(ob)
bpy.ops.object.select_all(action='DESELECT')
for ob in exports: ob.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(R/'exports/Jam_RMB93_v02.glb'), export_format='GLB', use_selection=True, export_apply=True, export_yup=True, export_extras=True)
print('COUNTER_PREVIEW_REFRESHED',json.dumps(updated),flush=True)
