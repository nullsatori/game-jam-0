"""The flyer's look: its colour atlas and material (editor Python, headless).

  OS_FBX_DIR=<out dir of Scripts/blender/os_flyer.py> UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/os_flyer_art.py" -unattended -nop4 -nosplash -nullrhi

Run after Scripts/os_import_meshes.py has imported the four SM_OS_Flyer_* meshes
from the same folder. Imports T_OS_Flyer_BC.png (256 px, painted by the Blender
script) into /Game/OSArt/Textures/Flyer with the retro texel (Nearest), makes
MI_OS_Flyer - an instance of the flesh master M_OS_Gritty (Scripts/os_flesh.py,
not rebuilt here), so a round in the flyer hits flesh (PM_MK_Flesh) - and gives
every flyer mesh that material in its only slot. AOSFlyer uses the meshes as they are.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else
                os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_build  # noqa: E402
import os_flesh  # noqa: E402

TEX = "/Game/OSArt/Textures/Flyer"
MESHES = "/Game/OSArt/Meshes/Flyer"
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("OSFLYERART: " + msg)


fbx_dir = os.environ.get("OS_FBX_DIR", "")
png = os.path.join(fbx_dir, "T_OS_Flyer_BC.png")
if not os.path.isfile(png):
    raise RuntimeError("no T_OS_Flyer_BC.png in OS_FBX_DIR=" + fbx_dir)
task = unreal.AssetImportTask()
task.set_editor_property("filename", png)
task.set_editor_property("destination_path", TEX)
task.set_editor_property("replace_existing", True)
task.set_editor_property("automated", True)
task.set_editor_property("save", False)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
texture_path = list(task.get_editor_property("imported_object_paths"))[0]
texture = unreal.load_asset(texture_path)
os_flesh._settings(texture, "colour")
os_build.save(texture_path.split(".")[0])
log("imported %s %dx%d" % (texture_path, texture.blueprint_get_size_x(), texture.blueprint_get_size_y()))

# The painted atlas carries the look: no vessel web, no macro layer, a light leathery normal;
# self light 0.3 (tuned on stills: at 0.12 it was a black cut-out against the sky).
mi = os_flesh._instance("MI_OS_Flyer", unreal.load_asset(os_flesh.BASE),
                         {"VesselAmount": 0.0, "NormalStrength": 0.4, "MacroNormalStrength": 0.0, "SelfLight": 0.3,
                          "RoughDry": 0.7, "RoughWet": 0.3, "Specular": 0.4, "WetTiling": 3.0},
                         {"Tint": (1.0, 1.0, 1.0)},
                         {"BaseColor": TEX + "/T_OS_Flyer_BC", "Normal": os_flesh.TEX + "/T_OS_Gritty_Muscle_N"})
for name in ("SM_OS_Flyer_Body", "SM_OS_Flyer_Wing", "SM_OS_Flyer_Tail", "SM_OS_Flyer_TailTip"):
    mesh = unreal.load_asset(MESHES + "/" + name)
    mesh.set_material(0, mi)
    os_build.save(MESHES + "/" + name)
    box = mesh.get_bounding_box()
    log("%s: %d triangles, MI_OS_Flyer, bounds %s .. %s" % (name, mesh.get_num_triangles(0), box.min, box.max))
