"""Editable exterior game-art mesh. No internal mechanisms. Blender 5.2."""
from pathlib import Path
import bpy, bmesh, math, json, sys
sys.path.insert(0,str(Path(__file__).parent))
from composite_material import apply_composite
from refine_contact_normals import refine_contact_normals
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.preferences.filepaths.save_version=0
s=bpy.context.scene
s.unit_settings.system='METRIC'
parts=[]
def node(mat,kind): return mat.node_tree.nodes.new(kind)

def contact_mask(m,coords,sites):
    ns,ls=m.node_tree.nodes,m.node_tree.links
    split=node(m,'ShaderNodeSeparateXYZ');ls.new(coords,split.inputs[0])
    ay=node(m,'ShaderNodeMath');ay.operation='ABSOLUTE';ls.new(split.outputs[1],ay.inputs[0])
    position=node(m,'ShaderNodeCombineXYZ');ls.new(split.outputs[0],position.inputs[0]);ls.new(ay.outputs[0],position.inputs[1]);ls.new(split.outputs[2],position.inputs[2])
    result=None
    for center,radii in sites:
        offset=node(m,'ShaderNodeVectorMath');offset.operation='SUBTRACT';offset.inputs[1].default_value=center;ls.new(position.outputs[0],offset.inputs[0])
        scale=node(m,'ShaderNodeVectorMath');scale.operation='MULTIPLY';scale.inputs[1].default_value=tuple(1/r for r in radii);ls.new(offset.outputs[0],scale.inputs[0])
        dist=node(m,'ShaderNodeVectorMath');dist.operation='LENGTH';ls.new(scale.outputs[0],dist.inputs[0])
        mask=node(m,'ShaderNodeMapRange');mask.clamp=True;mask.interpolation_type='SMOOTHERSTEP';mask.inputs['From Min'].default_value=.35;mask.inputs['From Max'].default_value=1.2;mask.inputs['To Min'].default_value=1;mask.inputs['To Max'].default_value=0;ls.new(dist.outputs['Value'],mask.inputs[0])
        if result is None:result=mask.outputs[0]
        else:
            maximum=node(m,'ShaderNodeMath');maximum.operation='MAXIMUM';ls.new(result,maximum.inputs[0]);ls.new(mask.outputs[0],maximum.inputs[1]);result=maximum.outputs[0]
    return result
