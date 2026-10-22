"""Builds /Game/OS/Maps/Lvl_OS_ZeroG from scratch (editor Python, headless).

Run (the editor stays open after a plain -ExecutePythonScript; wrap it in a
script that calls unreal.SystemLibrary.quit_editor(), see README):
  UnrealEditor-Cmd.exe <path>/OuterSpace.uproject
      -ExecutePythonScript="<path>/Scripts/build_zerog.py" -unattended -nop4 -nosplash -nullrhi

A closed 80 x 80 x 50 m box centred on the origin, lit for play (the
"dark test maps" rule is for test stands, not this game map): a sun whose
light the shell does not shadow, a low sky light and a fixed exposure.
Shapes are coloured by role: anchors grey, junk mid grey-brown, ship parts
orange, oxygen blue, the dock outlined in green. WorldSettings: global
gravity 0 and the AOSShipGameMode override (the one mode of every map). Inside: floating junk
(AMKPhysicsProp, 5-500 kg; the mode (AOSZeroGGameMode's StartPlay) gives it drift and spin on
StartPlay), static 4 m anchor blocks to push off, a rocket stand-in at the far
+X end with the dock zone and four tagged ship parts (Ship.Part) 20-40 m away,
oxygen canisters, a glass pane in a floating frame, an impact dummy,
a living wall (AOSLivingWall) closing an alcove against the shell with a ship
part inside, and the PlayerStart at the origin facing +X, 1 m from the first anchor's face,
with the gun and grapple pickups beside it.
Everything is seeded: the same map every run.
"""
import math
import os
import random
import sys

import unreal

# The shared living wall material (Scripts/os_living_wall.py), found next to this script.
sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_build  # noqa: E402
import os_dust  # noqa: E402
import os_flesh  # noqa: E402
import os_living_wall  # noqa: E402

MAP = "/Game/OS/Maps/Lvl_OS_ZeroG"
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
CYL = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
DRUM = unreal.load_asset("/Game/MindKore/Art/Admin/Yard/Props2/SM_MK_Yard_Drum")

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MATS = "/Game/OS/Materials"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
rng = random.Random(7)


def log(msg):
    unreal.log("OSZEROG: " + msg)


