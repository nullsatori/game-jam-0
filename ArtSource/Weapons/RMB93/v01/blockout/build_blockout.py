"""Exterior art blockout and consistent cameras, not a production weapon asset.

Run in Blender background mode. The source uses Blender +Y as anatomical LEFT;
the future Unreal export must convert that to UE -Y and verify handedness.
No gameplay, FBX or UV/PBR texture delivery is performed by this script.
"""
from pathlib import Path
import json
import math
import bpy
import bmesh
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[5]
OUT = ROOT / "Docs/Art/RMB93/v01/Renders"
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.preferences.filepaths.save_version = 0
scene = bpy.context.scene
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 1.0


def material(name, color, metallic=0, roughness=.5, emission=0):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    p = m.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value = (*color, 1)
    p.inputs["Metallic"].default_value = metallic
    p.inputs["Roughness"].default_value = roughness
    p.inputs["Emission Color"].default_value = (*color, 1)
    p.inputs["Emission Strength"].default_value = emission
    return m


STEEL = material("Preview_Steel", (.032, .040, .042), .85, .52)
EDGE = material("Preview_BlackenedPins", (.026, .030, .031), .85, .42)
POLY = material("Preview_Polymer", (.014, .017, .017), 0, .68)
BURG = material("Preview_Oxblood", (.061, .013, .010), 0, .65)
RUBBER = material("Preview_Rubber", (.009, .011, .012), 0, .88)
GLASS = material("Preview_OpaqueSmokedGlass", (.009, .007, .005), 0, .22)
BORE = material("Preview_DarkRecess", (.004, .005, .005), 0, .95)
GHOST = material("Preview_InactiveCathode", (.040, .023, .012), .65, .54)
MESH_WIRE = material("Preview_AnodeMesh", (.019, .018, .014), .7, .55)
AMBER = material("Preview_AmberCore", (1.0, .32, .055), 0, .3, 5.0)