def mat(name,c,metal,rough,kind='steel'):
    m=bpy.data.materials.new(name); m.use_nodes=True; m.diffuse_color=(*c,1)
    p=m.node_tree.nodes.get('Principled BSDF'); l=m.node_tree.links
    p.inputs['Base Color'].default_value=(*c,1); p.inputs['Metallic'].default_value=metal; p.inputs['Roughness'].default_value=rough
    tex=node(m,'ShaderNodeTexCoord')
    noise=node(m,'ShaderNodeTexNoise'); noise.inputs['Scale'].default_value=1800;noise.inputs['Detail'].default_value=2
    l.new(tex.outputs['Position'] if 'Position' in tex.outputs else tex.outputs['Object'],noise.inputs['Vector'])
    ramp=node(m,'ShaderNodeValToRGB');ramp.color_ramp.elements[0].color=(*(rough-.10,)*3,1);ramp.color_ramp.elements[1].color=(*(rough+.09,)*3,1)
    l.new(noise.outputs['Fac'],ramp.inputs[0]);l.new(ramp.outputs[0],p.inputs['Roughness'])
    bump=node(m,'ShaderNodeBump');bump.inputs['Strength'].default_value=.25;bump.inputs['Distance'].default_value=.000045
    l.new(noise.outputs['Fac'],bump.inputs['Height']);l.new(bump.outputs['Normal'],p.inputs['Normal'])
    if kind=='steel':
        im=node(m,'ShaderNodeTexImage');im.image=bpy.data.images.load(str(ROOT/'material_sources/BluedSteel_Source.png'),check_existing=True)
        im.projection='BOX'; im.projection_blend=.3
        mapping=node(m,'ShaderNodeVectorMath');mapping.operation='SCALE';mapping.inputs[3].default_value=6
        l.new(tex.outputs['Object'],mapping.inputs[0]);l.new(mapping.outputs[0],im.inputs['Vector'])
        mix=node(m,'ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1;mix.inputs[2].default_value=(1.5,1.65,1.7,1)
        l.new(im.outputs['Color'],mix.inputs[1]);l.new(mix.outputs[0],p.inputs['Base Color'])
        # Restrained edge polishing from true mesh curvature, baked on the final UVs.
        geo=node(m,'ShaderNodeNewGeometry');wear=node(m,'ShaderNodeValToRGB')
        wear.color_ramp.elements[0].position=.505;wear.color_ramp.elements[0].color=(0,0,0,1)
        wear.color_ramp.elements[1].position=.59;wear.color_ramp.elements[1].color=(.5,.5,.5,1)
        # Curvature interpolation on a triangulated low-poly wall creates triangular
        # paint patches. Keep wear zero here; surface patina comes from the color source.
        wear.color_ramp.elements[1].color=(0,0,0,1);wear.label='WearMask'
        quiet=node(m,'ShaderNodeMixRGB');quiet.blend_type='MIX';quiet.inputs[0].default_value=.48;quiet.inputs[2].default_value=(.048,.055,.061,1)
        l.new(mix.outputs[0],quiet.inputs[1]);l.new(quiet.outputs[0],p.inputs['Base Color'])
        patina=node(m,'ShaderNodeValToRGB');patina.color_ramp.elements[0].position=.014;patina.color_ramp.elements[1].position=.047;patina.color_ramp.elements[0].color=(.39,.39,.39,1);patina.color_ramp.elements[1].color=(.49,.49,.49,1)
        l.new(im.outputs['Color'],patina.inputs[0]);l.new(patina.outputs[0],p.inputs['Roughness'])
        brushed=node(m,'ShaderNodeVectorMath');brushed.operation='MULTIPLY';brushed.inputs[1].default_value=(60,900,2300)
        l.new(tex.outputs['Object'],brushed.inputs[0]);grain=node(m,'ShaderNodeTexNoise');grain.inputs['Scale'].default_value=1;grain.inputs['Detail'].default_value=2;l.new(brushed.outputs[0],grain.inputs[0])
        l.new(grain.outputs['Fac'],bump.inputs['Height']);bump.inputs['Distance'].default_value=.000035;bump.inputs['Strength'].default_value=.3
        # Fine directional grain carries the close-up finish; broad source patina
        # only adds subdued color variation, never the apparent surface scale.
        micro=node(m,'ShaderNodeValToRGB');micro.color_ramp.elements[0].color=(.88,.88,.88,1);micro.color_ramp.elements[1].color=(1,1,1,1);l.new(grain.outputs['Fac'],micro.inputs[0])
        brushed_color=node(m,'ShaderNodeMixRGB');brushed_color.blend_type='MULTIPLY';brushed_color.inputs[0].default_value=1;l.new(quiet.outputs[0],brushed_color.inputs[1]);l.new(micro.outputs[0],brushed_color.inputs[2]);l.new(brushed_color.outputs[0],p.inputs['Base Color'])
    if kind in ('check','composite'):
        apply_composite(m,p,tex.outputs['Object'],bump,ROOT,insert=kind=='check')
    if kind=='stock_rubber':
        # Quiet molded elastomer: fine pores, a satin contact face, no fibrous
        # composite grain or sprayed-on metallic highlights. All are baked.
        noise.inputs['Scale'].default_value=1800
        noise.inputs['Detail'].default_value=2
        bump.inputs['Strength'].default_value=.55
        bump.inputs['Distance'].default_value=.00010
        broad=node(m,'ShaderNodeTexNoise');broad.inputs['Scale'].default_value=85
        broad.inputs['Detail'].default_value=2;l.new(tex.outputs['Object'],broad.inputs['Vector'])
        shade=node(m,'ShaderNodeValToRGB')
        shade.color_ramp.elements[0].color=(.016,.018,.019,1)
        shade.color_ramp.elements[1].color=(.018,.020,.021,1)
        l.new(broad.outputs['Fac'],shade.inputs[0])
        pore_color=node(m,'ShaderNodeValToRGB')
        pore_color.color_ramp.elements[0].color=(.94,.94,.94,1)
        pore_color.color_ramp.elements[1].color=(1.02,1.02,1.02,1)
        l.new(noise.outputs['Fac'],pore_color.inputs[0])
        tinted=node(m,'ShaderNodeMixRGB');tinted.blend_type='MULTIPLY';tinted.inputs[0].default_value=1
        l.new(shade.outputs[0],tinted.inputs[1]);l.new(pore_color.outputs[0],tinted.inputs[2]);l.new(tinted.outputs[0],p.inputs['Base Color'])
        ramp.color_ramp.elements[0].color=(.69,.69,.69,1)
        ramp.color_ramp.elements[1].color=(.82,.82,.82,1)
    if 'Oxblood' in name:
        marble=node(m,'ShaderNodeTexNoise');marble.inputs['Scale'].default_value=220;marble.inputs['Detail'].default_value=3
        l.new(tex.outputs['Object'],marble.inputs['Vector'])
        tint=node(m,'ShaderNodeValToRGB');tint.color_ramp.elements[0].color=(.010,.0025,.003,1);tint.color_ramp.elements[1].color=(.032,.007,.009,1)
        l.new(marble.outputs['Fac'],tint.inputs[0]);l.new(tint.outputs[0],p.inputs['Base Color'])
    # Deliberately placed rubs: edges and hand contact, never blanket curvature wear.
    if kind in ('steel','composite','check'):
        if kind=='steel':
            sites=[((.119,.0175,.048),(.008,.004,.0012)),((.000,.021,.014),(.014,.004,.0009)),((.335,.020,.059),(.025,.003,.0008))]
            polish=(.12,.13,.135,1);rough_target=.32
        elif kind=='composite':
            sites=[((.223,.0205,.022),(.032,.003,.0012)),((.158,.0205,.001),(.019,.003,.005)),((-.088,.0175,-.056),(.008,.003,.021))]
            polish=(.022,.026,.023,1);rough_target=.39
        else:
            sites=[((-.089,.0185,-.060),(.008,.003,.022))]
            polish=(.018,.022,.019,1);rough_target=.43
        mask=contact_mask(m,tex.outputs['Object'],sites)
        if kind in ('steel','composite','check'):
            scuff_vec=node(m,'ShaderNodeVectorMath');scuff_vec.operation='MULTIPLY';scuff_vec.inputs[1].default_value=(120,700,4200);l.new(tex.outputs['Object'],scuff_vec.inputs[0])
            scuff=node(m,'ShaderNodeTexNoise');scuff.inputs['Scale'].default_value=1;scuff.inputs['Detail'].default_value=2;l.new(scuff_vec.outputs[0],scuff.inputs[0])
            broken=node(m,'ShaderNodeMath');broken.operation='MULTIPLY';l.new(mask,broken.inputs[0]);l.new(scuff.outputs['Fac'],broken.inputs[1]);mask=broken.outputs[0]
        if kind in ('composite','check'):
            burnish=node(m,'ShaderNodeMapRange');burnish.inputs['To Min'].default_value=.7;burnish.inputs['To Max'].default_value=.25;l.new(mask,burnish.inputs[0]);l.new(burnish.outputs[0],bump.inputs['Strength'])
        channels=[('Roughness',(rough_target,rough_target,rough_target,1))]
        if kind=='steel':channels.insert(0,('Base Color',polish))
        for socket,target in channels:
            inp=p.inputs[socket];original=inp.links[0].from_socket if inp.is_linked else None
            mix=node(m,'ShaderNodeMixRGB');mix.label='Source contact '+socket;mix.inputs[2].default_value=target;l.new(mask,mix.inputs[0])
            if original:l.new(original,mix.inputs[1])
            else:mix.inputs[1].default_value=inp.default_value if socket=='Base Color' else (inp.default_value,)*3+(1,)
            l.new(mix.outputs[0],inp)
        if kind=='steel':
            wear.label='WearMask';wear.name='Legacy_zero_wear'
            # Baker uses this labeled output as the authored R channel.
            wear.label='';authored=node(m,'ShaderNodeMath');authored.operation='MULTIPLY';authored.inputs[1].default_value=1;authored.label='WearMask';l.new(mask,authored.inputs[0])
    m['kind']=kind
    return m
steel=mat('Source_BluedSteel',(.105,.118,.125),.88,.47)
edge=mat('Source_WornContactSteel',(.15,.16,.16),.90,.42)
poly=mat('Source_MouldedPolymer',(.026,.029,.028),0,.62,'poly')
composite=mat('Source_PressedComposite',(.018,.021,.019),0,.72,'composite')
rubber=mat('Source_Rubber',(.011,.013,.012),0,.8,'poly')
burg=mat('Source_OxbloodPhenolic',(.080,.018,.014),0,.44,'poly')
check=mat('Source_GraphiteCheckering',(.021,.023,.023),0,.66,'check')
black=mat('Source_Recess',(.003,.004,.004),.1,.8,'poly')
ivory=mat('Source_SightWitnessPaint',(.42,.38,.27),0,.68,'poly')

def finish(o,m,group='Body',bevel=0,segments=2):
    o.data.materials.append(m);o['assembly']=group;parts.append(o)
    if bevel:
        mod=o.modifiers.new('Editable edge radii','BEVEL');mod.width=bevel;mod.segments=segments
    return o
def mesh(name,v,f,m=steel,group='Body',bevel=0):
    me=bpy.data.meshes.new(name);me.from_pydata(v,[],f);me.update()
    ob=bpy.data.objects.new(name,me);s.collection.objects.link(ob)
    bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(me);bm.free()
    return finish(ob,m,group,bevel)
def prism(name,xz,hw,m=steel,group='Body',bevel=.0007):
    n=len(xz);v=[(x,y,z) for y in (-hw,hw) for x,z in xz]
    f=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,v,f,m,group,bevel)
