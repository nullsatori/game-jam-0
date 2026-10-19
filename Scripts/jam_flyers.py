"""Flyer habitats (and the ship's roosts and encounter) in the jam maps (editor Python, headless). Docs/Flyers.md.

build_ship.py and build_zerog.py call place() before they save. Standalone it
opens the maps (JAM_FLYERS_MAPS=ship, zerog or both - the default), replaces only
the actors it owns (tag Jam.Flyers, or a label of its lists below) and saves them;
the rest of each map is untouched - the way to put the flyers back after a map was
taken from elsewhere, and to place the roosts:
  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/jam_flyers.py" -unattended -nop4 -nosplash -nullrhi
(wrap it so the editor quits, as the other scripts). Lvl_Jam_Ship is edited by hand
(Nick): `git lfs lock` it first, dump its actors before and after.

Each AJamFlyerHabitat spawns its flock on BeginPlay; the map holds only the habitat:
(label, centre cm, outer radius cm, inner radius cm, flock size, settings).
"""
import os

import unreal

ZEROG = "/Game/Jam/Maps/Lvl_Jam_ZeroG"
SHIP = "/Game/Jam/Maps/Lvl_Jam_Ship"
TAG = "Jam.Flyers"

# The roosts cling to these actors (Nick's USD imports, by object name): the player's ship and the wreck.
SHIP_HULL = "root_3"      # MindKore_Ship_R3_Blockout, x 3220..7220, the airlock on its -Y side at x 5560
WRECK_HULL = "root_0"     # MindKore_Ship_02_Blockout, the abandoned ship by the debris belt (no way in yet)

HABITATS = {
    # The test box (80 x 80 x 50 m): a ball off the start, 26 m from the PlayerStart at the origin.
    ZEROG: [("Flyers_Test", (-1500.0, -2200.0, 0.0), 1300.0, 0.0, 3, {})],
    SHIP: [
        # Around the planetoid (centre and R 80 m, relief to 86.5 m: build_ship.py): a shell from 90 to 120 m.
        ("Flyers_Planetoid", (0.0, 16000.0, -1000.0), 12000.0, 9000.0, 4, {}),
        # Among the debris belt (y -4500) and over the fuel tanks (y -6000..-6200).
        ("Flyers_Belt", (150.0, -6100.0, 100.0), 2500.0, 0.0, 3, {}),
        # The ship's roost: most cling to the hull head out, densest on the lit roof strip aft of the airlock - the
        # first thing seen out of the door, a file of spikes leading the eye aft to the star that rims them - the
        # rest down the flank and hanging under the belly (the dark side); a few patrol (stills 19.10, third take). The first EVA is unarmed: the
        # roost spares an unarmed player (menace, no bite) until the first gun; armed, only coming within 4.5 m of a
        # clinging one wakes it (the door stays 5.5 m clear: stepping out is not a provocation).
        ("Flyers_ShipRoost", (5220.0, 0.0, 780.0), 2700.0, 0.0, 26, dict(
            roost_on=[SHIP_HULL], roost_share=0.8, roost_spacing_cm=180.0,
            roost_focus=(6500.0, -500.0, 990.0), roost_focus_falloff_cm=600.0,
            roost_wake_radius_cm=450.0, roost_clear_of_doors_cm=550.0,
            roost_seed=int(os.environ.get("JAM_SHIP_ROOST_SEED", "19")),
            spare_unarmed=True, leash_cm=2500.0, aggro_margin_cm=300.0)),
        # The wreck's roost: the flock the flesh wall calls (the encounter). Densest aft, over the engines, away from
        # the open bow the player comes in by; it wakes only from close (4 m) - the wall's pain call is the trigger -
        # and chases far (the way home).
        ("Flyers_WreckRoost", (3405.0, -6933.0, 1344.0), 3800.0, 0.0, 10, dict(
            roost_on=[WRECK_HULL], roost_share=0.7, roost_spacing_cm=260.0,
            roost_focus=(4200.0, -8250.0, 2400.0), roost_focus_falloff_cm=2500.0, roost_wake_radius_cm=400.0,
            spare_unarmed=False, leash_cm=9000.0, aggro_margin_cm=500.0)),
    ],
}

# The encounter: a flesh wall on the wreck the player must shotgun; tearing it calls the wreck's roost (flesh is
# their nerves). The wreck has no way in yet (jam.Flyer.Rays 19.10: its bow room is sealed on every side), so the
# wall grows on its flank facing the player's ship (a hull point jam.Flyer.Rays found, 30 cm off it); when the wreck
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
    wall = eas.spawn_actor_from_class(unreal.JamLivingWall, centre, unreal.MathLibrary.make_rot_from_xz(width, up))
    wall.set_actor_label(label)
    wall.set_editor_property("width_cm", size[0])
    wall.set_editor_property("height_cm", size[1])
    wall.set_editor_property("wall_material", unreal.load_asset("/Game/Jam/Materials/MI_Jam_LivingWall"))
    # The mesh look (Scripts/jam_flesh.py skin_wall), from the saved materials: no asset is touched here.
    wall.set_editor_property("skin_mesh", unreal.load_asset("/Game/JamArt/Meshes/LivingWall/SM_Jam_LivingWall_Square"))
    skin = wall.get_editor_property("skin")
    for slot, name in {"Muscle": "Wall_Muscle", "Tendon": "Wall_Tendon", "Vessel": "Wall_Vessel", "Membrane": "Wall_Membrane", "FrameMetal": "FrameMetal"}.items():
        mi = unreal.load_asset("/Game/JamArt/Materials/Flesh/MI_Jam_" + name)
        if mi:
            skin.set_material_by_name(slot, mi)
    front = wall.get_actor_right_vector()
    log("flesh wall %s at (%.0f, %.0f, %.0f), %.0f x %.0f cm, front (%.2f, %.2f, %.2f)" % (label, centre.x, centre.y, centre.z, size[0], size[1], front.x, front.y, front.z))
    return wall


def place(map_path, log):
    """Replace the flyer habitats and walls this script owns in the open map (map_path)."""
    labels = [h[0] for h in HABITATS.get(map_path, [])] + [w[0] for w in WALLS.get(map_path, [])]
    for actor in eas.get_all_level_actors():
        if isinstance(actor, (unreal.JamFlyerHabitat, unreal.JamLivingWall)) and _owned(actor, labels):
            eas.destroy_actor(actor)
    spawned = {}
    for label, centre, outer, inner, count, settings in HABITATS[map_path]:
        habitat = eas.spawn_actor_from_class(unreal.JamFlyerHabitat, unreal.Vector(*centre), unreal.Rotator())
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
        link = unreal.JamFleshLink()
        link.set_editor_property("habitat", spawned[habitat_label])
        link.set_editor_property("arrival_delay_seconds", delay)
        wall.set_editor_property("linked_habitats", [link])
        log("  %s calls %s (arrival delay %.1f s)" % (label, habitat_label, delay))


if __name__ == "__main__":
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    which = os.environ.get("JAM_FLYERS_MAPS", "both")
    paths = {"ship": (SHIP,), "zerog": (ZEROG,)}.get(which, (ZEROG, SHIP))
    for path in paths:
        if not les.load_level(path):
            raise RuntimeError("cannot open " + path)
        place(path, lambda msg: unreal.log("JAMFLYERS: " + msg))
        if not les.save_current_level():
            raise RuntimeError("save_current_level failed: " + path)
        unreal.log("JAMFLYERS: saved " + path)
    if os.environ.get("JAM_FLYERS_QUIT"):
        unreal.SystemLibrary.quit_editor()