def gas_material(name, color, strength, opacity):
    """Soft transparent gas envelope around a 3D cathode; preview only."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nodes, links = m.node_tree.nodes, m.node_tree.links
    nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    mix = nodes.new("ShaderNodeMixShader")
    transparent = nodes.new("ShaderNodeBsdfTransparent")
    emission = nodes.new("ShaderNodeEmission")
    emission.inputs[0].default_value = (*color, 1)
    emission.inputs[1].default_value = strength
    facing = nodes.new("ShaderNodeLayerWeight")
    falloff = nodes.new("ShaderNodeMath")
    falloff.operation = "SUBTRACT"
    falloff.inputs[0].default_value = 1
    links.new(facing.outputs["Facing"], falloff.inputs[1])
    gain = nodes.new("ShaderNodeMath")
    gain.operation = "MULTIPLY"
    gain.inputs[1].default_value = opacity
    links.new(falloff.outputs[0], gain.inputs[0])
    links.new(gain.outputs[0], mix.inputs[0])
    links.new(transparent.outputs[0], mix.inputs[1])
    links.new(emission.outputs[0], mix.inputs[2])
    links.new(mix.outputs[0], out.inputs["Surface"])
    return m


CORONA = gas_material("Preview_OrangeDischarge", (1.0,.10,.006), 2.5, .65)
HALO = gas_material("Preview_RedAmberHalo", (1.0,.045,.003), 1.6, .12)

# Large restrained roughness variation, not a baked production texture set.
noise = STEEL.node_tree.nodes.new("ShaderNodeTexNoise")
noise.inputs["Scale"].default_value = 90
noise.inputs["Detail"].default_value = 1.0
ramp = STEEL.node_tree.nodes.new("ShaderNodeValToRGB")
ramp.color_ramp.elements[0].color = (.43, .43, .43, 1)
ramp.color_ramp.elements[1].color = (.60, .60, .60, 1)
STEEL.node_tree.links.new(noise.outputs["Fac"], ramp.inputs[0])
STEEL.node_tree.links.new(ramp.outputs["Color"], STEEL.node_tree.nodes["Principled BSDF"].inputs["Roughness"])

parts = {}


def assign(ob, mat, group="Body", bevel=0):
    ob.data.materials.append(mat)
    parts.setdefault(group, []).append(ob)
    if bevel:
        mod = ob.modifiers.new("Silhouette bevel", "BEVEL")
        mod.width, mod.segments = bevel, 1
        mod.affect = "EDGES"
    return ob


def mesh(name, vertices, faces, mat, group="Body", bevel=0):
    data = bpy.data.meshes.new(name)
    data.from_pydata(vertices, [], faces)
    data.update()
    ob = bpy.data.objects.new(name, data)
    scene.collection.objects.link(ob)
    return assign(ob, mat, group, bevel)


def box(name, center, size, mat, group="Body", bevel=.0007):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    ob = bpy.context.object
    ob.name = name
    ob.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return assign(ob, mat, group, bevel)


def prism(name, xz, halfwidth, mat, group="Body", bevel=.0008):
    v = [(x, y, z) for y in (-halfwidth, halfwidth) for x, z in xz]
    n = len(xz)
    f = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))]
    f += [(i, (i + 1) % n, (i + 1) % n + n, i + n) for i in range(n)]
    return mesh(name, v, f, mat, group, bevel)


def cylinder(name, start, end, radius, mat, group="Body", sides=12):
    a, b = Vector(start), Vector(end)
    bpy.ops.mesh.primitive_cylinder_add(vertices=sides, radius=radius,
                                      depth=(b-a).length, location=(a+b)/2)
    ob = bpy.context.object
    ob.name = name
    ob.rotation_euler = (b-a).to_track_quat("Z", "Y").to_euler()
    return assign(ob, mat, group, .00035)


def wire(name, points, radius, mat, group="Body", cyclic=False):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.resolution_u = 1
    cu.bevel_depth = radius
    cu.bevel_resolution = 3 if group == "DisplayPreview" else 1
    cu.use_fill_caps = True
    spline = cu.splines.new("POLY")
    spline.points.add(len(points)-1)
    for p, co in zip(spline.points, points):
        p.co = (*co, 1)
    spline.use_cyclic_u = cyclic
    ob = bpy.data.objects.new(name, cu)
    scene.collection.objects.link(ob)
    return assign(ob, mat, group)


def rounded_rect(cx, cz, width, height, radius, steps=4):
    result = []
    for x, z, start in [(cx+width/2-radius, cz+height/2-radius, 0),
                        (cx-width/2+radius, cz+height/2-radius, 90),
                        (cx-width/2+radius, cz-height/2+radius, 180),
                        (cx+width/2-radius, cz-height/2+radius, 270)]:
        for i in range(steps+1):
            a = math.radians(start+i*90/steps)
            result.append((x+math.cos(a)*radius, z+math.sin(a)*radius))
    return result


def panel(name, cx, cz, width, height, y, mat, group="Body"):
    outline = rounded_rect(cx, cz, width, height, min(width, height)*.16)
    return mesh(name, [(x,y,z) for x,z in outline],
                [tuple(range(len(outline)))], mat, group)


receiver = prism("Receiver", [(-.083,.0146),(-.046,.0349),(-.008,.0586),
                             (.124,.0551),(.154,.0433),(.123,.0265),
                             (.053,.0104),(-.034,.002)], .017, STEEL)
# A real shallow recess in the left wall, not a separate bolted box.
CX, CZ, FACE = .030, .036, .017
cut = box("Temporary_window_cut", (CX,.017,CZ), (.037,.018,.027), GLASS, "Temporary", .003)
bpy.context.view_layer.objects.active = cut
for mod in list(cut.modifiers):
    bpy.ops.object.modifier_apply(modifier=mod.name)
cutmod = receiver.modifiers.new("Counter recess", "BOOLEAN")
cutmod.operation = "DIFFERENCE"
cutmod.object = cut
bpy.context.view_layer.objects.active = receiver
bpy.ops.object.modifier_apply(modifier=cutmod.name)
bpy.data.objects.remove(cut, do_unlink=True)
parts.pop("Temporary", None)

cylinder("Upper_magazine_closed", (.105,0,.041), (.545,0,.041), .0125, STEEL)
cylinder("Upper_end_cap", (.539,0,.041), (.547,0,.041), .0128, EDGE)

# Lower barrel exterior with a shallow black visible opening. No internals.
cylinder("Lower_barrel", (.035,0,.012), (.554,0,.012), .0107, STEEL, "Action")
n = 12
verts, faces = [], []
for x, radius in [(.548,.0128), (.565,.0128), (.565,.0094), (.551,.0094)]:
    for i in range(n):
        a = 2*math.pi*i/n
        verts.append((x, math.sin(a)*radius, .012+math.cos(a)*radius))
for row in range(3):
    for i in range(n):
        j = (i+1)%n
        faces.append((row*n+i, row*n+j, (row+1)*n+j, (row+1)*n+i))
mesh("Lower_muzzle_rim", verts, faces, EDGE, "Action")
cylinder("Lower_muzzle_dark_inside", (.549,0,.012), (.551,0,.012), .0094, BORE, "Action")

# Faceted fore-end with broad grooves cut into the silhouette.
sections = [(.059,.90), (.065,1.0), (.113,1.0)]
for i in range(11):
    x = .120+i*.016
    sections += [(x,1.0), (x+.003,.92), (x+.007,.92), (x+.010,1.0)]
sections += [(.300,1.0), (.308,.94)]
sections = sorted(set(sections))
verts, faces = [], []
for x, scale in sections:
    for i in range(12):
        a = 2*math.pi*i/12
        s, c = math.sin(a), math.cos(a)
        y = .020*math.copysign(abs(s)**.75, s)*scale
        z = .010 + .017*math.copysign(abs(c)**.75, c)*scale
        verts.append((x,y,z))
for j in range(len(sections)-1):
    for i in range(12):
        faces.append((j*12+i,j*12+(i+1)%12,(j+1)*12+(i+1)%12,(j+1)*12+i))
faces += [tuple(range(11,-1,-1)), tuple(range((len(sections)-1)*12,len(sections)*12))]
mesh("Foreend_ribbed", verts, faces, POLY, "Action")
for side in [-1,1]:
    box("Foreend_long_insert_"+str(side), (.207,side*.0187,.020), (.168,.0012,.0035), BURG, "Action", .0007)
    panel("Foreend_rear_pad_"+str(side), .091,.010,.046,.017,side*.0204,POLY,"Action")

prism("Pistol_grip", [(-.047,.011),(-.030,.001),(-.047,-.026),(-.052,-.084),
                      (-.059,-.097),(-.100,-.079),(-.099,-.062),(-.077,.003)],
      .016, POLY, bevel=.0014)
for side in [-1,1]:
    accent = prism("Grip_oxblood_inlay_"+str(side),
                   [(-.067,-.021),(-.050,-.026),(-.059,-.082),(-.087,-.071)],
                   .0004, BURG, bevel=.0004)
    accent.location.y = side*.0166
    # Restrained large grip scores: a modelling proxy, no texture noise.
    for j in range(9):
        z = -.030-j*.005
        x = -.062 + (z+.030)*.20
        wire("Grip_score", [(x-.008,side*.0172,z),(x+.006,side*.0172,z-.002)], .00022, RUBBER)

wire("Trigger_guard", [(-.030,0,.004),(.022,0,.004),(.026,0,-.008),
                       (.023,0,-.022),(.015,0,-.029),(-.019,0,-.029),(-.030,0,-.023)],
     .0020, EDGE, cyclic=True)
wire("Trigger", [(.003,0,.004),(-.003,0,-.007),(-.001,0,-.018),(.005,0,-.021)], .0015, EDGE)

for side in [-1,1]:
    box("Folded_stock_rail_"+str(side), (.187,side*.014,.060), (.388,.0045,.007), STEEL, bevel=.0006)
    cylinder("Rear_stock_pin_"+str(side),(-.006,side*.017,.056),(-.006,side*.020,.056),.0033,EDGE)
    cylinder("Lower_receiver_pin_"+str(side),(.004,side*.017,.025),(.004,side*.019,.025),.0025,EDGE)
    end = prism("Folded_stock_toe_"+str(side),[(.372,.055),(.453,.055),(.458,.063),(.449,.063),(.380,.061)],.0023,STEEL)
    end.location.y = side*.014
box("Folded_stock_front_bridge",(.377,0,.058),(.011,.032,.008),STEEL)
box("Front_sight_low",(.529,0,.059),(.014,.007,.008),EDGE)
for side in [-1,1]:
    box("Rear_sight_notch_"+str(side),(.111,side*.005,.066),(.008,.0025,.005),EDGE,bevel=.0002)
# The selected LEFT side is Blender +Y. Deliberately no mirrored right lever.
lever = prism("Left_control_lever",[(-.014,.017),(.044,.018),(.045,.011),(-.013,.010)],.0012,EDGE,bevel=.0007)
lever.location.y = .0185

# Display lip and aperture, integrated into the receiver recess.
outline = rounded_rect(CX,CZ,.037,.027,.004)
wire("Counter_oxblood_gasket", [(x,.0175,z) for x,z in outline], .00055, BURG, cyclic=True)
outline2 = rounded_rect(CX,CZ,.039,.029,.0045)
wire("Counter_rolled_lip", [(x,.0173,z) for x,z in outline2], .0005, EDGE, cyclic=True)
panel("Counter_surface",CX,CZ,.036,.026,.009,GLASS,"Counter")
# Recessed cathodes and anode mesh establish the old instrument-clock character.
for i in range(-8,9):
    x = CX+i*.0016
    wire("Anode_vertical",[(x,.0098,CZ-.011),(x,.0098,CZ+.011)],.000035,MESH_WIRE,"DisplayPreview")
for i in range(-6,7):
    z = CZ+i*.0016
    wire("Anode_horizontal",[(CX-.014,.0098,z),(CX+.014,.0098,z)],.000035,MESH_WIRE,"DisplayPreview")
for x in [CX-.0115,CX+.0115]:
    cylinder("Cathode_support",(x,.010,CZ-.0105),(x,.010,CZ+.0105),.00016,GHOST,"DisplayPreview",8)
# A subtle bowed reflection is a baked-faux-glass reference, not another UE slot.
GLINT = material("Preview_GlassEdgeReflection",(.09,.115,.12),.15,.27)
wire("Glass_edge_glint",[(CX+.0148,.016,CZ-.007),(CX+.0157,.0166,CZ),
                        (CX+.0148,.016,CZ+.008),(CX+.012,.0158,CZ+.010)],.00013,GLINT,"DisplayPreview")

DIGITS = {
 "0":[[(.3,.12),(.67,.12),(.8,.28),(.8,.74),(.65,.9),(.31,.9),(.2,.73),(.2,.27),(.3,.12)]],
 "1":[[(.34,.29),(.57,.12),(.57,.90)],[(.33,.90),(.78,.90)]],
 "2":[[(.22,.27),(.34,.12),(.66,.12),(.8,.27),(.8,.41),(.2,.90),(.82,.90)]],
 "3":[[(.22,.13),(.65,.13),(.8,.26),(.8,.38),(.61,.50),(.37,.50)],
      [(.61,.50),(.8,.62),(.8,.77),(.65,.90),(.22,.90)]],
 "4":[[(.70,.11),(.19,.61),(.86,.61)],[(.70,.11),(.70,.91)]],
 "5":[[(.8,.12),(.25,.12),(.22,.49),(.66,.49),(.8,.63),(.8,.76),(.66,.90),(.25,.90),(.18,.80)]],
 "6":[[(.75,.14),(.48,.12),(.23,.37),(.23,.75),(.35,.9),(.66,.9),(.8,.76),(.8,.58),(.64,.47),(.24,.47)]],
 "7":[[(.19,.12),(.84,.12),(.41,.90)]],
 "8":[[(.35,.12),(.65,.12),(.8,.27),(.8,.37),(.2,.65),(.2,.76),(.35,.90),(.65,.90),(.8,.76),(.8,.65),(.2,.37),(.2,.27),(.35,.12)]],
 "9":[[(.77,.52),(.35,.52),(.21,.39),(.21,.26),(.35,.12),(.65,.12),(.78,.28),(.78,.66),(.52,.90),(.26,.88)]],
}


def soften(path, amount=.045):
    """Round bends like bent wire, keeping the chosen glyph's silhouette."""
    result = [path[0]]
    for a,b,c in zip(path,path[1:],path[2:]):
        a,b,c = Vector(a),Vector(b),Vector(c)
        r = min(amount,(a-b).length*.2,(c-b).length*.2)
        p,q = b+(a-b).normalized()*r, b+(c-b).normalized()*r
        for j in range(5):
            t=j/4
            result.append(tuple((1-t)**2*p+2*(1-t)*t*b+t*t*q))
    result.append(path[-1])
    return result