def box(name,c,dim,m=steel,group='Body',bevel=.0005):
    bpy.ops.mesh.primitive_cube_add(size=1,location=c);o=bpy.context.object;o.name=name;o.dimensions=dim
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    return finish(o,m,group,bevel)
def cyl(name,a,b,r,m=steel,group='Body',n=20,bevel=.0003):
    a,b=Vector(a),Vector(b);bpy.ops.mesh.primitive_cylinder_add(vertices=n,radius=r,depth=(b-a).length,location=(a+b)/2)
    o=bpy.context.object;o.name=name;o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler()
    for p in o.data.polygons:p.use_smooth=len(p.vertices)==4
    return finish(o,m,group,bevel,segments=1)
def px(points):return [((1345-x)*.000415,(451-z)*.000415) for x,z in points]
def rr(cx,cz,w,h,r,n=4):
    a=[]
    for x,z,t in [(cx+w/2-r,cz+h/2-r,0),(cx-w/2+r,cz+h/2-r,90),(cx-w/2+r,cz-h/2+r,180),(cx+w/2-r,cz-h/2+r,270)]:
        for i in range(n+1):
            d=math.radians(t+i*90/n);a.append((x+r*math.cos(d),z+r*math.sin(d)))
    return a
def panel(name,xz,y,thick,m,group='Body',bevel=.0003):
    o=prism(name,xz,thick/2,m,group,bevel);o.location.y=y;return o
