"""The flyer's sound assets (editor Python, headless). Docs/Flyers.md, "Sound".

  OS_WAV_DIR=<out dir of Scripts/os_flyer_sounds.py> UnrealEditor-Cmd.exe <path>/OuterSpace.uproject
      -ExecutePythonScript="<path>/Scripts/os_flyer_audio.py" -unattended -nop4 -nosplash -nullrhi

- Imports every SW_OS_Flyer_*.wav (mono stems) into /Game/OSArt/Audio/Flyer, replacing; the
  flutter loops. Every wave gets the class SC_OS_Flyer and a priority under the guns' 1.0
  (voice 0.9, impacts 0.8, wings 0.6): when voices run out, the player's own gun survives.
- Deletes the pre-layer waves (SW_OS_Flyer_Screech_A, _Death, _Flap_A) when nothing loads them.
- /Game/OSArt/Audio/Mix: SC_OS_Flyer and SC_OS_Ambience (sound classes), SMIX_OS_FlyerDuck
  (the ambience class -6 dB for 0.8 s, 50 ms in, 0.6 s out - pushed by an aggro screech) and
  SM_OS_AirReverb (a submix with a reverb, into the master and its limiter: the zones with air).
"""
import glob
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_build  # noqa: E402

DEST = "/Game/OSArt/Audio/Flyer"
MIX = "/Game/OSArt/Audio/Mix"
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("OSFLYERAUDIO: " + msg)


def asset(path, name, cls, factory):
    full = f"{path}/{name}"
    if eal.does_asset_exist(full):
        return unreal.load_asset(full)
    return tools.create_asset(name, path, cls, factory)


# --- the mix ---
flyer_class = asset(MIX, "SC_OS_Flyer", unreal.SoundClass, unreal.SoundClassFactory())
ambience_class = asset(MIX, "SC_OS_Ambience", unreal.SoundClass, unreal.SoundClassFactory())
os_build.save(flyer_class, only_if_dirty=True)
os_build.save(ambience_class, only_if_dirty=True)
duck = asset(MIX, "SMIX_OS_FlyerDuck", unreal.SoundMix, unreal.SoundMixFactory())
adjuster = unreal.SoundClassAdjuster()
adjuster.set_editor_property("sound_class_object", ambience_class)
adjuster.set_editor_property("volume_adjuster", 0.5)
duck.set_editor_property("sound_class_effects", [adjuster])
duck.set_editor_property("fade_in_time", 0.05)
duck.set_editor_property("duration", 0.8)
duck.set_editor_property("fade_out_time", 0.6)
os_build.save(duck, only_if_dirty=True)

factory = unreal.SoundSubmixEffectFactory()
factory.set_editor_property("sound_effect_submix_preset_class", unreal.SubmixEffectReverbPreset)
reverb = asset(MIX, "SMX_OS_AirReverb", unreal.SubmixEffectReverbPreset, factory)
settings = reverb.get_editor_property("settings")
# A small hard space: 1.1 s, dark, no dry (the send carries only the wet).
for key, value in (("decay_time", 1.1), ("decay_hf_ratio", 0.6), ("density", 0.8), ("diffusion", 0.8), ("gain", 0.6),
                   ("gain_hf", 0.5), ("reflections_gain", 0.3), ("reflections_delay", 0.012), ("late_gain", 1.0),
                   ("late_delay", 0.02), ("air_absorption_gain_hf", 0.9), ("wet_level", 0.6), ("dry_level", 0.0)):
    try:
        settings.set_editor_property(key, value)
    except Exception as error:  # noqa: BLE001 - log what this engine version lacks, keep the rest
        log(f"reverb setting {key} not set: {error}")
reverb.set_editor_property("settings", settings)
os_build.save(reverb, only_if_dirty=True)
air = asset(MIX, "SM_OS_AirReverb", unreal.SoundSubmix, unreal.SoundSubmixFactory())
air.set_editor_property("submix_effect_chain", [reverb])
os_build.save(air, only_if_dirty=True)
log(f"mix: {flyer_class.get_name()}, {ambience_class.get_name()}, {duck.get_name()} (ambience x0.5 for 0.8 s), "
    f"{air.get_name()} <- {reverb.get_name()}")

# --- the stems ---
wav_dir = os.environ["OS_WAV_DIR"]
tasks = []
for path in sorted(glob.glob(os.path.join(wav_dir, "SW_OS_Flyer_*.wav"))):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = DEST
    task.replace_existing = True
    task.automated = True
    task.save = False
    tasks.append(task)
tools.import_asset_tasks(tasks)
count = 0
for task in tasks:
    for obj in task.imported_object_paths:
        wave = unreal.load_asset(obj)
        name = wave.get_name()
        wave.set_editor_property("sound_class_object", flyer_class)
        priority = 0.6 if any(k in name for k in ("_Flap_", "_Flutter_", "_FlyBy_")) else 0.8 if any(
            k in name for k in ("_Hit_", "_Bite_")) else 0.9
        wave.set_editor_property("priority", priority)
        wave.set_editor_property("looping", "_Flutter_" in name)
        os_build.save(wave, only_if_dirty=True)
        count += 1
        log(f"{name}: {wave.get_editor_property('duration'):.2f} s, {wave.get_editor_property('num_channels')} ch, "
            f"priority {priority}{', looping' if '_Flutter_' in name else ''}")
log(f"{count} of {len(tasks)} stems imported")

for old in ("SW_OS_Flyer_Screech_A", "SW_OS_Flyer_Death", "SW_OS_Flyer_Flap_A"):
    full = f"{DEST}/{old}"
    if eal.does_asset_exist(full):
        refs = eal.find_package_referencers_for_asset(full, False)
        if refs:
            log(f"kept {old}: referenced by {list(refs)}")
        else:
            eal.delete_asset(full)
            log(f"deleted {old}")
unreal.SystemLibrary.quit_editor()