def digit_paths(digit, active=True, depth=.011):
    obs = []
    for path in DIGITS.get(digit,[]):
        pts = [(CX+(.5-u)*.019, .0145 if active else depth, CZ+(.5-v)*.023) for u,v in soften(path)]
        obs.append(wire(("Active_" if active else "Ghost_")+digit,pts,
                        .00013 if active else .000065, AMBER if active else GHOST, "DisplayPreview"))
        if active:
            obs.append(wire("Discharge_"+digit,pts,.00032,CORONA,"DisplayPreview"))
            obs.append(wire("Halo_"+digit,pts,.00090,HALO,"DisplayPreview"))
    return obs


for i,digit in enumerate(["0","2","5","8"]):
    digit_paths(digit,False,.0106+i*.00065)
active = digit_paths("4")

# Assembly collections keep moving groups inspectable in the source scene.
for ob in list(scene.objects):
    if ob.type == "MESH":
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(ob.data)
        bm.free()
for group, objects in parts.items():
    coll = bpy.data.collections.new(group)
    scene.collection.children.link(coll)
    for ob in objects:
        for old in list(ob.users_collection):
            old.objects.unlink(ob)
        coll.objects.link(ob)

scene.world = bpy.data.worlds.new("Studio")
scene.world.color = (.35,.35,.35)
scene.world.use_nodes = True
scene.world.node_tree.nodes["Background"].inputs["Color"].default_value=(.6,.65,.7,1)
scene.world.node_tree.nodes["Background"].inputs["Strength"].default_value=.08