def strip(name,outer,inner,hw,m=steel,group='Body'):
    n=len(outer);v=[(x,y,z) for y in (-hw,hw) for loop in (outer,inner) for x,z in loop];f=[]
    for i in range(n):
        j=(i+1)%n
        f += [(i,j,n+j,n+i),(2*n+i,3*n+i,3*n+j,2*n+j),(i,2*n+i,2*n+j,j),(n+i,n+j,3*n+j,3*n+i)]
    return mesh(name,v,f,m,group,.00035)

# Coordinates authored against the accepted concept, preserving external outline.
from receiver_geometry import build_receiver
receiver=build_receiver(prism,mesh,parts,steel,bpy)
cx,cz=-.030,.056
# Rear instrument face: screen-right = DCC -Y, up follows the receiver slope.
counter_origin=Vector((cx,0,cz));counter_up=Vector((.6,0,.8));counter_normal=Vector((-.8,0,.6));counter_right=Vector((0,-1,0))
def instrument_point(u,v,depth=0):return tuple(counter_origin+counter_right*u+counter_up*v+counter_normal*depth)
outline=rr(0,0,.027,.021,.003)
n=len(outline);vv=[instrument_point(u,v,d) for d in (-.004,.008) for u,v in outline]
ff=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
cut=mesh('Rear_window_cut',vv,ff,black)
mod=receiver.modifiers.new('Recessed display pocket','BOOLEAN');mod.operation='DIFFERENCE';mod.object=cut
bpy.context.view_layer.objects.active=receiver;receiver.modifiers.move(len(receiver.modifiers)-1,0);bpy.ops.object.modifier_apply(modifier=mod.name)
parts.remove(cut);bpy.data.objects.remove(cut,do_unlink=True)

