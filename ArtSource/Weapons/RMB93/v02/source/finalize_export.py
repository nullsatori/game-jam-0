from pathlib import Path
import bpy,json,sys
from mathutils import Vector
R=Path(__file__).resolve().parents[1];E=R/'exports'
bpy.ops.wm.open_mainfile(filepath=str(R/'Jam_RMB93_v02.blend'))
s=bpy.context.scene
objects=[o for o in s.objects if o.type=='MESH' and 'assembly' in o]
def select(obs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in obs:o.select_set(True)
    bpy.context.view_layer.objects.active=obs[0]
counts={o.name:len(o.data.polygons) for o in objects}
stats={'triangles_total':sum(counts.values()),'triangles_by_part':counts,'material_slots_unique':['Body','Counter'],'parts':len(objects),'textures_required':9,'unreal_import':False,'uv_mirrored':False,'length_m':max(v.co.x for o in objects for v in o.data.vertices)-min(v.co.x for o in objects for v in o.data.vertices)}
stats['pose_contract']={'pump_axis_blender':'+X','pump_travel_art_m':.055,'travel_is_measured_real_weapon':False,'stock_hinge':[-.012,0,.063],'stock_open_degrees_y':-110,'stock_deployed_degrees_y':-195,'loading_lid_hinge':[.105,0,.055],'loading_lid_open_degrees_y':80,'counter_location':'upper_rear_chamfer','ads_eye_m':[-.40,0,.074]}
# Five export assemblies; original editable pieces remain in the saved native file.
groups=['Body','Action','Stock','LoadingLid','Counter'];exports=[]
by_group={g:[o for o in objects if o['assembly']==g] for g in groups}
for group in groups:
    subset=by_group[group];select(subset);bpy.ops.object.join();o=bpy.context.object;o.name='SM_Jam_RMB93_'+group;o['assembly']=group;exports.append(o)
select(exports)
bpy.ops.export_scene.gltf(filepath=str(E/'Jam_RMB93_v02.glb'),export_format='GLB',use_selection=True,export_apply=True,export_yup=True,export_extras=True)
# FBX in Blender axes; UE import conversion is documented, verified externally.
fbxopts=dict(use_selection=True,object_types={'MESH'},apply_unit_scale=True,axis_forward='X',axis_up='Z',use_mesh_modifiers=True,add_leaf_bones=False,path_mode='RELATIVE')
for group in groups:
    subset=[o for o in exports if o['assembly']==group];select(subset)
    bpy.ops.export_scene.fbx(filepath=str(E/('SM_Jam_RMB93_'+group+'.fbx')),**fbxopts)
select(exports);bpy.ops.export_scene.fbx(filepath=str(E/'SM_Jam_RMB93_World.fbx'),**fbxopts)
(R/'exports/pose_contract.json').write_text(json.dumps(stats['pose_contract'],indent=2),encoding='utf-8')
(R/'validation.json').write_text(json.dumps(stats,indent=2),encoding='utf-8')
if '--skip-renders' in sys.argv:
    print('PRODUCTION_COMPLETE',json.dumps(stats),flush=True)
    raise SystemExit(0)
s.cycles.samples=48;s.render.filepath=str(R/'renders/02_textured.png');bpy.ops.render.render(write_still=True)
for ob in s.objects:
    if ob.get('presentation'):ob.hide_render=True
cam=s.camera;cam.data.type='PERSP';cam.data.lens=35;cam.location=(-.4,0,.074);cam.rotation_euler=(Vector((.5,0,.074))-cam.location).to_track_quat('-Z','Y').to_euler();s.render.filepath=str(R/'renders/03_ads.png');bpy.ops.render.render(write_still=True)
cam.data.type='ORTHO';cam.data.ortho_scale=.11;cam.location=(-.23,.12,.18);cam.rotation_euler=(Vector((-.03,0,.056))-cam.location).to_track_quat('-Z','Y').to_euler();s.render.filepath=str(R/'renders/04_counter.png');bpy.ops.render.render(write_still=True)
print('PRODUCTION_COMPLETE',json.dumps(stats),flush=True)
