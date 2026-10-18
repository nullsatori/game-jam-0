"""Builds /Game/Jam/Maps/Lvl_Jam_TestRoom from scratch (editor Python, headless).

Run:
  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject -run=pythonscript
      -script="<path>/Scripts/build_test_room.py" -unattended -nop4 -nosplash

The room is a dark 16 x 12 m box of engine cubes (ceiling 3 m), split by a
partition at Y = 800 into the main hall (Y < 800, where the player starts)
and a back corridor. The stations sit along the partition and in the hall:

  1 DOOR + KEY      AMKDoor (key lock) in the partition, AMKKeyPickup on a stand
  2 SWITCH + GATE   AMKControlSwitch -> AMKGate (shutter sliding into the wall)
  3 SWITCH + SHELF  AMKControlSwitch -> AMKShelfBlock (two archive racks over a gap)
  4 CARRY           drum, plank, crate, a 60 kg block (refused), 130 cm ledge
  5 PRY (G)         AMKFastener boards over a 100 x 110 crawl hole
  6 GLASS           AMKGlassPane in a window opening
  7 SHOOT           AMKImpactDummy
  8 APPLIANCES      BP_MK_Microwave, BP_MK_Toilet (AMKAppliance children)

The player is the game's one pawn (AJamZeroGPawn) under the ship's mode
(AJamShipGameMode): a gravity zone over the room lets it walk, with the same
controls as everywhere (F interacts, RMB grabs, Q/E roll); the three gun
pickups stand by the start. World gravity stays normal for the props.

Every number is centimetres. The script deletes an existing map first and
asserts the editor world is the new one before it spawns anything (a failed
new_level leaves the previous level open and would receive the actors).
"""
import unreal

MAP = "/Game/Jam/Maps/Lvl_Jam_TestRoom"
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
CYL = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
MK = "/Game/MindKore/"

eal = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def log(msg):
    unreal.log("JAMROOM: " + msg)


def load(path):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError("missing asset " + path)
    return asset