def subtract(ob,cutter):
    bpy.context.view_layer.objects.active=ob;mod=ob.modifiers.new('Exterior clearance','BOOLEAN');mod.operation='DIFFERENCE';mod.object=cutter
    ob.modifiers.move(len(ob.modifiers)-1,0);bpy.ops.object.modifier_apply(modifier=mod.name)
    parts.remove(cutter);bpy.data.objects.remove(cutter,do_unlink=True)
# Art-only clearance for the moving exterior barrel, no mechanical internals.
cut=cyl('Barrel_clearance',(.010,0,.0075),(.145,0,.0075),.0115,black,n=20,bevel=0);subtract(receiver,cut)
# A visible top loading aperture and separately hinged lid for animation blocking.
cut=box('Loading_aperture',(.062,0,.054),(.071,.023,.022),black,bevel=0);subtract(receiver,cut)
box('Loading_aperture_dark_bed',(.062,0,.0435),(.069,.021,.001),black,bevel=0)
# A narrow underside tongue seats inside the aperture; the lid's outer
# flange remains 0.3 mm above the receiver with a controlled visible seam.
lid_outline=rr(.062,0,.076,.027,.003,n=3)
lid_seat=rr(.062,0,.070,.022,.002,n=3);n=len(lid_outline)
vv=[(x,y,z) for z,profile in [(.0523,lid_seat),(.05365,lid_seat),(.0538,lid_outline),(.0555,lid_outline)] for x,y in profile]
ff=[tuple(range(n-1,-1,-1)),tuple(range(3*n,4*n))]
for k in range(3):
    ff.extend((k*n+i,k*n+(i+1)%n,(k+1)*n+(i+1)%n,(k+1)*n+i) for i in range(n))
lid=mesh('Loading_lid',vv,ff,steel,'LoadingLid',.00012)
lid.modifiers[-1].segments=1
lid['design_note']='Stepped underside seat with controlled receiver clearance and thin steel flange'
cyl('Loading_lid_hinge',(.105,-.012,.055),(.105,.012,.055),.002,edge,n=20,bevel=.00015)

# Parallel tubes, capped upper tube and a shallow artistic dark muzzle.
cyl('Upper_magazine',(.092,0,.0423),(.532,0,.0423),.0142,n=28,bevel=.00018)
cyl('Upper_front_cap',(.529,0,.0423),(.539,0,.0423),.0146,edge,n=32,bevel=.00018)
cyl('Upper_rear_ferrule',(.125,0,.0423),(.135,0,.0423),.0148,steel,n=28,bevel=.00018)
cyl('Lower_barrel',(.042,0,.0075),(.538,0,.0075),.0108,steel,'Action',n=28,bevel=.00018)
v=[];f=[];n=32
for x,r in [(.531,.014),(.546,.014),(.546,.0097),(.538,.0097)]:
    for i in range(n):
        a=2*math.pi*i/n;v.append((x,r*math.sin(a),.0075+r*math.cos(a)))
for k in range(3):
    for i in range(n):f.append((k*n+i,k*n+(i+1)%n,(k+1)*n+(i+1)%n,(k+1)*n+i))
o=mesh('Muzzle_rim',v,f,edge,'Action',.00012)
cyl('Muzzle_dark_recess',(.536,0,.0075),(.537,0,.0075),.0097,black,'Action',n=32,bevel=0)
cyl('Barrel_front_band',(.520,0,.0075),(.528,0,.0075),.0128,steel,'Action',n=28,bevel=.00018)
prism('Front_sight_base',[(.505,.0562),(.509,.0592),(.520,.0592),(.525,.0562)],.004,steel,bevel=.00025)
prism('Front_sight',[(.511,.060),(.517,.060),(.516,.074),(.5145,.074)],.0008,edge,bevel=.00015)
mesh('Front_sight_witness',[(.51384,-.00035,.0714),(.51384,.00035,.0714),(.51432,.00035,.0734),(.51432,-.00035,.0734)],[(0,1,2,3)],ivory)

