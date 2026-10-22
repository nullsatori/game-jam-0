"""Flyer habitats (and the ship's roosts and encounter) in the game maps (editor Python, headless). Docs/Flyers.md.

build_ship.py and build_zerog.py call place() before they save. Standalone it
opens the maps (OS_FLYERS_MAPS=ship, zerog or both - the default), replaces only
the actors it owns (tag OS.Flyers, or a label of its lists below) and saves them;
the rest of each map is untouched - the way to put the flyers back after a map was
taken from elsewhere, and to place the roosts:
  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/os_flyers.py" -unattended -nop4 -nosplash -nullrhi
(wrap it so the editor quits, as the other scripts). Lvl_OS_Ship is edited by hand
(Nick): `git lfs lock` it first, dump its actors before and after.

Each AOSFlyerHabitat spawns its flock on BeginPlay; the map holds only the habitat:
(label, centre cm, outer radius cm, inner radius cm, flock size, settings).
"""
import os

import unreal

ZEROG = "/Game/OS/Maps/Lvl_OS_ZeroG"
SHIP = "/Game/OS/Maps/Lvl_OS_Ship"
TAG = "OS.Flyers"

# The roost clings to this actor (Nick's USD import, by object name): the wreck. The player's ship (root_3) has none.
WRECK_HULL = "root_0"     # MindKore_Ship_02_Blockout, the abandoned ship by the debris belt (no way in yet)

HABITATS = {
    # The test box (80 x 80 x 50 m): a ball off the start, 26 m from the PlayerStart at the origin.
    ZEROG: [("Flyers_Test", (-1500.0, -2200.0, 0.0), 1300.0, 0.0, 3, {})],
    SHIP: [
        # Around the planetoid (centre and R 80 m, relief to 86.5 m: build_ship.py): a shell from 90 to 120 m.
        ("Flyers_Planetoid", (0.0, 16000.0, -1000.0), 12000.0, 9000.0, 4, {}),
        # Among the debris belt (y -4500) and over the fuel tanks (y -6000..-6200).
        ("Flyers_Belt", (150.0, -6100.0, 100.0), 2500.0, 0.0, 3, {}),
        # The roost (moved off the player's ship onto the wreck 19.10, owner: "the flock goes to the other ship"): one
        # flock - the old ship roost's and the wreck's - clinging head out on the wreck's hull, densest along the top ridge
        # of its aft half, the edge the player sees as he crosses from the airlock: a file of spikes against the star that
        # rims them, leading the eye aft (stills 19.10, takes 1-3: focus further aft hid them on the far side; falloff 7 m
        # thinned the ridge); a few patrol around it. The first EVA is unarmed: it spares an unarmed player (menace, no bite) until the first gun;
        # armed, only coming within 4 m of a clinging one wakes it, and no one clings within 5.5 m of the flesh wall, so
        # the wall's pain call is the trigger. It chases far (the leash reaches the player's ship: the way home).
        ("Flyers_WreckRoost", (3405.0, -6933.0, 1344.0), 3800.0, 0.0, 30, dict(
            roost_on=[WRECK_HULL], roost_share=0.8, roost_spacing_cm=200.0,
            roost_focus=(3900.0, -6900.0, 2100.0), roost_focus_falloff_cm=900.0, roost_wake_radius_cm=400.0, roost_clear_of_doors_cm=550.0,
            roost_seed=int(os.environ.get("OS_WRECK_ROOST_SEED", "19")),
            spare_unarmed=True, leash_cm=9000.0, aggro_margin_cm=500.0)),
    ],
}

# The encounter: a flesh wall on the wreck the player must shotgun; tearing it calls the wreck's roost (flesh is
# their nerves). The wreck has no way in yet (os.Flyer.Rays 19.10: its bow room is sealed on every side), so the
# wall grows on its flank facing the player's ship (a hull point os.Flyer.Rays found, 30 cm off it); when the wreck
# opens, move the wall into the doorway - the link stays. (label, centre, outward normal, up, size cm, habitat, delay s.)
WALLS = {
    SHIP: [("Flesh_WreckDoor", (3044.7, -5177.7, 850.5), (0.78, 0.60, 0.15), (-0.370, 0.259, 0.892), (400.0, 400.0),
            "Flyers_WreckRoost", 8.0)],
}

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _owned(actor, labels):
    return TAG in [str(t) for t in actor.tags] or actor.get_actor_label() in labels


def _by_name(name):
    for actor in eas.get_all_level_actors():
        if actor.get_name() == name:
            return actor
    return None