def area(name, location, energy, size, color=(1,1,1)):
    data = bpy.data.lights.new(name,"AREA")
    data.energy, data.shape, data.size, data.color = energy, "DISK", size, color
    ob = bpy.data.objects.new(name,data)
    scene.collection.objects.link(ob)
    ob.location=location
    ob.rotation_euler=(Vector((.15,0,.02))-ob.location).to_track_quat("-Z","Y").to_euler()


area("Key",(.15,.45,.70),9,.7,(1,.92,.82))
area("Fill",(.05,-.45,.35),5,.8,(.76,.86,1))
area("Edge",(.65,.15,.45),7,.6)
area("UnderFill",(.10,.05,-.65),4,1.0,(.86,.92,1))
area("RearFill",(-.55,0,.16),3,.7,(.92,.95,1))
scene.render.engine="CYCLES"
scene.cycles.samples=32
scene.cycles.use_denoising=True
try:
    prefs=bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type="OPTIX"
    prefs.get_devices()
    gpu=False
    for d in prefs.devices:
        d.use=d.type!="CPU"
        gpu |= d.use
    if gpu:
        scene.cycles.device="GPU"
except Exception:
    pass
scene.render.image_settings.file_format="PNG"
scene.render.image_settings.color_mode="RGBA"
scene.render.film_transparent=True
scene.render.resolution_percentage=100
scene.view_settings.view_transform="AgX"
scene.view_settings.look="AgX - Medium High Contrast"
scene.render.image_settings.color_depth="8"

