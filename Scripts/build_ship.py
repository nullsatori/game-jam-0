"""Builds /Game/Jam/Maps/Lvl_Jam_Ship, the grey blockout of «Почини и улети» (editor Python, headless).

Run (delete Content/Jam/Maps/Lvl_Jam_Ship.umap first; the editor stays open after
a plain -ExecutePythonScript, so wrap it in a script that ends with
unreal.SystemLibrary.quit_editor()):
  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/build_ship.py" -unattended -nop4 -nosplash -nullrhi

Everything is in centimetres, the ship along +X with its centre at the origin.
Layout, sizes and tags: Docs/Ship-Blockout.md. Seeded: the same map every run.
"""
import math
import random

import unreal

MAP = "/Game/Jam/Maps/Lvl_Jam_Ship"
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
CYL = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
SPHERE = unreal.load_asset("/Engine/BasicShapes/Sphere.Sphere")
DRUM = unreal.load_asset("/Game/MindKore/Art/Admin/Yard/Props2/SM_MK_Yard_Drum")
DOOR = unreal.load_asset("/Game/MindKore/Art/Admin/Doors/SM_MK_Door_Wood")

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
MATS = "/Game/Jam/Materials"
EXPOSURE_BIAS = 0.0  # tuned on rendered frames (jam.Ship.Shots)
rng = random.Random(11)


def log(msg):
    unreal.log("JAMSHIP: " + msg)


def V(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def R(yaw=0.0, pitch=0.0, roll=0.0):
    return unreal.Rotator(roll=float(roll), pitch=float(pitch), yaw=float(yaw))


def norm(v):
    length = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)
    return (v[0] / length, v[1] / length, v[2] / length)


def spawn(cls, loc, rot=None, label=None):
    actor = eas.spawn_actor_from_class(cls, loc, rot or R())
    if actor is None:
        raise RuntimeError("spawn failed: %s" % cls)
    if label:
        actor.set_actor_label(label)
    return actor


