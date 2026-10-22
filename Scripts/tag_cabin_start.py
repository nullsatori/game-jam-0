"""Tag the cabin's PlayerStart "Cabin" in Lvl_OS_Ship (editor Python, headless).

AOSShipGameMode wakes the player at the PlayerStart tagged Cabin after death.
The ship map is edited by hand, so this touches only that one actor: it opens
the map, finds the PlayerStart inside the cabin's gravity zone (label
Gravity_Cabin), adds the tag (modify() first, so the change is saved) and saves.
Running it again changes nothing. build_ship.py tags the start it spawns.

  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/tag_cabin_start.py" -unattended -nop4 -nosplash -nullrhi
"""
import unreal

SHIP = "/Game/OS/Maps/Lvl_OS_Ship"
TAG = "Cabin"


def log(message):
    unreal.log("OSCABIN: " + message)


def run():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not les.load_level(SHIP):
        raise RuntimeError("cannot open " + SHIP)
    actors = eas.get_all_level_actors()
    cabins = [a for a in actors if isinstance(a, unreal.OSGravityZone) and a.get_actor_label() == "Gravity_Cabin"]
    if len(cabins) != 1:
        raise RuntimeError("expected one Gravity_Cabin zone, found %d" % len(cabins))
    starts = [a for a in actors if isinstance(a, unreal.PlayerStart) and cabins[0].contains(a.get_actor_location(), 0.0)]
    if len(starts) != 1:
        raise RuntimeError("expected one PlayerStart inside Gravity_Cabin, found %d" % len(starts))
    start = starts[0]
    tags = [str(t) for t in start.tags]
    log("%s (%s) at %s, tags %s" % (start.get_actor_label(), start.get_name(), start.get_actor_location(), tags))
    if TAG in tags:
        log("already tagged, nothing to save")
        return
    start.modify()
    start.set_editor_property("tags", tags + [TAG])
    if not les.save_current_level():
        raise RuntimeError("save_current_level failed")
    log("tagged %s and saved %s" % (start.get_actor_label(), SHIP))


try:
    run()
finally:
    unreal.SystemLibrary.quit_editor()