# Optical diffusion supplements the local gas envelope; the glyph remains
# readable without this presentation bloom. Keep original background alpha.
comp = bpy.data.node_groups.new("Counter_optical_diffusion", "CompositorNodeTree")
comp.interface.new_socket(name="Image",in_out="OUTPUT",socket_type="NodeSocketColor")
layer = comp.nodes.new("CompositorNodeRLayers")
glare = comp.nodes.new("CompositorNodeGlare")
glare.inputs["Type"].default_value = "Fog Glow"
glare.inputs["Threshold"].default_value = 1.0
glare.inputs["Strength"].default_value = .3
glare.inputs["Size"].default_value = .25
alpha = comp.nodes.new("CompositorNodeSetAlpha")
output = comp.nodes.new("NodeGroupOutput")
comp.links.new(layer.outputs["Image"],glare.inputs["Image"])
comp.links.new(glare.outputs["Image"],alpha.inputs["Image"])
comp.links.new(layer.outputs["Alpha"],alpha.inputs["Alpha"])
comp.links.new(alpha.outputs["Image"],output.inputs["Image"])
scene.compositing_node_group = comp

camera_data=bpy.data.cameras.new("Modeller_camera")
camera=bpy.data.objects.new("Modeller_camera",camera_data)
scene.collection.objects.link(camera)
scene.camera=camera


