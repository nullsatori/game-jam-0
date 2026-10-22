"""UV, same-surface PBR baking, portable editable delivery and game exports."""
from pathlib import Path
import bpy, math, json, numpy as np
from mathutils import Vector
R=Path(__file__).resolve().parents[1];T=R/'textures';E=R/'exports'
bpy.ops.wm.open_mainfile(filepath=str(R/'source/Jam_RMB93_Editable_v02.blend'))
s=bpy.context.scene
objects=[o for o in s.objects if o.type=='MESH' and 'assembly' in o]
body=[o for o in objects if o['assembly']!='Counter'];counter=next(o for o in objects if o['assembly']=='Counter')
def select(obs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in obs:o.select_set(True)
    bpy.context.view_layer.objects.active=obs[0]
for o in objects:
    select([o])
    for mod in list(o.modifiers):bpy.ops.object.modifier_apply(modifier=mod.name)
    mod=o.modifiers.new('Final triangulation','TRIANGULATE');mod.keep_custom_normals=True;bpy.ops.object.modifier_apply(modifier=mod.name)
select(body);bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=0,area_weight=.1)
bpy.ops.object.mode_set(mode='OBJECT')
# Give first-person contact surfaces more atlas area; preserve relative island
# scale during the common pack instead of raising every texture to 8K.
for ob in body:
    weight=2.0 if ob.name.startswith('Counter_') else 1.45 if ob.name.startswith('Grip_') or ob.name=='Pistol_grip' else 1.30 if ob.name in ('Receiver_shell','Loading_lid') else 1.0
    for loop in ob.data.uv_layers.active.data:
        loop.uv=(loop.uv.x*weight,loop.uv.y*weight)
    ob['uv_priority_scale']=weight
select(body);bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.pack_islands(rotate=True,margin_method='FRACTION',margin=.003)
bpy.ops.object.mode_set(mode='OBJECT')
materials=list({m for o in body for m in o.data.materials})
saved={m.name:m.node_tree.nodes['Material Output'].inputs['Surface'].links[0].from_socket for m in materials}

# All source transforms are already applied. Bake one temporary joined copy:
# same UVs, split normals and Object coordinates, without processing a full 4K
# target separately for each of the 44 editable parts on every pass.
bake_copies=[]
visibility={o:o.hide_render for o in body}
for o in body:
    assert all(abs(o.matrix_world[i][j]-(1 if i==j else 0))<1e-6 for i in range(4) for j in range(4)),o.name
    copy=o.copy();copy.data=o.data.copy();s.collection.objects.link(copy)
    copy.hide_render=False;bake_copies.append(copy);o.hide_render=True
select(bake_copies);bpy.ops.object.join();bake_body=bpy.context.object
bake_body.name='Temporary_combined_bake_surface'
targets={}
for m in materials:
    n=m.node_tree.nodes.new('ShaderNodeTexImage');n.name='BAKE_TARGET';targets[m.name]=n
def image(name,size,linear=True):
    im=bpy.data.images.new(name,width=size,height=size,alpha=False);im.colorspace_settings.name='Non-Color' if linear else 'sRGB';return im
def save(im,path):
    im.filepath_raw=str(path);im.file_format='PNG';im.save()
def bake(label,size,mode):
    im=image('T_Jam_RMB93_'+label,size,label!='BC')
    # Transparent-clear is not used: all maps are RGB, padded by the baker.
    for m in materials:
        ns,ls=m.node_tree.nodes,m.node_tree.links;p=ns['Principled BSDF'];out=ns['Material Output']
        for link in list(out.inputs['Surface'].links):ls.remove(link)
        ls.new(saved[m.name],out.inputs['Surface'])
        if mode!='NORMAL':
            em=ns.new('ShaderNodeEmission');ls.new(em.outputs[0],out.inputs['Surface'])
            if label=='BC':
                inp=p.inputs['Base Color']
                if inp.is_linked:ls.new(inp.links[0].from_socket,em.inputs['Color'])
                else:em.inputs['Color'].default_value=inp.default_value
            else:
                rgb=ns.new('ShaderNodeCombineColor');rgb.mode='RGB';ls.new(rgb.outputs[0],em.inputs[0])
                ao=ns.new('ShaderNodeAmbientOcclusion');ao.inputs['Distance'].default_value=.006;ao.samples=16
                if label=='ORM':
                    ls.new(ao.outputs['AO'],rgb.inputs[0])
                    for i,k in [(1,'Roughness'),(2,'Metallic')]:
                        inp=p.inputs[k]
                        if inp.is_linked:ls.new(inp.links[0].from_socket,rgb.inputs[i])
                        else:rgb.inputs[i].default_value=inp.default_value
                else:
                    wear=next((n for n in ns if n.label=='WearMask'),None)
                    if wear:ls.new(wear.outputs[0],rgb.inputs[0])
                    inv=ns.new('ShaderNodeMath');inv.operation='SUBTRACT';inv.inputs[0].default_value=1;ls.new(ao.outputs['AO'],inv.inputs[1])
                    dirt=next((n for n in ns if n.label=='DirtMask'),None)
                    if dirt:
                        combined=ns.new('ShaderNodeMath');combined.operation='MAXIMUM';ls.new(inv.outputs[0],combined.inputs[0]);ls.new(dirt.outputs[0],combined.inputs[1]);ls.new(combined.outputs[0],rgb.inputs[1])
                    else:ls.new(inv.outputs[0],rgb.inputs[1])
                    rgb.inputs[2].default_value=1 if 'Oxblood' in m.name else 0
        targets[m.name].image=im;ns.active=targets[m.name]
    select([bake_body]);s.render.bake.margin=size//128;s.render.bake.use_clear=False;s.render.bake.use_selected_to_active=False
    s.render.bake.normal_space='TANGENT';s.render.bake.normal_g='NEG_Y'
    bpy.ops.object.bake(type=mode)
    save(im,T/(im.name+'.png'));print('BAKED',label,flush=True)
    return im