# Detailed fore-end authored separately for easy manual replacement.
import sys
sys.path.insert(0,str(Path(__file__).parent))
from foreend_geometry import build_foreend
fore=build_foreend(mesh,prism,panel,cyl,rr,parts,composite,steel,edge,burg,rubber,bpy)
from perforated_sleeve import build_sleeve
build_sleeve(mesh,steel)

from grip_geometry import build_grip
grip_parts=build_grip(mesh,prism,parts,composite,check,steel,black,bpy)
grip=grip_parts[0]

# Exterior control cluster: sculpted guard, curved trigger and seated left lever.
from control_geometry import build_controls
from control_material import make_control_steel
control_steel=make_control_steel(steel,contact_mask)
build_controls(mesh,prism,panel,cyl,rr,parts,control_steel,edge,black,bpy)

# Profiled folding frame and distal shoulder shoe; fixed hinge faces stay on Body.
from stock_geometry import build_stock
stock_rubber=mat('Source_StockMouldedRubber',(.016,.018,.019),0,.62,'stock_rubber')
build_stock(mesh,prism,parts,steel,composite,stock_rubber,bpy)
for side in (-1,1):
    cyl('Stock_rear_hinge_'+str(side),(-.012,side*.018,.063),(-.012,side*.024,.063),.0055,edge,n=24,bevel=.00018)
    cyl('Stock_hinge_pin_'+str(side),(-.012,side*.024,.063),(-.012,side*.0245,.063),.0024,steel,n=16,bevel=0)

# Thin sculpted open rear notch: tapered protective wings, rounded inner corners.
yz=[(-.009,.067),(-.0083,.0785),(-.0059,.0785),(-.0044,.0715),(.0044,.0715),(.0059,.0785),(.0083,.0785),(.009,.067)]
n=len(yz);vv=[(x,y,z) for x in (.1135,.1165) for y,z in yz];ff=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
mesh('Rear_sight_sculpted_notch',vv,ff,edge,bevel=.00025)
foot=prism('Rear_sight_foot',[(.103,.0555),(.105,.060),(.112,.0668),(.118,.0668),(.125,.060),(.127,.0555)],.0065,steel,bevel=.0003)
cut=cyl('Sight_saddle_clearance',(.099,0,.0423),(.131,0,.0423),.01425,black,n=20,bevel=0);subtract(foot,cut)

# Flush rear-facing rim. No side screen remains after the ADS design revision.
outer=rr(0,0,.0265,.0205,.003);inner=rr(0,0,.024,.018,.0025)
metal_inner=rr(0,0,.025,.019,.0028)
n=len(outer);vv=[instrument_point(u,v,d) for d in (-.0002,.0006) for loop in (outer,metal_inner) for u,v in loop];ff=[]
for i in range(n):
    j=(i+1)%n;ff += [(i,j,n+j,n+i),(2*n+i,3*n+i,3*n+j,2*n+j),(i,2*n+i,2*n+j,j),(n+i,n+j,3*n+j,3*n+i)]
rim=mesh('Counter_recess_rim',vv,ff,edge,bevel=.00008)
rim.modifiers[-1].segments=1
# A separate dark gasket sits between the thin metal ledge and unchanged glass.
gasket_outer=rr(0,0,.02485,.01885,.0027)
gv=[instrument_point(u,v,d) for d in (-.00015,.00018) for loop in (gasket_outer,inner) for u,v in loop]
gasket=mesh('Counter_sealing_gasket',gv,ff,black,bevel=0)
gasket['design_note']='Continuous dark sealing strip; 75 micron reveal to bezel, glass UV and position unchanged'
counter=mesh('Counter_face',[instrument_point(u,v,.00025) for u,v in inner],[tuple(range(n))],black,'Counter')
bm=bmesh.new();bm.from_mesh(counter.data)
bmesh.ops.reverse_faces(bm,faces=[f for f in bm.faces if f.normal.dot(counter_normal)<0])
bm.to_mesh(counter.data);bm.free();counter.data.update()
uv=counter.data.uv_layers.new(name='UVMap')
for p in counter.data.polygons:
    for li in p.loop_indices:
        pos=counter.data.vertices[counter.data.loops[li].vertex_index].co-counter_origin
        uv.data[li].uv=(.5+pos.dot(counter_right)/.024,.5+pos.dot(counter_up)/.018)