def render(name, location, target, scale=.80, size=(1800,900), ortho=True):
    camera.location=location
    camera.rotation_euler=(Vector(target)-camera.location).to_track_quat("-Z","Y").to_euler()
    camera_data.type="ORTHO" if ortho else "PERSP"
    camera_data.ortho_scale=scale
    camera_data.lens=60
    scene.render.resolution_x,scene.render.resolution_y=size
    scene.render.filepath=str(OUT/(name+".png"))
    bpy.ops.render.render(write_still=True)
    print("ART_RENDER", name, flush=True)


center=(.232,0,-.014)
views=[
 ("01_left",(.232,1,-.014),center,.75,(2000,650)),
 ("02_right",(.232,-1,-.014),center,.75,(2000,650)),
 ("03_top",(.232,0,1),(.232,0,0),.75,(2000,580)),
 ("04_bottom",(.232,0,-1),(.232,0,0),.75,(2000,580)),
 ("05_front",(1,0,-.014),(.15,0,-.014),.22,(900,1100)),
 ("06_rear",(-1,0,-.014),(.15,0,-.014),.22,(900,1100)),
 ("07_front_left",(.9,1,.45),center,.80,(1800,1000)),
 ("08_rear_left",(-.7,.95,.35),center,.80,(1800,1000)),
 ("09_front_right",(.9,-1,.45),center,.80,(1800,1000)),
 ("10_rear_right",(-.7,-.95,.35),center,.80,(1800,1000)),
 ("11_counter_context",(-.08,.55,.13),(CX,0,CZ),.18,(1800,1100)),
]

# Save a source scene before render so failure never loses the blockout.
source=Path(__file__).with_name("Jam_RMB93_Blockout_v01.blend")
bpy.ops.wm.save_as_mainfile(filepath=str(source))
for args in views:
    render(*args)
for state in ["0","4","6","OFF"]:
    for ob in active:
        bpy.data.objects.remove(ob,do_unlink=True)
    active=digit_paths(state)
    render("12_counter_"+state,(CX,.45,CZ),(CX,0,CZ),.049,(1100,800))
for ob in active:
    bpy.data.objects.remove(ob,do_unlink=True)
active=digit_paths("4")
camera.location=(-.08,.55,.13)
camera.rotation_euler=(Vector((CX,0,CZ))-camera.location).to_track_quat("-Z","Y").to_euler()
camera_data.ortho_scale=.18
bpy.ops.wm.save_as_mainfile(filepath=str(source))

deps=bpy.context.evaluated_depsgraph_get()
triangles=0
for ob in scene.objects:
    if ob.type in {"MESH","CURVE"}:
        evaluated=ob.evaluated_get(deps)
        me=evaluated.to_mesh()
        me.calc_loop_triangles()
        triangles+=len(me.loop_triangles)
        evaluated.to_mesh_clear()
manifest={"status":"exterior_reference_blockout_not_production_asset",
          "source_blender_axes":{"forward":"+X","left":"+Y","up":"+Z"},
          "target_unreal_axes":{"forward":"+X","left":"-Y","up":"+Z"},
          "production_export_verified":False,"uv_textures_delivered":False,
          "triangles_in_preview_scene":triangles,
          "revision":"02_proportions_nixie",
          "reference_profile":"References/RMB93_Proportions_User_Reference.png",
          "folded_length_to_height_target":4.0,
          "display_preview":"layered_cathodes_anode_mesh_orange_corona",
          "views":[p.name for p in sorted(OUT.glob("*.png"))],
          "source":str(source.relative_to(ROOT)).replace("\\","/")}
(OUT/"blockout_manifest.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding="utf-8")
print("ART_COMPLETE",json.dumps(manifest),flush=True)
