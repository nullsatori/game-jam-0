"""Refresh the four presentation frames from the final saved native asset."""
from pathlib import Path
import bpy
from mathutils import Vector

root=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(root/'Jam_RMB93_v02.blend'))
scene=bpy.context.scene
scene.cycles.samples=32
scene.cycles.use_denoising=True
camera=scene.camera

def capture(name):
    scene.render.filepath=str(root/'renders'/name)
    bpy.ops.render.render(write_still=True)
    assert Path(scene.render.filepath).stat().st_size>10000

clay=bpy.data.materials.new('Geometry_review_clay');clay.use_nodes=True
clay.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.15,.17,.16,1)
clay.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.55
scene.view_layers[0].material_override=clay
capture('01_geometry.png')
scene.view_layers[0].material_override=None
capture('02_textured.png')
for obj in scene.objects:
    if obj.get('presentation'):obj.hide_render=True
camera.data.type='PERSP';camera.data.lens=35;camera.location=(-.4,0,.074)
camera.rotation_euler=(Vector((.5,0,.074))-camera.location).to_track_quat('-Z','Y').to_euler()
capture('03_ads.png')
camera.data.type='ORTHO';camera.data.ortho_scale=.11;camera.location=(-.23,.12,.18)
camera.rotation_euler=(Vector((-.03,0,.056))-camera.location).to_track_quat('-Z','Y').to_euler()
capture('04_counter.png')
print('HANDOFF_FRAMES_COMPLETE',flush=True)