def V(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def R(yaw=0.0, pitch=0.0, roll=0.0):
    return unreal.Rotator(roll=float(roll), pitch=float(pitch), yaw=float(yaw))


def spawn(cls, loc, yaw=0.0, label=None):
    actor = eas.spawn_actor_from_class(cls, loc, R(yaw))
    if actor is None:
        raise RuntimeError("spawn failed: %s" % cls)
    if label:
        actor.set_actor_label(label)
    return actor


def block(label, centre, size, mesh=None, material=None, shadow=True, collide=True):
    """A static engine cube (or other 100 cm shape) of the given size in cm."""
    actor = eas.spawn_actor_from_object(mesh or CUBE, centre, R())
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
    """A colour instance of the engine's BasicShapeMaterial (its "Color" parameter), made once."""
    path = MATS + "/" + name
    if eal.does_asset_exist(path):
        return unreal.load_asset(path)
    mi = tools.create_asset(name, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial"))
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    os_build.save(path)
    log("made " + path)
    return mi


def label_text(text, loc, yaw, size=60.0):
    actor = spawn(unreal.TextRenderActor, loc, yaw, "Label_" + text.split()[0])
    comp = actor.get_editor_property("text_render")
    comp.set_editor_property("text", unreal.Text(text))
    comp.set_editor_property("world_size", size)
    comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    comp.set_editor_property("text_render_color", unreal.Color(r=200, g=200, b=170, a=255))
    return actor


def zero_damping(component):
    body = component.get_editor_property("body_instance")
    body.set_editor_property("linear_damping", 0.0)
    body.set_editor_property("angular_damping", 0.0)
    component.set_editor_property("body_instance", body)


# --- fresh, non-partitioned level --------------------------------------------
if eal.does_asset_exist(MAP):
    # Rebuilt in place: the map is loaded and every actor cleared (its file is edited, never deleted).
    if not les.load_level(MAP) or ues.get_editor_world().get_outer().get_path_name() != MAP:
        raise RuntimeError("load_level failed for " + MAP)
    # Volumes (post-process, etc.) are Brush subclasses too: keep only the builder brush, or every
    # rebuild left one more PostProcess_Look behind.
    eas.destroy_actors([a for a in eas.get_all_level_actors()
                        if not isinstance(a, unreal.WorldSettings) and not (isinstance(a, unreal.Brush) and not isinstance(a, unreal.Volume))])
elif not les.new_level(MAP, False):
    raise RuntimeError("new_level failed for " + MAP)
world = ues.get_editor_world()
if world.get_outer().get_path_name() != MAP:
    raise RuntimeError("editor world is %s, not %s: refusing to spawn" % (world.get_outer().get_path_name(), MAP))
log("new level " + MAP)

log("BasicShapeMaterial vector parameters: %s" % mel.get_vector_parameter_names(unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial")))
SHELL = tint("MI_OS_Shell", (0.12, 0.12, 0.13))
ANCHOR = tint("MI_OS_Anchor", (0.45, 0.46, 0.48))
JUNK = tint("MI_OS_Junk", (0.32, 0.27, 0.22))
PART = tint("MI_OS_ShipPart", (0.95, 0.42, 0.05))
OXYGEN = tint("MI_OS_O2", (0.1, 0.35, 0.95))
DOCK = tint("MI_OS_Dock", (0.2, 0.9, 0.3))
ROCKET_MAT = tint("MI_OS_Rocket", (0.85, 0.85, 0.8))
os_dust.material(log)  # AOSZeroGPawn's dust specks

settings = world.get_world_settings()
settings.modify()
settings.set_editor_property("global_gravity_set", True)
settings.set_editor_property("global_gravity_z", 0.0)
# One pawn, one rule set on every map (owner 18.10): the ship's mode; the guns come from pickups.
settings.set_editor_property("default_game_mode", unreal.OSShipGameMode.static_class())

# --- the box -----------------------------------------------------------------
HX, HY, HZ, T = 4000.0, 4000.0, 2500.0, 100.0
# The shell casts no shadow, so the sun reaches inside the closed box.
block("Shell_Floor", V(0, 0, -HZ - T / 2), (2 * HX + 2 * T, 2 * HY + 2 * T, T), material=SHELL, shadow=False)
block("Shell_Ceiling", V(0, 0, HZ + T / 2), (2 * HX + 2 * T, 2 * HY + 2 * T, T), material=SHELL, shadow=False)
block("Shell_North", V(0, HY + T / 2, 0), (2 * HX, T, 2 * HZ), material=SHELL, shadow=False)
block("Shell_South", V(0, -HY - T / 2, 0), (2 * HX, T, 2 * HZ), material=SHELL, shadow=False)
block("Shell_East", V(HX + T / 2, 0, 0), (T, 2 * HY, 2 * HZ), material=SHELL, shadow=False)
block("Shell_West", V(-HX - T / 2, 0, 0), (T, 2 * HY, 2 * HZ), material=SHELL, shadow=False)

# --- light: a sun as the key, a low sky light, a few warm points, fixed exposure ---
sun = spawn(unreal.DirectionalLight, V(0, 0, 2000), 0, "Sun")
sun.set_actor_rotation(R(yaw=35.0, pitch=-40.0), False)
sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sun_light = sun.get_editor_property("light_component")
sun_light.set_editor_property("intensity", 3.0)
sun_light.set_editor_property("light_color", unreal.Color(r=255, g=244, b=228, a=255))
sky = spawn(unreal.SkyLight, V(0, 0, 0), 0, "SkyLight_Low")
sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky_light = sky.get_editor_property("light_component")
sky_light.set_editor_property("intensity", 0.6)
sky_light.set_editor_property("lower_hemisphere_is_black", False)
sky_light.set_editor_property("lower_hemisphere_color", unreal.LinearColor(0.05, 0.06, 0.08, 1.0))
for i, loc in enumerate([(0, 0, 1500), (2500, 0, 0), (-2500, 2000, 0), (-2000, -2500, 800), (1500, 2800, -1200), (3300, 0, 1200)]):
    light = spawn(unreal.PointLight, V(*loc), 0, "DimLight_%d" % i)
    comp = light.get_editor_property("point_light_component")
    comp.set_editor_property("intensity", 40.0)
    comp.set_editor_property("attenuation_radius", 3500.0)
    comp.set_editor_property("light_color", unreal.Color(r=200, g=215, b=255, a=255))
# Fixed exposure: manual, no physical camera, bias 0 - a 3 lux sun on a mid
# albedo reads mid grey, and the view never pumps when you turn to the dark.
ppv = spawn(unreal.PostProcessVolume, V(0, 0, 0), 0, "PostProcess_Exposure")
ppv.set_editor_property("unbound", True)
pp = ppv.get_editor_property("settings")
pp.set_editor_property("override_auto_exposure_method", True)
pp.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
pp.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
pp.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
pp.set_editor_property("override_auto_exposure_bias", True)
pp.set_editor_property("auto_exposure_bias", 0.0)
ppv.set_editor_property("settings", pp)

# --- player, anchors, rocket -------------------------------------------------------
spawn(unreal.PlayerStart, V(0, 0, 0), 0, "PlayerStart")
# The guns and the grapple on pickups beside the start, as on the other maps (F takes them).
for index, (name, y) in enumerate((("PISTOL", -250.0), ("SHOTGUN", -400.0), ("SV-98", -550.0))):
    gun = spawn(unreal.OSWeaponPickup, V(150, y, -60), 90, "Pickup_" + name)
    gun.set_actor_scale3d(V(0.3, 0.5, 1.2))
    gun.set_editor_property("weapon_index", index)
    label_text(name, V(150, y, 60), 90, 25.0)
grapple = spawn(unreal.OSToolPickup, V(150, -700, -60), 90, "Pickup_Grapple")
grapple.set_actor_scale3d(V(0.3, 0.5, 1.2))
label_text("GRAPPLE", V(150, -700, 60), 90, 25.0)
ANCHORS = [V(300, 0, 0), V(-2000, 1500, 500), V(1500, -2200, -800), V(-1800, -2000, 1200), V(2200, 2000, -1500)]
for i, centre in enumerate(ANCHORS):
    block("Anchor_%d" % i, centre, (400, 400, 400), material=ANCHOR)
ROCKET = V(3300, 0, 0)
block("Rocket_StandIn", ROCKET, (300, 300, 1500), CYL, material=ROCKET_MAT)
label_text("ROCKET", V(3300 - 250, 0, 900), 180)

# --- stations -----------------------------------------------------------------------
for i, loc in enumerate([(-300, 300, 0), (0, -400, 150), (200, 400, -200), (-500, -200, 100)]):
    canister = spawn(unreal.OSOxygenCanister, V(*loc), 0, "O2_Canister_%d" % i)
    canister.get_editor_property("body").set_material(0, OXYGEN)
    zero_damping(canister.get_editor_property("body"))
label_text("O2", V(-300, 300, 80), 0, 30)

pane = spawn(unreal.MKGlassPane, V(0, 1200, 0), 0, "Glass_Pane")
pane.set_editor_property("pane_width_cm", 200.0)
pane.set_editor_property("pane_height_cm", 150.0)
MK = "/Game/MindKore/Materials/"
pane.set_editor_property("intact_material", unreal.load_asset(MK + "MI_MK_Glass_Pane"))
pane.set_editor_property("cracked_material", unreal.load_asset(MK + "MI_MK_Glass_Cracked"))
pane.set_editor_property("debris_material", unreal.load_asset(MK + "MI_MK_Glass_Shard"))
for name, centre, size in (("Top", V(0, 1200, 85), (220, 20, 20)), ("Bottom", V(0, 1200, -85), (220, 20, 20)),
                           ("Left", V(-110, 1200, 0), (20, 20, 190)), ("Right", V(110, 1200, 0), (20, 20, 190))):
    block("Glass_Frame_" + name, centre, size, material=ANCHOR)
label_text("GLASS", V(0, 1190, 130), -90, 30)

spawn(unreal.MKImpactDummy, V(0, -1200, -120), 90, "Dummy")
label_text("DUMMY", V(0, -1190, 140), 90, 30)

# --- ship parts and the dock --------------------------------------------------------
# Loot: big tagged parts 20-40 m from the rocket, to be towed or pushed into the dock.
dock = spawn(unreal.OSDockZone, V(2600, 0, 0), 0, "Dock_Ship")
dock.set_editor_property("required_count", 4)
label_text("DOCK", V(2600, 0, 900), 180)
# The dock's box (AOSDockZone default extent 600 x 700 x 800) as twelve thin green edges, no collision.
DX, DY, DZ, E = 600.0, 700.0, 800.0, 8.0
for a in (-1, 1):
    for b in (-1, 1):
        block("Dock_Edge_X_%d%d" % (a, b), V(2600, a * DY, b * DZ), (2 * DX, E, E), material=DOCK, shadow=False, collide=False)
        block("Dock_Edge_Y_%d%d" % (a, b), V(2600 + a * DX, 0, b * DZ), (E, 2 * DY, E), material=DOCK, shadow=False, collide=False)
        block("Dock_Edge_Z_%d%d" % (a, b), V(2600 + a * DX, b * DY, 0), (E, E, 2 * DZ), material=DOCK, shadow=False, collide=False)
PARTS = [(V(1000, 1500, 0), "cube", (2.0, 2.5, 3.0), 300.0), (V(0, -1500, -600), "cylinder", (2.2, 2.2, 4.0), 800.0),
         (V(500, 2500, 1200), "cube", (3.5, 2.0, 2.0), 500.0), (V(1300, -400, -1500), "cylinder", (3.0, 3.0, 2.0), 200.0)]
for i, (loc, kind, scale, mass) in enumerate(PARTS):
    part = spawn(unreal.MKPhysicsProp, loc, 0, "ShipPart_%d_%dkg" % (i, mass))
    body = part.get_editor_property("body")
    body.modify()
    body.set_static_mesh(CUBE if kind == "cube" else CYL)
    body.set_material(0, PART)
    part.set_actor_scale3d(V(*scale))
    part.set_editor_property("mass_kg", mass)
    part.set_editor_property("tags", ["Ship.Part"])
    zero_damping(body)

# --- junk -----------------------------------------------------------------------------
KEEP_CLEAR = [(V(0, 0, 0), 700.0), (V(0, 1200, 0), 300.0), (V(0, -1200, 0), 300.0), (ROCKET, 600.0)] + \
             [(a, 450.0) for a in ANCHORS] + [(loc, 400.0) for loc, _, _, _ in PARTS] + [(V(2600, 0, 0), 900.0)]


def clear(p):
    return all(math.dist((p.x, p.y, p.z), (c.x, c.y, c.z)) > r for c, r in KEEP_CLEAR)


count = 0
while count < 40:
    p = V(rng.uniform(-3500, 3500), rng.uniform(-3500, 3500), rng.uniform(-2000, 2000))
    if not clear(p):
        continue
    kind = rng.choice(["cube", "cube", "cylinder", "drum"])
    mass = math.exp(rng.uniform(math.log(5.0), math.log(500.0)))
    prop = spawn(unreal.MKPhysicsProp, p, rng.uniform(0, 360), "Junk_%02d_%s_%dkg" % (count, kind, mass))
    body = prop.get_editor_property("body")
    body.modify()
    if kind == "drum":
        body.set_static_mesh(DRUM)
    else:
        body.set_static_mesh(CUBE if kind == "cube" else CYL)
        body.set_material(0, JUNK)
        s = (mass / 50.0) ** (1.0 / 3.0)  # bigger with mass, about 1 m at 50 kg
        prop.set_actor_scale3d(V(s * rng.uniform(0.6, 1.4), s * rng.uniform(0.6, 1.4), s * rng.uniform(0.6, 1.4)))
    prop.set_editor_property("mass_kg", float(mass))
    zero_damping(body)
    count += 1
log("junk: %d props" % count)

# --- someone to shoot: more floating dummies (after the junk, so its seed is unchanged) ---
for i, (loc, yaw) in enumerate([(V(-1500, 0, 600), 0), (V(1500, 1200, -400), 200), (V(-800, -2600, -900), 60)]):
    spawn(unreal.MKImpactDummy, loc, yaw, "Dummy_Floating_%d" % i)

# --- the living wall: an alcove against the shell, closed by AOSLivingWall (Docs/LivingWall.md) ---
# After everything else so the seeded layout above is unchanged; the first
# candidate mouth with nothing placed within 1.5 m of the alcove is taken.
WALL_MAT = os_living_wall.material(log)
DEPTH, OPEN_W, SIDE_T = 300.0, 400.0, 40.0
X_MOUTH = -HX + DEPTH
taken = [a.get_actor_location() for a in eas.get_all_level_actors()
         if not a.get_actor_label().startswith(("Shell_", "Sun", "SkyLight", "PostProcess", "DimLight"))]
for y0, z0 in ((0.0, 0.0), (-2500.0, 0.0), (2500.0, 0.0), (0.0, -1500.0), (0.0, 1500.0)):
    if all(not (-HX - 150 < p.x < X_MOUTH + 150 and abs(p.y - y0) < OPEN_W / 2 + 190 and abs(p.z - z0) < OPEN_W / 2 + 190) for p in taken):
        break
else:
    raise RuntimeError("no clear spot for the living wall alcove")
xc = (-HX + X_MOUTH) / 2
half = OPEN_W / 2 + SIDE_T / 2
block("Alcove_Top", V(xc, y0, z0 + half), (DEPTH, OPEN_W + 2 * SIDE_T, SIDE_T), material=ANCHOR)
block("Alcove_Bottom", V(xc, y0, z0 - half), (DEPTH, OPEN_W + 2 * SIDE_T, SIDE_T), material=ANCHOR)
block("Alcove_Left", V(xc, y0 - half, z0), (DEPTH, SIDE_T, OPEN_W), material=ANCHOR)
block("Alcove_Right", V(xc, y0 + half, z0), (DEPTH, SIDE_T, OPEN_W), material=ANCHOR)
wall = spawn(unreal.OSLivingWall, V(X_MOUTH - 10, y0, z0), -90, "LivingWall")  # yaw -90: its front (+Y) faces +X, the arena
wall.set_editor_property("width_cm", OPEN_W)
wall.set_editor_property("height_cm", OPEN_W)
wall.set_editor_property("thin_spot_count", 1)
wall.set_editor_property("wall_material", WALL_MAT)
os_flesh.skin_wall(wall, os_flesh.materials(log))  # the mesh look (Docs/LivingWall.md)
wall.set_editor_property("tags", ["OS.WallProbe"])
prize = spawn(unreal.MKPhysicsProp, V(xc, y0, z0), 0, "ShipPart_Alcove_40kg")
prize_body = prize.get_editor_property("body")
prize_body.modify()
prize_body.set_static_mesh(CUBE)
prize_body.set_material(0, PART)
prize.set_actor_scale3d(V(0.6, 0.6, 0.6))
prize.set_editor_property("mass_kg", 40.0)
prize.set_editor_property("tags", ["Ship.Part", "OS.NoDrift"])
zero_damping(prize_body)
inside = spawn(unreal.PointLight, V(xc - 60, y0, z0 + 120), 0, "Alcove_Light")
inside_light = inside.get_editor_property("point_light_component")
inside_light.set_editor_property("intensity", 15.0)
inside_light.set_editor_property("attenuation_radius", 450.0)
inside_light.set_editor_property("light_color", unreal.Color(r=255, g=190, b=140, a=255))
# Its face looks away from the sun: a warm lamp in front so the weave reads.
front = spawn(unreal.PointLight, V(X_MOUTH + 250, y0, z0 + 150), 0, "LivingWall_Light")
front_light = front.get_editor_property("point_light_component")
front_light.set_editor_property("intensity", 25.0)
front_light.set_editor_property("attenuation_radius", 900.0)
front_light.set_editor_property("light_color", unreal.Color(r=255, g=210, b=180, a=255))
label_text("LIVING WALL", V(X_MOUTH + 10, y0, z0 + OPEN_W / 2 + 70), 0, 50)
log("living wall at the alcove mouth x %.0f, y %.0f, z %.0f" % (X_MOUTH - 10, y0, z0))

# --- flyers: their habitats (Scripts/os_flyers.py, Docs/Flyers.md) ---
import os_flyers  # noqa: E402
os_flyers.place(MAP, log)

# --- save -------------------------------------------------------------------------------
assert ues.get_editor_world().get_outer().get_path_name() == MAP
if not les.save_current_level():
    raise RuntimeError("save_current_level failed")
log("saved %s with %d actors" % (MAP, len(eas.get_all_level_actors())))