s.cycles.samples=16
bc=bake('BC',4096,'EMIT');orm=bake('ORM',4096,'EMIT');masks=bake('Masks',1024,'EMIT');normal=bake('N',4096,'NORMAL')
bpy.data.objects.remove(bake_body,do_unlink=True)
for o,hidden in visibility.items():o.hide_render=hidden

def load(path,linear=False):
    im=bpy.data.images.load(str(path),check_existing=True)
    im.colorspace_settings.name='Non-Color' if linear else 'sRGB';return im
def normal_gl(im,path):
    a=np.array(im.pixels[:],dtype=np.float32).reshape(-1,4);a[:,1]=1-a[:,1]
    out=image(path.stem,im.size[0],True);out.pixels.foreach_set(a.ravel());save(out,path);return out
ngl=normal_gl(normal,R/'source/Body_N_OpenGL.png')
def pbr(name,base,norm,packed):
    m=bpy.data.materials.new(name);m.use_nodes=True;ns,ls=m.node_tree.nodes,m.node_tree.links;p=ns['Principled BSDF']
    def tex(im):n=ns.new('ShaderNodeTexImage');n.image=im;return n
    ls.new(tex(base).outputs['Color'],p.inputs['Base Color']);sep=ns.new('ShaderNodeSeparateColor');ls.new(tex(packed).outputs[0],sep.inputs[0]);ls.new(sep.outputs[1],p.inputs['Roughness']);ls.new(sep.outputs[2],p.inputs['Metallic'])
    n=ns.new('ShaderNodeNormalMap');ls.new(tex(norm).outputs[0],n.inputs[1]);ls.new(n.outputs[0],p.inputs['Normal'])
    # Exporter recognizes glTF Material Output occlusion socket.
    group=bpy.data.node_groups.get('glTF Material Output')
    if not group:
        group=bpy.data.node_groups.new('glTF Material Output','ShaderNodeTree');group.interface.new_socket(name='Occlusion',in_out='INPUT',socket_type='NodeSocketFloat')
    g=ns.new('ShaderNodeGroup');g.node_tree=group;ls.new(sep.outputs[0],g.inputs['Occlusion'])
    return m
bodymat=pbr('Body',bc,ngl,orm)
cmat=pbr('Counter',load(T/'T_Jam_RMB93_Counter_BC.png'),load(R/'counter/Counter_N_OpenGL.png',True),load(T/'T_Jam_RMB93_Counter_ORM.png',True))
pn=cmat.node_tree.nodes['Principled BSDF'];em=cmat.node_tree.nodes.new('ShaderNodeTexImage');em.image=load(R/'counter/Counter_Emission4_sRGB.png',False);cmat.node_tree.links.new(em.outputs[0],pn.inputs['Emission Color']);pn.inputs['Emission Strength'].default_value=8.35
pn.inputs['Specular IOR Level'].default_value=.18
for o in body:o.data.materials.clear();o.data.materials.append(bodymat)
counter.data.materials.clear();counter.data.materials.append(cmat)
# Keep source parts editable; packed images make the blend portable.
for im in bpy.data.images:
    if im.source=='FILE' or im.name.startswith('T_'):
        try:im.pack()
        except RuntimeError:pass
select(objects)
bpy.ops.wm.save_as_mainfile(filepath=str(R/'Jam_RMB93_v02.blend'))
# Native .blend + GLB retain named parts; world FBX combines by assembly.
import runpy
runpy.run_path(str(R/'source/finalize_export.py'),run_name='__main__')
