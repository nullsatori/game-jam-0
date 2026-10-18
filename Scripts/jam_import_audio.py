"""Import the guns' waves (editor Python, headless).

  JAM_WAV_DIR=<out dir of Scripts/jam_space_shots.py> UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/jam_import_audio.py" -unattended -nop4 -nosplash -nullrhi

Every SW_Jam_*.wav in JAM_WAV_DIR becomes a SoundWave in /Game/JamArt/Audio/Weapons
(replacing the one there), saved; each is logged with its duration and channels.
"""
import glob
import os

import unreal

DEST = "/Game/JamArt/Audio/Weapons"
wav_dir = os.environ["JAM_WAV_DIR"]
tasks = []
for path in sorted(glob.glob(os.path.join(wav_dir, "SW_Jam_*.wav"))):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = DEST
    task.replace_existing = True
    task.automated = True
    task.save = True
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
for task in tasks:
    for obj in task.imported_object_paths:
        wave = unreal.load_asset(obj)
        unreal.log(f"JAMAUDIO: {obj} {wave.get_editor_property('duration'):.2f} s, "
                   f"{wave.get_editor_property('num_channels')} ch")
unreal.log(f"JAMAUDIO: {sum(len(t.imported_object_paths) for t in tasks)} of {len(tasks)} imported")
unreal.SystemLibrary.quit_editor()