def _wall(label, centre, normal, up, size, log):
    """A living wall of size (w, h) at centre, its front (+Y) along normal, its height along up."""
    normal = unreal.Vector(*normal).normal()
    up = unreal.Vector(*up)
    up = (up - normal * up.dot(normal)).normal()
    width = normal.cross(up)  # X = Y x Z: the wall front (+Y) is then the normal
    centre = unreal.Vector(*centre) + normal * 30.0
    wall = eas.spawn_actor_from_class(unreal.OSLivingWall, centre, unreal.MathLibrary.make_rot_from_xz(width, up))
    wall.set_actor_label(label)
    wall.set_editor_property("width_cm", size[0])
    wall.set_editor_property("height_cm", size[1])
    wall.set_editor_property("wall_material", unreal.load_asset("/Game/OS/Materials/MI_OS_LivingWall"))
    # The mesh look (Scripts/os_flesh.py skin_wall), from the saved materials: no asset is touched here.
    wall.set_editor_property("skin_mesh", unreal.load_asset("/Game/OSArt/Meshes/LivingWall/SM_OS_LivingWall_Square"))
    skin = wall.get_editor_property("skin")
    for slot, name in {"Muscle": "Wall_Muscle", "Tendon": "Wall_Tendon", "Vessel": "Wall_Vessel", "Membrane": "Wall_Membrane", "FrameMetal": "FrameMetal"}.items():
        mi = unreal.load_asset("/Game/OSArt/Materials/Flesh/MI_OS_" + name)
        if mi:
            skin.set_material_by_name(slot, mi)
    front = wall.get_actor_right_vector()
    log("flesh wall %s at (%.0f, %.0f, %.0f), %.0f x %.0f cm, front (%.2f, %.2f, %.2f)" % (label, centre.x, centre.y, centre.z, size[0], size[1], front.x, front.y, front.z))
    return wall


def place(map_path, log):
    """Replace the flyer habitats and walls this script owns in the open map (map_path)."""
    labels = [h[0] for h in HABITATS.get(map_path, [])] + [w[0] for w in WALLS.get(map_path, [])]
    for actor in eas.get_all_level_actors():
        if isinstance(actor, (unreal.OSFlyerHabitat, unreal.OSLivingWall)) and _owned(actor, labels):
            eas.destroy_actor(actor)
    spawned = {}
    for label, centre, outer, inner, count, settings in HABITATS[map_path]:
        habitat = eas.spawn_actor_from_class(unreal.OSFlyerHabitat, unreal.Vector(*centre), unreal.Rotator())
        habitat.set_actor_label(label)
        habitat.set_editor_property("tags", [TAG])
        habitat.get_editor_property("zone").set_sphere_radius(outer)
        habitat.set_editor_property("inner_radius_cm", inner)
        habitat.set_editor_property("flock_size", count)
        for key, value in settings.items():
            if key == "roost_on":
                actors = [_by_name(name) for name in value]
                if None in actors:
                    raise RuntimeError("%s: roost surface missing: %s" % (label, value))
                habitat.set_editor_property("roost_on", actors)
            elif key == "roost_focus":
                habitat.set_editor_property("roost_focus", unreal.Vector(*value))
            elif key == "spare_unarmed":
                habitat.set_editor_property("spare_unarmed", value)
            else:
                habitat.set_editor_property(key, value)
        spawned[label] = habitat
        log("flyer habitat %s at %s, radius %.0f m (inner %.0f m), %d flyers%s" % (label, centre, outer / 100, inner / 100, count,
                                                                                  ", " + ", ".join(sorted(settings)) if settings else ""))
    for label, centre, normal, up, size, habitat_label, delay in WALLS.get(map_path, []):
        wall = _wall(label, centre, normal, up, size, log)
        wall.set_editor_property("tags", [TAG])
        link = unreal.OSFleshLink()
        link.set_editor_property("habitat", spawned[habitat_label])
        link.set_editor_property("arrival_delay_seconds", delay)
        wall.set_editor_property("linked_habitats", [link])
        log("  %s calls %s (arrival delay %.1f s)" % (label, habitat_label, delay))


if __name__ == "__main__":
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    which = os.environ.get("OS_FLYERS_MAPS", "both")
    paths = {"ship": (SHIP,), "zerog": (ZEROG,)}.get(which, (ZEROG, SHIP))
    for path in paths:
        if not les.load_level(path):
            raise RuntimeError("cannot open " + path)
        place(path, lambda msg: unreal.log("OSFLYERS: " + msg))
        if not les.save_current_level():
            raise RuntimeError("save_current_level failed: " + path)
        unreal.log("OSFLYERS: saved " + path)
    if os.environ.get("OS_FLYERS_QUIT"):
        unreal.SystemLibrary.quit_editor()