# Source surface finish shared by every metal/composite part; baked to two game materials.
from production_finish import assign_finishes
source_finishes=assign_finishes(parts,counter,ROOT,contact_mask)

# Apply dimensions, keep all authored pieces separate with common game-space origin.
for o in parts:
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    if o!=counter:
        for mod in list(o.modifiers):bpy.ops.object.modifier_apply(modifier=mod.name)
    # Split only hard boundaries, retaining smooth cylinder bands.
    refined=o.get('control_custom_normals',False) or o.get('grip_custom_normals',False) or o.get('joinery_custom_normals',False) or refine_contact_normals(o)
    if not refined and not o.get('stock_custom_normals') and o not in (fore,counter) and o.name!='Barrel_perforated_sleeve':
        for p in o.data.polygons:p.use_smooth=len(p.vertices)<=4
        mod=o.modifiers.new('Weighted corner normals','WEIGHTED_NORMAL');mod.keep_sharp=True;mod.weight=40
    o.color=o.data.materials[0].diffuse_color
for name,loc,parent in [('Muzzle',(.546,0,.0075),'Action'),('Grip_R',(-.077,0,-.060),'Body'),('Grip_L',(.200,0,.005),'Action'),('Counter_Center',tuple(counter_origin),'Body'),('Stock_Hinge',(-.012,0,.063),'Body'),('LoadingLid_Hinge',(.105,0,.055),'Body'),('Shell_Insert',(.062,0,.065),'Body'),('Eject_Art',(.025,0,-.014),'Body')]:
    o=bpy.data.objects.new(name,None);s.collection.objects.link(o);o.location=loc;o.empty_display_size=.012;o['assembly']=parent

# Presentation setup. World lighting is deliberately restrained; materials carry the surface.
s.world=bpy.data.worlds.new('Studio_world');s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs[0].default_value=(.18,.20,.19,1);s.world.node_tree.nodes['Background'].inputs[1].default_value=.32
def area(name,loc,power,size,col=(1,1,1)):
    d=bpy.data.lights.new(name,'AREA');d.energy=power;d.shape='DISK';d.size=size;d.color=col;o=bpy.data.objects.new(name,d);s.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector((.20,0,0))-o.location).to_track_quat('-Z','Y').to_euler()
area('Key',(.15,.45,.65),11,.7,(1,.95,.88));area('Fill',(.05,-.4,.25),6,.7,(.75,.85,1));area('Soft_front',(.3,.7,-.15),3,.9);area('Rear_rim',(-.45,.15,.25),3,.4)
cam=bpy.data.objects.new('Camera_Concept',bpy.data.cameras.new('Camera_Concept'));s.collection.objects.link(cam);s.camera=cam
cam.location=(.218,1,.024);target=Vector((.218,0,-.010));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=.693
s.render.engine='CYCLES';s.cycles.samples=32;s.cycles.use_denoising=True
try:
    prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
    for d in prefs.devices:d.use=d.type!='CPU'
    s.cycles.device='GPU'
except Exception:pass
s.render.resolution_x=1672;s.render.resolution_y=941;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG';s.render.image_settings.color_mode='RGB'
s.view_settings.view_transform='AgX';s.view_settings.look='AgX - Medium High Contrast'
# A backdrop is presentation-only and excluded from game exports.
bg=bpy.data.materials.new('Backdrop');bg.use_nodes=True;p=bg.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(.052,.060,.054,1);p.inputs['Roughness'].default_value=1
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,-.15,0),rotation=(math.pi/2,0,0));back=bpy.context.object;back.name='Studio_Backdrop';back.data.materials.append(bg);back['presentation']=True
for ob in parts:
    assert len(ob.data.polygons)>0 and all(math.isfinite(c) for v in ob.data.vertices for c in v.co),ob.name
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'source/Jam_RMB93_Editable_v02.blend'))
if '--skip-renders' not in sys.argv:
    s.render.filepath=str(ROOT/'renders/01_geometry.png');bpy.ops.render.render(write_still=True)
print('GEOMETRY_COMPLETE',len(parts),flush=True)