def V(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def R(yaw=0.0, pitch=0.0, roll=0.0):
    return unreal.Rotator(roll=float(roll), pitch=float(pitch), yaw=float(yaw))


def bounds(mesh):
    box = mesh.get_bounding_box()
    return box.min, box.max


def spawn(cls, loc, yaw=0.0, label=None):
    actor = eas.spawn_actor_from_class(cls, loc, R(yaw))
    if actor is None:
        raise RuntimeError("spawn failed: %s" % cls)
    if label:
        actor.set_actor_label(label)
    return actor


def box(label, x0, x1, y0, y1, z0, z1, mesh=None):
    """An engine cube stretched over the given world box (static, blocks all)."""
    actor = eas.spawn_actor_from_object(mesh or CUBE, V((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), R())
    actor.set_actor_label(label)
    actor.set_actor_scale3d(V((x1 - x0) / 100.0, (y1 - y0) / 100.0, (z1 - z0) / 100.0))
    return actor


def mesh_actor(label, mesh, loc, yaw=0.0):
    actor = eas.spawn_actor_from_object(mesh, loc, R(yaw))
    actor.set_actor_label(label)
    return actor


def label_text(text, loc, yaw):
    actor = spawn(unreal.TextRenderActor, loc, yaw, "Label_" + text.split()[0])
    comp = actor.get_editor_property("text_render")
    comp.set_editor_property("text", unreal.Text(text))
    comp.set_editor_property("world_size", 18.0)
    comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    comp.set_editor_property("text_render_color", unreal.Color(r=200, g=200, b=170, a=255))
    return actor


def seat_in_wall(mesh, centre_x, wall_y, bottom_z):
    """Location and yaw that put a flat mesh's bounds centred at centre_x, in the
    wall plane Y = wall_y, bottom at bottom_z, its thin horizontal axis along Y."""
    lo, hi = bounds(mesh)
    yaw = 90.0 if (hi.x - lo.x) < (hi.y - lo.y) else 0.0
    cx, cy = (lo.x + hi.x) / 2, (lo.y + hi.y) / 2
    if yaw == 90.0:
        # Rotated: mesh (x, y) -> world (-y, x).
        return V(centre_x + cy, wall_y - cx, bottom_z - lo.z), yaw
    return V(centre_x - cx, wall_y - cy, bottom_z - lo.z), yaw


def set_rel(comp, loc=None, yaw=None, scale=None):
    comp.modify()
    if loc is not None:
        comp.set_editor_property("relative_location", loc)
    if yaw is not None:
        comp.set_editor_property("relative_rotation", R(yaw))
    if scale is not None:
        comp.set_editor_property("relative_scale3d", scale)


# --- fresh, non-partitioned level -------------------------------------------
if eal.does_asset_exist(MAP):
    les.load_level("/Engine/Maps/Entry")
    if not eal.delete_asset(MAP):
        raise RuntimeError("could not delete the old " + MAP)
    log("old map deleted")
if not les.new_level(MAP, False):
    raise RuntimeError("new_level failed for " + MAP)
world = ues.get_editor_world()
package = world.get_outer().get_path_name()
if package != MAP:
    raise RuntimeError("editor world is %s, not %s: refusing to spawn" % (package, MAP))
log("new level " + package)
world.get_world_settings().set_editor_property("default_game_mode", unreal.JamShipGameMode.static_class())

# --- shell -------------------------------------------------------------------
W, D, H, T = 1600.0, 1200.0, 300.0, 20.0     # room size and wall thickness
PY = 800.0                                    # partition plane (wall Y 790..810)
box("Floor", -T, W + T, -T, D + T, -20, 0)
box("Ceiling", -T, W + T, -T, D + T, H, H + 20)
box("Wall_South", -T, W + T, -T, 0, 0, H)
box("Wall_North", -T, W + T, D, D + T, 0, H)
box("Wall_West", -T, 0, 0, D, 0, H)
box("Wall_East", W, W + T, 0, D, 0, H)

# Openings in the partition: (name, x0, x1, z0, z1).
door_mesh = load(MK + "Art/Admin/Doors/SM_MK_Door_Wood")
frame_mesh = load(MK + "Art/Admin/Doors/SM_MK_DoorFrame_Wood")
flo, fhi = bounds(frame_mesh)
frame_w = max(fhi.x - flo.x, fhi.y - flo.y)
frame_h = fhi.z - flo.z
log("door frame %.0f x %.0f, leaf bounds %s" % (frame_w, frame_h, bounds(door_mesh)))
DOOR_X0 = 100.0
OPEN = [
    ("Door", DOOR_X0, DOOR_X0 + frame_w, 0.0, frame_h),
    ("Gate", 400.0, 550.0, 0.0, 220.0),
    ("Boards", 700.0, 800.0, 0.0, 110.0),
    ("Window", 950.0, 1100.0, 90.0, 230.0),
    ("Shelf", 1300.0, 1480.0, 0.0, 220.0),
]
x = 0.0
for name, x0, x1, z0, z1 in OPEN:
    if x0 > x:
        box("Partition_%s_left" % name, x, x0, PY - 10, PY + 10, 0, H)
    if z0 > 0:
        box("Partition_%s_sill" % name, x0, x1, PY - 10, PY + 10, 0, z0)
    box("Partition_%s_lintel" % name, x0, x1, PY - 10, PY + 10, z1, H)
    x = x1
box("Partition_end", x, W, PY - 10, PY + 10, 0, H)

# --- lights: dark (test maps are built dark), a flicker over the hall -------
for i, (lx, ly) in enumerate([(300, 400), (1300, 400), (800, 1000)]):
    light = spawn(unreal.PointLight, V(lx, ly, 270), 0, "DimLight_%d" % i)
    comp = light.get_editor_property("point_light_component")
    comp.set_editor_property("intensity", 6.0)
    comp.set_editor_property("attenuation_radius", 650.0)
    comp.set_editor_property("light_color", unreal.Color(r=255, g=214, b=170, a=255))
flicker = spawn(unreal.MKFlickerLightActor, V(800, 400, 255), 0, "FlickerLight")
flicker.set_editor_property("base_intensity", 600.0)
flicker.set_editor_property("flicker_intensity", 250.0)
flicker.set_editor_property("attenuation_radius", 700.0)
PENDANT = MK + "Art/Admin/UpperArt20260924/Props/PendantEnamel_D36/SM_MK_AP_PendantEnamel_D36_SM_MK_AP_PendantEnamel_D36_"
for part in ("Mount", "Body"):
    mesh_actor("Pendant_" + part, load(PENDANT + part), V(800, 400, 300))
emitter = mesh_actor("Pendant_Emitter", load(PENDANT + "Emitter"), V(800, 400, 300))
flicker.set_editor_property("emissive_fixture_actor", emitter)
ppv = spawn(unreal.PostProcessVolume, V(800, 600, 150), 0, "PostProcess_Dark")
ppv.set_editor_property("unbound", True)
settings = ppv.get_editor_property("settings")
settings.set_editor_property("override_auto_exposure_bias", True)
settings.set_editor_property("auto_exposure_bias", -1.0)
ppv.set_editor_property("settings", settings)

# --- 8 player start ----------------------------------------------------------
spawn(unreal.PlayerStart, V(800, 150, 100), 90, "PlayerStart")
# The ship's mode: walking and air come from a gravity zone over the whole room; the guns from pickups.
room_zone = spawn(unreal.JamGravityZone, V(W / 2, D / 2, H / 2), 0, "Gravity_Room")
room_zone.get_editor_property("zone").set_box_extent(V(W / 2, D / 2, H / 2 + 50))
room_zone.set_editor_property("refill_per_second", 30.0)
for index, (name, x) in enumerate((("PISTOL", 1000.0), ("SHOTGUN", 1120.0), ("SV-98", 1240.0))):
    gun = spawn(unreal.JamWeaponPickup, V(x, 120, 60), 90, "Pickup_" + name)
    gun.set_actor_scale3d(V(0.3, 0.5, 1.2))
    gun.set_editor_property("weapon_index", index)
    label_text(name, V(x, 120, 160), 90)

# --- 1 door + key --------------------------------------------------------------
fx = DOOR_X0 + frame_w / 2
floc, fyaw = seat_in_wall(frame_mesh, fx, PY, 0.0)
mesh_actor("Door1_Frame", frame_mesh, floc, fyaw)
leaf_lo, leaf_hi = bounds(door_mesh)
leaf_w = max(leaf_hi.x - leaf_lo.x, leaf_hi.y - leaf_lo.y)
# AMKDoor frame: origin at the hinge on the floor, leaf along local +Y, faces
# local +X closed. Yaw -90 lays local +Y along world +X (the partition) and
# faces the hall (-Y).
hinge_x = fx - leaf_w / 2
door = spawn(unreal.MKDoor, V(hinge_x, PY, 0), -90, "Door1_KeyLocked")
leaf = door.get_editor_property("leaf")
leaf.modify()
leaf.set_static_mesh(door_mesh)
if (leaf_hi.x - leaf_lo.x) > (leaf_hi.y - leaf_lo.y):
    # Authored along mesh X: turn it onto the door's +Y; mesh (x, y) -> (-y, x).
    set_rel(leaf, V((leaf_lo.y + leaf_hi.y) / 2, -leaf_lo.x, -leaf_lo.z), 90.0)
else:
    set_rel(leaf, V(-(leaf_lo.x + leaf_hi.x) / 2, -leaf_lo.y, -leaf_lo.z), 0.0)
door.set_editor_property("required_key_id", "jam_key")
box("Door1_KeyStand", 330, 370, 280, 320, 0, 95)
# SM_MK_Yard_KeyTag hangs below its pivot (z -19..-0.6): the hook sits 25 cm above the stand.
key = spawn(unreal.MKKeyPickup, V(350, 300, 120), 0, "Door1_Key")
key_mesh = key.get_editor_property("mesh")
key_mesh.modify()
key_mesh.set_static_mesh(load(MK + "Art/Admin/Yard/KeyRoute/SM_MK_Yard_KeyTag"))
key.set_editor_property("key_id", "jam_key")
label_text("1 DOOR + KEY", V(fx, PY - 12, 250), -90)


def wall_switch(label, x):
    """An AMKControlSwitch on the hall face of the partition. The switch faces its
    local +X (its back on the wall), so yaw -90 faces the hall."""
    sw = spawn(unreal.MKControlSwitch, V(x, PY - 10, 125), -90, label)
    for prop, name in (("body", "Body"), ("handle", "Handle"), ("lamp", "Lamp")):
        comp = sw.get_editor_property(prop)
        comp.modify()
        comp.set_static_mesh(load(MK + "Art/Admin/Switch/SM_MK_Switch_" + name))
    sw.set_editor_property("lamp_lit_material", load(MK + "Art/Admin/Materials/M_Pilot_Amber"))
    sw.set_editor_property("lamp_dark_material", load(MK + "Art/Admin/Switch/M_MK_Switch_LensDark"))
    return sw


# --- 2 switch -> gate -----------------------------------------------------------
shutter = load(MK + "Art/Admin/Facade/SM_MK_Fac_Shutter")
slo, shi = bounds(shutter)
log("shutter bounds %s %s" % (slo, shi))
gate = spawn(unreal.MKGate, V(475, PY, 0), 0, "Gate2_Shutter")
panel = gate.get_editor_property("panel")
panel.modify()
panel.set_static_mesh(shutter)
thin_x = (shi.x - slo.x) < (shi.y - slo.y)
span = (shi.y - slo.y) if thin_x else (shi.x - slo.x)
sx, sz = 150.0 / max(span, 1.0), 220.0 / max(shi.z - slo.z, 1.0)
if thin_x:
    set_rel(panel, V((slo.y + shi.y) / 2 * sx, -(slo.x + shi.x) / 2, -slo.z * sz), 90.0, V(1, sx, sz))
else:
    set_rel(panel, V(-(slo.x + shi.x) / 2 * sx, -(slo.y + shi.y) / 2, -slo.z * sz), 0.0, V(sx, 1, sz))
# Slides sideways into the wall to its right, like a pocket door.
gate.set_editor_property("open_offset_cm", V(155, 0, 0))
gate.set_editor_property("travel_seconds", 1.5)
sw2 = wall_switch("Gate2_Switch", 600)
sw2.set_editor_property("gates", [gate])
sw2.set_editor_property("toggle_gates", True)
sw2.set_editor_property("one_shot", False)
label_text("2 SWITCH + GATE", V(475, PY - 12, 250), -90)

# --- 3 switch -> shelf block ---------------------------------------------------
rack = load(MK + "Art/Admin/Meshes/SM_MK_ArchiveRack_90")
folders = load(MK + "Art/Admin/Meshes/SM_MK_ArchiveFolders_A")
rlo, rhi = bounds(rack)
log("rack bounds %s %s" % (rlo, rhi))
rack_w, rack_d, rack_h = rhi.x - rlo.x, rhi.y - rlo.y, rhi.z - rlo.z
shelf = spawn(unreal.MKShelfBlock, V(1300, PY - 10 - rack_d, 0), 0, "Shelf3_Block")
for i in range(2):
    origin = V(1300 + i * rack_w - rlo.x, PY - 10 - rack_d - rlo.y, -rlo.z)
    r = mesh_actor("Shelf3_Rack%d" % i, rack, origin)
    f = mesh_actor("Shelf3_Folders%d" % i, folders, origin)
    for a in (r, f):
        a.get_editor_property("static_mesh_component").set_mobility(unreal.ComponentMobility.MOVABLE)
        a.attach_to_actor(shelf, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
                          unreal.AttachmentRule.KEEP_WORLD, False)
shelf.set_editor_property("footprint_center_cm", V(rack_w, rack_d / 2, rack_h / 2))
shelf.set_editor_property("footprint_half_extent_cm", V(rack_w, rack_d / 2, rack_h / 2))
shelf.set_editor_property("travel_offset_cm", V(-2 * rack_w - 10, 0, 0))
shelf.set_editor_property("travel_seconds", 3.0)
sw3 = wall_switch("Shelf3_Switch", 1540)
sw3.set_editor_property("shelf_blocks", [shelf])
sw3.set_editor_property("one_shot", False)
label_text("3 SWITCH + SHELF", V(1390, PY - 60, 250), -90)

# --- 4 carry -----------------------------------------------------------------------
box("Carry4_Ledge130", 1400, W, 80, 400, 0, 130)


def prop(label, mesh, loc, mass, scale=None):
    p = spawn(unreal.MKPhysicsProp, loc, 0, label)
    body = p.get_editor_property("body")
    body.modify()
    body.set_static_mesh(mesh)
    if scale is not None:
        p.set_actor_scale3d(scale)
    p.set_editor_property("mass_kg", float(mass))
    return p


drum = load(MK + "Art/Admin/Yard/Props2/SM_MK_Yard_Drum")
dlo, dhi = bounds(drum)
prop("Carry4_Drum", drum, V(1250, 250, 2 - dlo.z), 25)
# A plank from an engine cube: SM_plywood_sheet has no simple collision and
# cannot simulate; SM_bench_board pulls 68 MB of 4K concrete textures.
prop("Carry4_Plank", CUBE, V(1150, 550, 4), 6, V(2.2, 0.2, 0.04))
prop("Carry4_Crate", CUBE, V(1250, 450, 32), 15, V(0.6, 0.6, 0.6))
prop("Carry4_Block60kg", CUBE, V(1100, 300, 22), 60, V(0.5, 0.4, 0.4))
label_text("4 CARRY (ledge 130)", V(1500, 400, 200), 180)

# --- 5 pry --------------------------------------------------------------------------
boards = load(MK + "Art/Admin/Yard/KeyRoute/SM_MK_StoreBoards_Intact")
bloc, byaw = seat_in_wall(boards, 750, PY - 12, 0.0)
fastener = spawn(unreal.MKFastener, bloc, byaw, "Pry5_Boards")
blocker = fastener.get_editor_property("blocker")
blocker.modify()
blocker.set_static_mesh(boards)
label_text("5 PRY (G = crowbar)", V(750, PY - 12, 250), -90)

# --- 6 glass ------------------------------------------------------------------------
pane = spawn(unreal.MKGlassPane, V(1025, PY, 160), 0, "Glass6_Pane")
pane.set_editor_property("pane_width_cm", 150.0)
pane.set_editor_property("pane_height_cm", 140.0)
pane.set_editor_property("intact_material", load(MK + "Materials/MI_MK_Glass_Pane"))
pane.set_editor_property("cracked_material", load(MK + "Materials/MI_MK_Glass_Cracked"))
pane.set_editor_property("debris_material", load(MK + "Materials/MI_MK_Glass_Shard"))
label_text("6 GLASS", V(1025, PY - 12, 255), -90)

# --- 7 shoot ------------------------------------------------------------------------
spawn(unreal.MKImpactDummy, V(500, 450, 0), -90, "Shoot7_Dummy")
label_text("7 SHOOT", V(500, 450, 250), -90)

# --- 8 appliances ---------------------------------------------------------------------
box("Appl8_Table", 40, 140, 380, 480, 0, 90)
for path, loc, name in ((MK + "Household/BP_MK_Microwave", V(90, 430, 90), "Appl8_Microwave"),
                        (MK + "Household/BP_MK_Toilet", V(60, 620, 0), "Appl8_Toilet")):
    bp = load(path)
    a = eas.spawn_actor_from_object(bp, loc, R(0))
    a.set_actor_label(name)
label_text("8 APPLIANCES", V(120, 530, 200), 0)

# --- save ---------------------------------------------------------------------------
world = ues.get_editor_world()
assert world.get_outer().get_path_name() == MAP
if not les.save_current_level():
    raise RuntimeError("save_current_level failed")
log("saved %s with %d actors" % (MAP, len(eas.get_all_level_actors())))
