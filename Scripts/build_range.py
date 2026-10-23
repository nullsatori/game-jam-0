"""Builds /Game/OS/Maps/Lvl_OS_Range: a test range for the guns and the living walls (editor Python, headless).

  UnrealEditor-Cmd.exe <path>/OuterSpace.uproject
      -ExecutePythonScript="<path>/Scripts/build_range.py" -unattended -nop4 -nosplash -nullrhi
(wrap it in a script that calls unreal.SystemLibrary.quit_editor(), see README)

The ship's mode (AOSShipGameMode), so a gravity zone gives a floor to stand on
and beyond it is zero-G. The hall: a 30 x 20 m floor open to space, gravity and
air over it. At the start, on the left out of the firing lanes, three pickups -
pistol, shotgun, SV-98 - and the grapple. Down the hall, facing the start: a square flesh wall at 8 m, another at
14 m off to the side, a round membrane at 20 m, each with a box behind it to see
through the hole. Past the hall's end, in zero-G: a floating square wall and two
anchor blocks to cling to (RMB) and shoot from. Warm lamps in front of the
walls, a low sun, a fixed exposure.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_flesh  # noqa: E402
import os_living_wall  # noqa: E402

MAP = "/Game/OS/Maps/Lvl_OS_Range"
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
MATS = "/Game/OS/Materials"
eal = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def log(msg):
    unreal.log("OSRANGE: " + msg)


def V(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def R(yaw=0.0, pitch=0.0, roll=0.0):
    return unreal.Rotator(roll=float(roll), pitch=float(pitch), yaw=float(yaw))


def spawn(cls, loc, rot=None, label=None):
    actor = eas.spawn_actor_from_class(cls, loc, rot or R())
    if actor is None:
        raise RuntimeError("spawn failed: %s" % cls)
    if label:
        actor.set_actor_label(label)
    return actor


def block(label, centre, size, material=None):
    actor = eas.spawn_actor_from_object(CUBE, centre, R())
    actor.set_actor_label(label)
    actor.set_actor_scale3d(V(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    if material:
        actor.get_editor_property("static_mesh_component").set_material(0, material)
    return actor


def label_text(text, loc, yaw, size=40.0):
    actor = spawn(unreal.TextRenderActor, loc, R(yaw), "Label_" + text.split()[0])
    comp = actor.get_editor_property("text_render")
    comp.set_editor_property("text", unreal.Text(text))
    comp.set_editor_property("world_size", size)
    comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    comp.set_editor_property("text_render_color", unreal.Color(r=200, g=200, b=170, a=255))
    return actor


def lamp(label, loc, intensity, radius):
    light = spawn(unreal.PointLight, loc, None, label)
    comp = light.get_editor_property("point_light_component")
    comp.set_editor_property("intensity", intensity)
    comp.set_editor_property("attenuation_radius", radius)
    comp.set_editor_property("light_color", unreal.Color(r=255, g=205, b=160, a=255))


def tinted(name):
    return unreal.load_asset(MATS + "/" + name)  # made by the other map builders


# --- fresh level -------------------------------------------------------------------------
if eal.does_asset_exist(MAP):
    if not les.load_level(MAP) or ues.get_editor_world().get_outer().get_path_name() != MAP:
        raise RuntimeError("load_level failed for " + MAP)
    eas.destroy_actors([a for a in eas.get_all_level_actors()
                        if not isinstance(a, unreal.WorldSettings) and not (isinstance(a, unreal.Brush) and not isinstance(a, unreal.Volume))])
elif not les.new_level(MAP, False):
    raise RuntimeError("new_level failed for " + MAP)
world = ues.get_editor_world()
if world.get_outer().get_path_name() != MAP:
    raise RuntimeError("editor world is %s, not %s: refusing to spawn" % (world.get_outer().get_path_name(), MAP))
settings = world.get_world_settings()
settings.modify()
settings.set_editor_property("global_gravity_set", True)
settings.set_editor_property("global_gravity_z", 0.0)
settings.set_editor_property("default_game_mode", unreal.OSShipGameMode.static_class())

ANCHOR = tinted("MI_OS_Anchor")
PART = tinted("MI_OS_ShipPart")
JUNK = tinted("MI_OS_Junk")

# --- light: a low sun, a sky light, a fixed exposure ------------------------------------
sun = spawn(unreal.DirectionalLight, V(0, 0, 2000), R(yaw=-30.0, pitch=-35.0), "Sun")
sun.get_editor_property("light_component").set_editor_property("intensity", 3.0)
sky = spawn(unreal.SkyLight, V(0, 0, 0), None, "SkyLight")
sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky.get_editor_property("light_component").set_editor_property("intensity", 0.6)
sky.get_editor_property("light_component").set_editor_property("lower_hemisphere_is_black", False)
ppv = spawn(unreal.PostProcessVolume, V(0, 0, 0), None, "PostProcess_Exposure")
ppv.set_editor_property("unbound", True)
pp = ppv.get_editor_property("settings")
pp.set_editor_property("override_auto_exposure_method", True)
pp.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
pp.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
pp.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
pp.set_editor_property("override_auto_exposure_bias", True)
pp.set_editor_property("auto_exposure_bias", 0.0)
ppv.set_editor_property("settings", pp)

# --- the hall: floor, gravity and air, the start with the pickups ------------------------
block("Floor", V(1500, 0, -10), (3000, 2000, 20), ANCHOR)
hall = spawn(unreal.OSGravityZone, V(1500, 0, 600), None, "Gravity_Hall")
hall.get_editor_property("zone").set_box_extent(V(1500, 1000, 600))
hall.set_editor_property("refill_per_second", 30.0)
hall.set_editor_property("hud_text", unreal.Text("Тир: гравитация, воздух"))
spawn(unreal.PlayerStart, V(150, 0, 100), R(), "PlayerStart")
# On the left, out of the firing lanes (the walls stand at y -350, 0 and 450).
for index, (name, y) in enumerate((("PISTOL", -650.0), ("SHOTGUN", -800.0), ("SV-98", -950.0))):
    pickup = spawn(unreal.OSWeaponPickup, V(300, y, 60), R(yaw=90.0), "Pickup_" + name)
    pickup.set_actor_scale3d(V(0.4, 0.6, 1.2))
    pickup.set_editor_property("weapon_index", index)
    pickup.get_editor_property("mesh").set_material(0, PART)
    label_text(name, V(300, y, 160), 90, 30)
grapple = spawn(unreal.OSToolPickup, V(300, -500, 60), R(yaw=90.0), "Pickup_Grapple")
grapple.set_actor_scale3d(V(0.4, 0.6, 1.2))
grapple.get_editor_property("mesh").set_material(0, ANCHOR)
label_text("GRAPPLE", V(300, -500, 160), 90, 30)

# --- living walls down the hall, facing the start (-X) -----------------------------------
WALL_MAT = os_living_wall.material(log)
FLESH = os_flesh.materials(log)


def flesh_wall(label, centre, yaw, size, round_=False):
    wall = spawn(unreal.OSLivingWall, centre, R(yaw=yaw), label)
    wall.set_editor_property("width_cm", size)
    wall.set_editor_property("height_cm", size)
    wall.set_editor_property("round", round_)
    wall.set_editor_property("wall_material", WALL_MAT)
    os_flesh.skin_wall(wall, FLESH, round_=round_)
    return wall


# A wall's front (+Y) faces +X at yaw -90 (build_zerog.py): yaw 90 turns it to the start.
square_near = flesh_wall("Wall_Square_8m", V(800, -350, 200), 90.0, 400.0)
square_near.set_editor_property("tags", ["OS.WallProbe"])
flesh_wall("Wall_Square_14m", V(1400, 450, 200), 90.0, 400.0)
flesh_wall("Wall_Round_20m", V(2000, 0, 250), 90.0, 400.0, round_=True)
for label, loc in (("Target_8m", V(950, -350, 60)), ("Target_14m", V(1550, 450, 60)), ("Target_20m", V(2150, 0, 60))):
    block(label, loc, (120, 120, 120), JUNK)
lamp("Lamp_8m", V(550, -350, 400), 25.0, 900.0)
lamp("Lamp_14m", V(1150, 450, 400), 25.0, 900.0)
lamp("Lamp_20m", V(1750, 0, 450), 25.0, 900.0)
label_text("8 M", V(800, -350, 450), 180)
label_text("14 M", V(1400, 450, 450), 180)
label_text("20 M  ROUND", V(2000, 0, 500), 180)

# --- past the hall: zero-G, a floating wall, anchors to cling to ---------------------------
flesh_wall("Wall_ZeroG", V(4200, 0, 600), 90.0, 400.0)
block("Anchor_A", V(3400, -300, 600), (200, 200, 200), ANCHOR)
block("Anchor_B", V(3400, 400, 900), (200, 200, 200), ANCHOR)
lamp("Lamp_ZeroG", V(3800, 0, 900), 30.0, 1200.0)
label_text("ZERO-G: RMB ON A BLOCK TO HOLD ON", V(3400, 0, 1150), 180, 30)

assert ues.get_editor_world().get_outer().get_path_name() == MAP
if not les.save_current_level():
    raise RuntimeError("save_current_level failed")
log("saved %s with %d actors" % (MAP, len(eas.get_all_level_actors())))
