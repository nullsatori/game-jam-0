"""Update only authored counter textures; preserve body UV, geometry and bake."""
from pathlib import Path
import bpy,bmesh,runpy
from mathutils import Vector
R=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(R/'Jam_RMB93_v02.blend'))
for ob in bpy.data.objects:
    if ob.type!='MESH' or ob.get('assembly')!='Counter':continue
    bm=bmesh.new();bm.from_mesh(ob.data)
    inward=[f for f in bm.faces if f.normal.dot(Vector((-.8,0,.6)))<0]
    if inward:bmesh.ops.reverse_faces(bm,faces=inward)
    bm.to_mesh(ob.data);bm.free();ob.data.update()
mat=bpy.data.materials['Counter']
for node in mat.node_tree.nodes:
    if node.type!='TEX_IMAGE' or not node.image:continue
    old=node.image;name=Path(old.filepath).name
    folder='textures' if name.startswith('T_Jam') else 'counter'
    path=R/folder/name
    if not path.exists():raise FileNotFoundError(path)
    new=bpy.data.images.load(str(path),check_existing=False)
    new.colorspace_settings.name=old.colorspace_settings.name
    new.pack();node.image=new
    if old.users==0:bpy.data.images.remove(old)
bpy.ops.wm.save_as_mainfile(filepath=str(R/'Jam_RMB93_v02.blend'))
runpy.run_path(str(R/'source/finalize_export.py'),run_name='__main__')