def block(label, centre, size, rot=None, mesh=None, material=None, shadow=True, collide=True):
    """A static engine shape (100 cm) stretched to size (cm), turned by rot."""
    actor = eas.spawn_actor_from_object(mesh or CUBE, centre, rot or R())
    actor.set_actor_label(label)
    actor.set_actor_scale3d(V(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    comp = actor.get_editor_property("static_mesh_component")
    if material:
        comp.set_material(0, material)
    if not shadow:
        comp.set_editor_property("cast_shadow", False)
    if not collide:
        comp.set_collision_profile_name("NoCollision")
    return actor


def tint(name, rgb):
    """A colour instance of BasicShapeMaterial; made once, its colour rewritten every build."""
    path = MATS + "/" + name
    if eal.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = tools.create_asset(name, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial"))
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    eal.save_asset(path, False)
    log("made " + path)
    return mi


def emissive_base(name, two_sided):
    """An unlit material: emissive = Color x Intensity (vector and scalar parameters), made once."""
    path = MATS + "/" + name
    if eal.does_asset_exist(path):
        return unreal.load_asset(path)
    m = tools.create_asset(name, MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", two_sided)
    colour = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -500, 0)
    colour.set_editor_property("parameter_name", "Color")
    colour.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    strength = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, 200)
    strength.set_editor_property("parameter_name", "Intensity")
    strength.set_editor_property("default_value", 1.0)
    times = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 100)
    mel.connect_material_expressions(colour, "", times, "A")
    mel.connect_material_expressions(strength, "", times, "B")
    mel.connect_material_property(times, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_asset(path, False)
    log("made " + path)
    return m


def glow(name, base, rgb, intensity):
    """An emissive instance; made once, its colour and intensity rewritten every build."""
    path = MATS + "/" + name
    if eal.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = tools.create_asset(name, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, base)
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Intensity", intensity)
    eal.save_asset(path, False)
    log("made " + path)
    return mi


def point_light(label, loc, rgb, candela, radius, shadows=True):
    light = spawn(unreal.PointLight, loc, None, label)
    comp = light.get_editor_property("point_light_component")
    comp.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    comp.set_editor_property("intensity", candela)
    comp.set_editor_property("attenuation_radius", radius)
    comp.set_editor_property("light_color", unreal.Color(r=int(rgb[0] * 255), g=int(rgb[1] * 255), b=int(rgb[2] * 255), a=255))
    comp.set_editor_property("cast_shadows", shadows)
    return light


def label_text(text, loc, yaw, size=60.0):
    actor = spawn(unreal.TextRenderActor, loc, R(yaw), "Label_" + text.split()[0])
    comp = actor.get_editor_property("text_render")
    comp.set_editor_property("text", unreal.Text(text))
    comp.set_editor_property("world_size", size)
    comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    comp.set_editor_property("text_render_color", unreal.Color(r=230, g=230, b=200, a=255))
    return actor


def zero_damping(component):
    body = component.get_editor_property("body_instance")
    body.set_editor_property("linear_damping", 0.0)
    body.set_editor_property("angular_damping", 0.0)
    component.set_editor_property("body_instance", body)


def prop(label, loc, mesh, size, mass, material, tags=(), cls=None, rot=None):
    """A loose AMKPhysicsProp (or subclass) of an engine shape of size (cm)."""
    p = spawn(cls or unreal.MKPhysicsProp, loc, rot, label)
    body = p.get_editor_property("body")
    body.modify()
    if mesh is not None:
        body.set_static_mesh(mesh)
    if size is not None:
        p.set_actor_scale3d(V(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    if material:
        body.set_material(0, material)
    p.set_editor_property("mass_kg", float(mass))
    if tags:
        p.set_editor_property("tags", list(tags))
    zero_damping(body)
    return p


def set_extent(actor, prop_name, extent):
    actor.get_editor_property(prop_name).set_box_extent(V(*extent))


# --- fresh, non-partitioned level -----------------------------------------------
if eal.does_asset_exist(MAP):
    raise RuntimeError(MAP + " exists: delete the .umap first (a failed new_level would build into another map)")
if not les.new_level(MAP, False):
    raise RuntimeError("new_level failed for " + MAP)
world = ues.get_editor_world()
if world.get_outer().get_path_name() != MAP:
    raise RuntimeError("editor world is %s, not %s: refusing to spawn" % (world.get_outer().get_path_name(), MAP))
log("new level " + MAP)

settings = world.get_world_settings()
settings.modify()
settings.set_editor_property("global_gravity_set", True)
settings.set_editor_property("global_gravity_z", 0.0)
settings.set_editor_property("default_game_mode", unreal.JamShipGameMode.static_class())

# Role colours (the zero-G map's, plus new ones).
HULL = tint("MI_Jam_Hull", (0.38, 0.4, 0.43))
ANCHOR = tint("MI_Jam_Anchor", (0.45, 0.46, 0.48))
JUNK = tint("MI_Jam_Junk", (0.32, 0.27, 0.22))
PART = tint("MI_Jam_ShipPart", (0.95, 0.42, 0.05))
PLATE = tint("MI_Jam_HullPlate", (0.55, 0.65, 0.75))
FUEL = tint("MI_Jam_Fuel", (0.95, 0.8, 0.1))
OXYGEN = tint("MI_Jam_O2", (0.1, 0.35, 0.95))
DOCK = tint("MI_Jam_Dock", (0.2, 0.9, 0.3))
ROCK = tint("MI_Jam_Rock", (0.33, 0.31, 0.28))
FLESH = tint("MI_Jam_Flesh", (0.45, 0.05, 0.08))  # dark saturated red-pink: stays fleshy under the teal key
EMISSIVE = emissive_base("M_Jam_Emissive", False)
EMISSIVE_2S = emissive_base("M_Jam_Emissive2S", True)
STAR = glow("MI_Jam_Star", EMISSIVE, (0.1, 0.75, 1.0), 12.0)  # teal like its light; still blooms
STARS = glow("MI_Jam_Stars", EMISSIVE, (0.85, 0.9, 1.0), 25.0)
DOME = glow("MI_Jam_SkyDome", EMISSIVE_2S, (0.035, 0.02, 0.07), 1.0)
EYE = glow("MI_Jam_EyeGlow", EMISSIVE, (0.7, 0.03, 0.02), 6.0)
LAMP = glow("MI_Jam_PanelLamp", EMISSIVE, (1.0, 0.02, 0.01), 2.0)
CARGO = tint("MI_Jam_Cargo", (0.9, 0.75, 0.3))
CABIN = tint("MI_Jam_Cabin", (0.55, 0.53, 0.5))

# --- light: an alien star, a rim from the other side, a dim violet sky -----------------------------
# Key: a teal-cyan star - cold and clearly not our Sun, it reads well against the
# warm cabin, the orange parts and the fleshy planetoid. Rim: a weak magenta
# from the opposite side for silhouettes. Sky: a far two-sided dark-violet dome
# and a few hundred tiny emissive stars that the SkyLight captures as a faint
# coloured ambient. Exposure: manual (no physical camera), bias tuned on frames.
KEY_ROT = R(yaw=140.0, pitch=-30.0)
key = spawn(unreal.DirectionalLight, V(0, 0, 3000), KEY_ROT, "Star_Key")
key.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
key_light = key.get_editor_property("light_component")
key_light.set_editor_property("intensity", 5.0)
key_light.set_editor_property("forward_shading_priority", 1)  # the one used for forward shading/translucency
key_light.set_editor_property("light_color", unreal.Color(r=95, g=215, b=255, a=255))
rim = spawn(unreal.DirectionalLight, V(0, 0, 3000), R(yaw=-40.0, pitch=20.0), "Rim")
rim.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
rim_light = rim.get_editor_property("light_component")
rim_light.set_editor_property("intensity", 1.2)
rim_light.set_editor_property("light_color", unreal.Color(r=255, g=110, b=190, a=255))
rim_light.set_editor_property("cast_shadows", False)
rim_light.set_editor_property("forward_shading_priority", 0)
# The star's disc: 2 km away against the light's direction, ~3 degrees across.
toward = key.get_actor_forward_vector()
block("Star_Disc", V(-toward.x * 200000.0, -toward.y * 200000.0, -toward.z * 200000.0), (10000, 10000, 10000), None, SPHERE, STAR,
      shadow=False, collide=False)
block("Sky_Dome", V(0, 0, 0), (600000, 600000, 600000), None, SPHERE, DOME, shadow=False, collide=False)
for j in range(300):
    u, w = rng.uniform(-1.0, 1.0), rng.uniform(0, 2 * math.pi)
    d = (math.sqrt(1 - u * u) * math.cos(w), math.sqrt(1 - u * u) * math.sin(w), u)
    size = rng.uniform(400, 900)
    block("Star_%03d" % j, V(d[0] * 250000, d[1] * 250000, d[2] * 250000), (size, size, size), None, SPHERE, STARS,
          shadow=False, collide=False)
sky = spawn(unreal.SkyLight, V(0, 0, 0), None, "SkyLight")
sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky_light = sky.get_editor_property("light_component")
sky_light.set_editor_property("intensity", 1.0)
sky_light.set_editor_property("lower_hemisphere_is_black", False)
sky_light.set_editor_property("sky_distance_threshold", 100000.0)
ppv = spawn(unreal.PostProcessVolume, V(0, 0, 0), None, "PostProcess_Look")
ppv.set_editor_property("unbound", True)
pp = ppv.get_editor_property("settings")
pp.set_editor_property("override_auto_exposure_method", True)
pp.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
pp.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
pp.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
pp.set_editor_property("override_auto_exposure_bias", True)
pp.set_editor_property("auto_exposure_bias", EXPOSURE_BIAS)
pp.set_editor_property("override_bloom_intensity", True)
pp.set_editor_property("bloom_intensity", 0.8)
pp.set_editor_property("override_vignette_intensity", True)
pp.set_editor_property("vignette_intensity", 0.5)
ppv.set_editor_property("settings", pp)

# --- the hull: an octagonal prism of 250 cm segments ---------------------------------------
APOTHEM, THICK, SEG = 400.0, 20.0, 250.0
SIDE = 2.0 * APOTHEM * math.tan(math.radians(22.5)) + 4.0   # a little overlap at the corners
HOLE = 250.0


def hull(prefix, origin, rotation, x0, count, breaches):
    """Segments along local X from x0. breaches: {(segment, side)}. Returns the breach centres and rotations."""
    xf = unreal.Transform(location=origin, rotation=rotation, scale=V(1, 1, 1))
    holes = []
    for k in range(count):
        xc = x0 + SEG * 0.5 + SEG * k
        for i in range(8):
            theta = math.radians(45.0 * i)
            n = (0.0, math.cos(theta), math.sin(theta))
            t = (0.0, math.sin(theta), -math.cos(theta))
            local_rot = R(roll=45.0 * i - 90.0)
            world_rot = unreal.MathLibrary.compose_rotators(local_rot, rotation)
            centre = (xc, n[1] * APOTHEM, n[2] * APOTHEM)
            if (k, i) in breaches:
                strip = (SIDE - HOLE) * 0.5
                for sign in (-1, 1):
                    off = sign * (HOLE * 0.5 + strip * 0.5)
                    c = V(centre[0], centre[1] + t[1] * off, centre[2] + t[2] * off)
                    block("%s_Seg%02d_Side%d_Edge%+d" % (prefix, k, i, sign), xf.transform_location(c), (SEG, strip, THICK),
                          world_rot, material=HULL)
                holes.append((xf.transform_location(V(*centre)), world_rot))
            else:
                block("%s_Seg%02d_Side%d" % (prefix, k, i), xf.transform_location(V(*centre)), (SEG, SIDE, THICK),
                      world_rot, material=HULL)
    return holes


# Front half: x -1000..2000 (12 segments), three breaches; the break is open at x = -1000.
BREACHES = {(2, 2), (5, 0), (7, 5)}
holes = hull("Hull", V(0, 0, 0), R(), -1000.0, 12, BREACHES)
block("Hull_NoseCap", V(2010, 0, 0), (870, 870, THICK), R(pitch=90.0), CYL, HULL)
for j in range(6):
    # Torn plates hanging off the break.
    a = rng.uniform(0, 360)
    block("Hull_Torn_%d" % j, V(-1000 - rng.uniform(50, 250), math.cos(math.radians(a)) * 380, math.sin(math.radians(a)) * 380),
          (rng.uniform(100, 250), rng.uniform(80, 200), THICK), R(yaw=rng.uniform(-40, 40), pitch=rng.uniform(-40, 40), roll=a),
          material=HULL)

# Rear chunk: 10 m, drifted 30+ m off at an angle; open at its break, an engine stand-in at the far end.
REAR_ORIGIN, REAR_ROT = V(-3500, 1500, 800), R(yaw=25.0, pitch=12.0)
hull("RearChunk", REAR_ORIGIN, REAR_ROT, -500.0, 4, set())
rear = unreal.Transform(location=REAR_ORIGIN, rotation=REAR_ROT, scale=V(1, 1, 1))
block("RearChunk_Cap", rear.transform_location(V(510, 0, 0)), (870, 870, THICK),
      unreal.MathLibrary.compose_rotators(R(pitch=90.0), REAR_ROT), CYL, HULL)
block("RearChunk_Engine", rear.transform_location(V(700, 0, 0)), (300, 300, 350),
      unreal.MathLibrary.compose_rotators(R(pitch=90.0), REAR_ROT), CYL, ANCHOR)
grapple = spawn(unreal.JamToolPickup, rear.transform_location(V(950, 0, 0)), REAR_ROT, "Pickup_Grapple")
grapple.set_actor_scale3d(V(0.6, 0.6, 1.2))
grapple.get_editor_property("mesh").set_material(0, PART)
label_text("GRAPPLE", rear.transform_location(V(950, 0, 120)), 0, 40)

# --- the cabin (safe zone, gravity) and the airlock --------------------------------------------
FLOOR = -150.0
block("Cabin_Floor", V(1600, 0, FLOOR - 10), (800, 500, 20), material=CABIN)        # x 1200..2000 (airlock too)
block("Cabin_Ceiling", V(1700, 0, 160), (600, 500, 20), material=CABIN)
block("Cabin_WallL", V(1700, -260, 0), (600, 20, 300), material=CABIN)
block("Cabin_WallR", V(1700, 260, 0), (600, 20, 300), material=CABIN)
block("Cabin_WallFront", V(1990, 0, 0), (20, 500, 300), material=CABIN)


def wall_with_door(prefix, x, half_width, z_top):
    """A wall across X at x from y -half_width..half_width, z FLOOR..z_top, with a 125 x 210 door opening at y = 0."""
    block(prefix + "_Left", V(x, -(62.5 + half_width) / 2.0, (FLOOR + z_top) / 2.0), (20, half_width - 62.5, z_top - FLOOR), material=CABIN)
    block(prefix + "_Right", V(x, (62.5 + half_width) / 2.0, (FLOOR + z_top) / 2.0), (20, half_width - 62.5, z_top - FLOOR), material=CABIN)
    block(prefix + "_Lintel", V(x, 0, (FLOOR + 210 + z_top) / 2.0), (20, 125, z_top - FLOOR - 210), material=CABIN)


def door(label, x, key=None, reason=None):
    """AMKDoor at the opening of a wall across X: hinge at y = -60.5 on the floor, leaf along +Y, faces +X."""
    d = spawn(unreal.MKDoor, V(x, -60.5, FLOOR), R(), label)
    leaf = d.get_editor_property("leaf")
    leaf.modify()
    leaf.set_static_mesh(DOOR)
    box = DOOR.get_bounding_box()
    lo, hi = box.min, box.max
    if (hi.x - lo.x) > (hi.y - lo.y):
        leaf.set_editor_property("relative_location", V((lo.y + hi.y) / 2, -lo.x, -lo.z))
        leaf.set_editor_property("relative_rotation", R(yaw=90.0))
    else:
        leaf.set_editor_property("relative_location", V(-(lo.x + hi.x) / 2, -lo.y, -lo.z))
    if key:
        d.set_editor_property("required_key_id", key)
        d.set_editor_property("key_required_reason", unreal.Text(reason))
    return d


wall_with_door("Cabin_WallBack", 1400, 250, 150)
block("Airlock_WallL", V(1300, -110, -25), (200, 20, 250), material=CABIN)
block("Airlock_WallR", V(1300, 110, -25), (200, 20, 250), material=CABIN)
block("Airlock_Ceiling", V(1300, 0, 110), (200, 240, 20), material=CABIN)
wall_with_door("Airlock_Outer", 1200, 120, 100)
door("Door_Inner", 1400)
door("Door_Outer", 1200, "Jam.Suit", "Нужен скафандр")
AMBER = (1.0, 0.62, 0.28)
point_light("Light_Cabin_Front", V(1850, 0, 120), AMBER, 4.0, 900.0)
point_light("Light_Cabin_Back", V(1550, 0, 120), AMBER, 2.5, 900.0)
point_light("Light_Airlock", V(1300, 0, 80), AMBER, 2.0, 500.0)
point_light("Light_Emergency", V(1450, -230, 120), (1.0, 0.0, 0.0), 10.0, 600.0, shadows=False)

cabin_zone = spawn(unreal.JamGravityZone, V(1700, 0, 0), None, "Gravity_Cabin")
set_extent(cabin_zone, "zone", (300, 250, 150))
cabin_zone.set_editor_property("refill_per_second", 20.0)
airlock_zone = spawn(unreal.JamGravityZone, V(1300, 0, -25), None, "Gravity_Airlock")
set_extent(airlock_zone, "zone", (100, 100, 125))
airlock_zone.set_editor_property("refill_per_second", 20.0)

spawn(unreal.PlayerStart, V(1650, 0, FLOOR + 100), R(), "PlayerStart")
locker = spawn(unreal.JamSuitLocker, V(1500, 215, FLOOR + 100), R(yaw=-90.0), "SuitLocker")
locker.set_actor_scale3d(V(0.6, 0.4, 2.0))
locker.get_editor_property("mesh").set_material(0, OXYGEN)
label_text("SUIT", V(1500, 200, 70), -90, 25)

# --- the interior to clean, the sockets, the docks ----------------------------------------------
clean = spawn(unreal.JamCleanZone, V(90, 0, 0), None, "Clean_Hull")
set_extent(clean, "zone", (1090, 380, 380))
sockets = []
for j, (centre, rot) in enumerate(holes):
    s = spawn(unreal.JamPlateSocket, centre, rot, "Socket_Breach%d" % j)
    s.set_editor_property("clean_zone", clean)
    sockets.append(s)
fuel_dock = spawn(unreal.JamDockZone, V(-1350, 0, 0), None, "Dock_Fuel")
set_extent(fuel_dock, "zone", (300, 300, 300))
fuel_dock.set_editor_property("required_tag", "Ship.Fuel")
fuel_dock.set_editor_property("required_count", 2)
label_text("FUEL", V(-1350, 0, 380), 180, 50)
nav_dock = spawn(unreal.JamDockZone, V(1000, 0, 0), None, "Dock_Nav")
set_extent(nav_dock, "zone", (150, 200, 200))
nav_dock.set_editor_property("required_tag", "Ship.Nav")
nav_dock.set_editor_property("required_count", 1)
label_text("NAV", V(1000, 0, 230), 180, 30)

panel = spawn(unreal.JamShipPanel, V(1950, 0, FLOOR + 50), R(yaw=180.0), "Panel")
panel.set_editor_property("hull_sockets", sockets)
panel.set_editor_property("fuel_dock", fuel_dock)
panel.set_editor_property("nav_dock", nav_dock)
for lamp_mesh in panel.get_editor_property("lamps"):
    lamp_mesh.set_material(0, LAMP)  # emissive; the panel's MIDs swap its Color red/green
label_text("HULL  FUEL  NAV", V(1920, 0, FLOOR + 115), 180, 12)

for j in range(12):
    # 160 cm apart along the hull so no two start overlapping, and no start drift
    # (Jam.NoDrift): the hull stays dirty until the player cleans it.
    # Within 240 cm of the hull's axis: a corner spot (y, z both ~250) put a box into the
    # octagon's diagonal panel, and the overlap threw it out through the hull.
    ra, rr = rng.uniform(0, 2 * math.pi), rng.uniform(0, 240)
    prop("Junk_Foreign_%02d" % j, V(-850 + 160 * j, rr * math.cos(ra), rr * math.sin(ra)),
         rng.choice([CUBE, CYL]), (rng.uniform(30, 80), rng.uniform(30, 80), rng.uniform(30, 80)),
         rng.uniform(5, 60), JUNK, ["Junk.Foreign", "Jam.NoDrift"], rot=R(rng.uniform(0, 360), rng.uniform(0, 360)))

# --- hull plates in the junk around the ship --------------------------------------------------------
for j, loc in enumerate([V(-1500, 2500, 1000), V(3000, -2000, -1200), V(-2800, -1500, -1500)]):
    prop("HullPlate_%d" % j, loc, CUBE, (250, 250, 20), 60, PLATE, ["Hull.Plate", "Jam.NoDrift"])

# --- the floating room fragment with the pistol --------------------------------------------------------
ROOM = (2500.0, 3000.0, 800.0)   # 6 x 4 x 3 m, open on -Y (toward the ship)
rx, ry, rz = ROOM
block("Room_Floor", V(rx, ry, rz - 150), (600, 400, 20), material=CABIN)
block("Room_Ceiling", V(rx, ry, rz + 150), (600, 400, 20), material=CABIN)
block("Room_Back", V(rx, ry + 200, rz), (600, 20, 300), material=CABIN)
block("Room_SideA", V(rx - 300, ry, rz), (20, 400, 300), material=CABIN)
block("Room_SideB", V(rx + 300, ry, rz), (20, 400, 300), material=CABIN)
pistol = spawn(unreal.JamWeaponPickup, V(rx, ry + 150, rz - 50), R(yaw=-90.0), "Pickup_Pistol")
pistol.set_actor_scale3d(V(0.6, 0.4, 1.8))
pistol.set_editor_property("weapon_index", 0)
pistol.get_editor_property("mesh").set_material(0, PART)
label_text("PISTOL", V(rx, ry + 140, rz + 60), -90, 30)

# --- stepping stones and junk around the ship ----------------------------------------------------------
ANCHORS = [V(600, 1800, 300), V(1500, 2400, 600), V(-600, 900, 900), V(-2000, -800, -600), V(1200, -2200, -300),
           V(-400, -3200, 400), V(2600, -900, 1100), V(-1800, 2600, -700)]
for j, loc in enumerate(ANCHORS):
    s = rng.uniform(200, 300)
    block("Anchor_%d" % j, loc, (s, s, s), R(rng.uniform(0, 90), rng.uniform(0, 90)), material=ANCHOR)
count = 0
while count < 25:
    d = rng.uniform(1000, 4000)
    a, b = rng.uniform(0, 2 * math.pi), rng.uniform(-0.6, 0.6)
    loc = V(d * math.cos(a) * math.cos(b), d * math.sin(a) * math.cos(b), d * math.sin(b))
    if abs(loc.y) < 600 and abs(loc.z) < 600 and -1200 < loc.x < 2200:
        continue  # not inside the hull
    mass = math.exp(rng.uniform(math.log(5.0), math.log(400.0)))
    if rng.random() < 0.25:
        prop("Junk_%02d_drum" % count, loc, DRUM, None, mass, None)
    else:
        s = (mass / 50.0) ** (1.0 / 3.0) * 100.0
        prop("Junk_%02d" % count, loc, rng.choice([CUBE, CYL]), (s * rng.uniform(0.6, 1.4), s * rng.uniform(0.6, 1.4), s),
             mass, JUNK, rot=R(rng.uniform(0, 360), rng.uniform(0, 360)))
    count += 1

# --- the debris belt and the fuel beyond it ------------------------------------------------------------
for ix in range(10):
    for iz in range(5):
        size = rng.uniform(440, 520)
        r = spawn(unreal.JamBreakableRock, V(-1800 + 400 * ix + rng.uniform(-60, 60), -4500 + rng.uniform(-150, 150),
                                              -800 + 400 * iz + rng.uniform(-60, 60)), R(rng.uniform(0, 360)), "Rock_%d_%d" % (ix, iz))
        r.set_actor_scale3d(V(size / 100.0, size / 100.0 * rng.uniform(0.8, 1.1), size / 100.0))
        r.get_editor_property("mesh").set_material(0, ROCK)
for j, loc in enumerate([V(0, -6000, 0), V(300, -6200, 200)]):
    prop("FuelTank_%d" % j, loc, CYL, (120, 120, 300), 300, FUEL, ["Ship.Fuel", "Jam.NoDrift"])
label_text("FUEL TANKS", V(150, -6000, 350), 90, 50)

# --- the planetoid: living, breathable, walkable --------------------------------------------------
# Within 8 m of the surface the player walks on it (radial gravity 980 toward the
# centre; the view's up eases to the surface normal); within 10 m breathing
# restores oxygen. A jump, then Space again in the air, is a jet leap off it.
PLANET, PR = (0.0, 10000.0, -1000.0), 3000.0
block("Planetoid", V(*PLANET), (2 * PR, 2 * PR, 2 * PR), None, SPHERE, FLESH)
walk_shell = spawn(unreal.JamGravityZone, V(*PLANET), None, "Gravity_Planetoid")
walk_shell.set_editor_property("sphere_radius_cm", PR + 800.0)
walk_shell.set_editor_property("radial", True)
walk_shell.set_editor_property("gravity_cm", 980.0)
walk_shell.set_editor_property("refill_per_second", 5.0)
walk_shell.set_editor_property("hud_text", unreal.Text("Можно дышать"))
air_shell = spawn(unreal.JamGravityZone, V(*PLANET), None, "Air_Planetoid")
air_shell.set_editor_property("sphere_radius_cm", PR + 1000.0)
air_shell.set_editor_property("walk", False)
air_shell.set_editor_property("refill_per_second", 5.0)
air_shell.set_editor_property("hud_text", unreal.Text("Можно дышать"))


def on_surface(direction, height):
    d = norm(direction)
    return V(PLANET[0] + d[0] * (PR + height), PLANET[1] + d[1] * (PR + height), PLANET[2] + d[2] * (PR + height)), d


probe_at, probe_up = on_surface((0.0, -1.0, 0.2), 60.0)
probe_rot = unreal.MathLibrary.make_rot_from_z(V(*probe_up))
block("Probe_Body", probe_at, (200, 120, 120), probe_rot, material=ANCHOR)
block("Probe_Wing", on_surface((0.15, -1.0, 0.25), 40.0)[0], (300, 20, 80), probe_rot, material=ANCHOR)
nav_at, _ = on_surface((0.03, -1.0, 0.2), 150.0)
prop("NavModule", nav_at, CUBE, (100, 100, 100), 80, PART, ["Ship.Nav", "Jam.NoDrift"])
label_text("NAV MODULE", on_surface((0.03, -1.0, 0.2), 300.0)[0], 90, 30)
gun_at, gun_up = on_surface((0.6, -0.8, 0.3), 100.0)
shotgun = spawn(unreal.JamWeaponPickup, gun_at, unreal.MathLibrary.make_rot_from_z(V(*gun_up)), "Pickup_Shotgun")
shotgun.set_actor_scale3d(V(0.6, 0.4, 1.8))
shotgun.set_editor_property("weapon_index", 1)
shotgun.get_editor_property("mesh").set_material(0, PART)
label_text("SHOTGUN", on_surface((0.6, -0.8, 0.3), 250.0)[0], 90, 30)

# Finale placeholders: tentacles lying on the surface, a dark eye. No behaviour.
for j, direction in enumerate([(0.5, -0.6, -0.5), (-0.6, -0.5, 0.4), (0.7, 0.2, 0.6), (-0.4, 0.3, -0.8)]):
    at, up = on_surface(direction, 40.0)
    tangent = norm((up[1], -up[0], 0.0)) if abs(up[2]) < 0.9 else (1.0, 0.0, 0.0)
    block("Tentacle_%d" % j, at, (80, 80, 1200), unreal.MathLibrary.make_rot_from_z(V(*tangent)), CYL, ANCHOR)
eye_at, eye_up = on_surface((-0.3, -0.9, -0.2), 100.0)
block("Eye", eye_at, (400, 400, 400), None, SPHERE, EYE, shadow=False)
# Bioluminescence: warm pink and orange glows close to the surface.
for j, (direction, rgb) in enumerate([((0.0, -1.0, 0.25), (1.0, 0.45, 0.6)), ((0.5, -0.8, -0.3), (1.0, 0.55, 0.25)),
                                      ((-0.6, -0.6, 0.5), (1.0, 0.4, 0.55)), ((-0.2, -0.95, -0.4), (1.0, 0.5, 0.3)),
                                      ((0.6, -0.7, 0.4), (1.0, 0.45, 0.5))]):
    point_light("Planetoid_Glow_%d" % j, on_surface(direction, 300.0)[0], rgb, 120.0, 2500.0, shadows=False)

# --- valuable cargo ----------------------------------------------------------------------------------------
cargo_spots = [V(500, 200, -200), V(rx - 150, ry + 100, rz - 100), rear.transform_location(V(0, 0, 0)),
               V(-500, -6100, -300), on_surface((-0.1, -1.0, 0.1), 120.0)[0], V(-1400, -300, 600)]
for j, loc in enumerate(cargo_spots):
    c = spawn(unreal.JamCargo, loc, None, "Cargo_%d" % j)
    c.get_editor_property("body").set_material(0, CARGO)
    zero_damping(c.get_editor_property("body"))
    c.set_editor_property("tags", ["Cargo", "Jam.NoDrift"])

# --- save -------------------------------------------------------------------------------------------------
assert ues.get_editor_world().get_outer().get_path_name() == MAP
if not les.save_current_level():
    raise RuntimeError("save_current_level failed")
log("saved %s with %d actors, %d breaches" % (MAP, len(eas.get_all_level_actors()), len(holes)))
