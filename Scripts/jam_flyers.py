"""Flyer habitats in the jam maps (editor Python, headless). Docs/Flyers.md.

build_ship.py and build_zerog.py call place() before they save. Standalone it
opens both maps, replaces their habitats and saves them (the rest of each map is
untouched) - the way to put the flyers back after a map was taken from elsewhere:
  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/jam_flyers.py" -unattended -nop4 -nosplash -nullrhi
(wrap it so the editor quits, as the other scripts).

Each AJamFlyerHabitat spawns its flock on BeginPlay; the map holds only the habitat:
(label, centre cm, outer radius cm, inner radius cm, flock size).
"""
import unreal

ZEROG = "/Game/Jam/Maps/Lvl_Jam_ZeroG"
SHIP = "/Game/Jam/Maps/Lvl_Jam_Ship"
HABITATS = {
    # The test box (80 x 80 x 50 m): a ball off the start, 26 m from the PlayerStart at the origin.
    ZEROG: [("Flyers_Test", (-1500.0, -2200.0, 0.0), 1300.0, 0.0, 3)],
    SHIP: [
        # Around the planetoid (centre and R 80 m, relief to 86.5 m: build_ship.py): a shell from 90 to 120 m.
        # Its near edge is 37 m from the hull, so the cabin and the airlock stay out of reach.
        ("Flyers_Planetoid", (0.0, 16000.0, -1000.0), 12000.0, 9000.0, 4),
        # Among the debris belt (y -4500) and over the fuel tanks (y -6000..-6200); 32 m from the hull.
        ("Flyers_Belt", (150.0, -6100.0, 100.0), 2500.0, 0.0, 3),
    ],
}

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def place(map_path, log):
    """Replace the flyer habitats of the open map (map_path) with HABITATS'."""
    for actor in eas.get_all_level_actors():
        if isinstance(actor, unreal.JamFlyerHabitat):
            eas.destroy_actor(actor)
    for label, centre, outer, inner, count in HABITATS[map_path]:
        habitat = eas.spawn_actor_from_class(unreal.JamFlyerHabitat, unreal.Vector(*centre), unreal.Rotator())
        habitat.set_actor_label(label)
        habitat.get_editor_property("zone").set_sphere_radius(outer)
        habitat.set_editor_property("inner_radius_cm", inner)
        habitat.set_editor_property("flock_size", count)
        log("flyer habitat %s at %s, radius %.0f m (inner %.0f m), %d flyers" % (label, centre, outer / 100, inner / 100, count))


if __name__ == "__main__":
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    for path in (ZEROG, SHIP):
        if not les.load_level(path):
            raise RuntimeError("cannot open " + path)
        place(path, lambda msg: unreal.log("JAMFLYERS: " + msg))
        if not les.save_current_level():
            raise RuntimeError("save_current_level failed: " + path)
        unreal.log("JAMFLYERS: saved " + path)
