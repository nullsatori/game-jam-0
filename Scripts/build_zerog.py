"""Builds /Game/Jam/Maps/Lvl_Jam_ZeroG from scratch (editor Python, headless).

Run (the editor stays open after a plain -ExecutePythonScript; wrap it in a
script that calls unreal.SystemLibrary.quit_editor(), see README):
  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/build_zerog.py" -unattended -nop4 -nosplash -nullrhi

A dark, closed 80 x 80 x 50 m box centred on the origin. WorldSettings: global
gravity 0 and the AJamZeroGGameMode override. Inside: floating junk
(AMKPhysicsProp, 5-500 kg; AJamZeroGGameMode gives it drift and spin on
StartPlay), static 4 m anchor blocks to push off, a rocket stand-in at the far
+X end with the dock zone and four tagged ship parts (Ship.Part) 20-40 m away,
oxygen canisters, a glass pane in a floating frame, an impact dummy
and the PlayerStart at the origin facing +X, 1 m from the first anchor's face.
Everything is seeded: the same map every run.
"""
import math
import random

import unreal

MAP = "/Game/Jam/Maps/Lvl_Jam_ZeroG"
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
CYL = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
DRUM = unreal.load_asset("/Game/MindKore/Art/Admin/Yard/Props2/SM_MK_Yard_Drum")

eal = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
rng = random.Random(7)


def log(msg):
    unreal.log("JAMZEROG: " + msg)


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


def block(label, centre, size, mesh=None):
    """A static engine cube (or other 100 cm shape) of the given size in cm."""
    actor = eas.spawn_actor_from_object(mesh or CUBE, centre, R())
    actor.set_actor_label(label)
    actor.set_actor_scale3d(V(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    return actor


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
settings.set_editor_property("default_game_mode", unreal.JamZeroGGameMode.static_class())

# --- the box -----------------------------------------------------------------
HX, HY, HZ, T = 4000.0, 4000.0, 2500.0, 100.0
block("Shell_Floor", V(0, 0, -HZ - T / 2), (2 * HX + 2 * T, 2 * HY + 2 * T, T))
block("Shell_Ceiling", V(0, 0, HZ + T / 2), (2 * HX + 2 * T, 2 * HY + 2 * T, T))
block("Shell_North", V(0, HY + T / 2, 0), (2 * HX, T, 2 * HZ))
block("Shell_South", V(0, -HY - T / 2, 0), (2 * HX, T, 2 * HZ))
block("Shell_East", V(HX + T / 2, 0, 0), (T, 2 * HY, 2 * HZ))
block("Shell_West", V(-HX - T / 2, 0, 0), (T, 2 * HY, 2 * HZ))

# --- light: dark, a few dim points ----------------------------------------------
for i, loc in enumerate([(0, 0, 1500), (2500, 0, 0), (-2500, 2000, 0), (-2000, -2500, 800), (1500, 2800, -1200), (3300, 0, 1200)]):
    light = spawn(unreal.PointLight, V(*loc), 0, "DimLight_%d" % i)
    comp = light.get_editor_property("point_light_component")
    comp.set_editor_property("intensity", 40.0)
    comp.set_editor_property("attenuation_radius", 3500.0)
    comp.set_editor_property("light_color", unreal.Color(r=200, g=215, b=255, a=255))
ppv = spawn(unreal.PostProcessVolume, V(0, 0, 0), 0, "PostProcess_Dark")
ppv.set_editor_property("unbound", True)
pp = ppv.get_editor_property("settings")
pp.set_editor_property("override_auto_exposure_bias", True)
pp.set_editor_property("auto_exposure_bias", -1.0)
ppv.set_editor_property("settings", pp)

# --- player, anchors, rocket -------------------------------------------------------
spawn(unreal.PlayerStart, V(0, 0, 0), 0, "PlayerStart")
ANCHORS = [V(300, 0, 0), V(-2000, 1500, 500), V(1500, -2200, -800), V(-1800, -2000, 1200), V(2200, 2000, -1500)]
for i, centre in enumerate(ANCHORS):
    block("Anchor_%d" % i, centre, (400, 400, 400))
ROCKET = V(3300, 0, 0)
block("Rocket_StandIn", ROCKET, (300, 300, 1500), CYL)
label_text("ROCKET", V(3300 - 250, 0, 900), 180)

# --- stations -----------------------------------------------------------------------
for i, loc in enumerate([(-300, 300, 0), (0, -400, 150), (200, 400, -200), (-500, -200, 100)]):
    canister = spawn(unreal.JamOxygenCanister, V(*loc), 0, "O2_Canister_%d" % i)
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
    block("Glass_Frame_" + name, centre, size)
label_text("GLASS", V(0, 1190, 130), -90, 30)

spawn(unreal.MKImpactDummy, V(0, -1200, -120), 90, "Dummy")
label_text("DUMMY", V(0, -1190, 140), 90, 30)

# --- ship parts and the dock --------------------------------------------------------
# Loot: big tagged parts 20-40 m from the rocket, to be towed or pushed into the dock.
dock = spawn(unreal.JamDockZone, V(2600, 0, 0), 0, "Dock_Ship")
dock.set_editor_property("required_count", 4)
label_text("DOCK", V(2600, 0, 900), 180)
PARTS = [(V(1000, 1500, 0), "cube", (2.0, 2.5, 3.0), 300.0), (V(0, -1500, -600), "cylinder", (2.2, 2.2, 4.0), 800.0),
         (V(500, 2500, 1200), "cube", (3.5, 2.0, 2.0), 500.0), (V(1300, -400, -1500), "cylinder", (3.0, 3.0, 2.0), 200.0)]
for i, (loc, kind, scale, mass) in enumerate(PARTS):
    part = spawn(unreal.MKPhysicsProp, loc, 0, "ShipPart_%d_%dkg" % (i, mass))
    body = part.get_editor_property("body")
    body.modify()
    body.set_static_mesh(CUBE if kind == "cube" else CYL)
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
        s = (mass / 50.0) ** (1.0 / 3.0)  # bigger with mass, about 1 m at 50 kg
        prop.set_actor_scale3d(V(s * rng.uniform(0.6, 1.4), s * rng.uniform(0.6, 1.4), s * rng.uniform(0.6, 1.4)))
    prop.set_editor_property("mass_kg", float(mass))
    zero_damping(body)
    count += 1
log("junk: %d props" % count)

# --- someone to shoot: more floating dummies (after the junk, so its seed is unchanged) ---
for i, (loc, yaw) in enumerate([(V(-1500, 0, 600), 0), (V(1500, 1200, -400), 200), (V(-800, -2600, -900), 60)]):
    spawn(unreal.MKImpactDummy, loc, yaw, "Dummy_Floating_%d" % i)

# --- save -------------------------------------------------------------------------------
assert ues.get_editor_world().get_outer().get_path_name() == MAP
if not les.save_current_level():
    raise RuntimeError("save_current_level failed")
log("saved %s with %d actors" % (MAP, len(eas.get_all_level_actors())))
