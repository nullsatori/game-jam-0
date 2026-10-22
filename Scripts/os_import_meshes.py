"""Import the Blender-made meshes into /Game/OSArt/Meshes (editor Python, headless).

  OS_FBX_DIR=<dir with the FBX files> UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/os_import_meshes.py" -unattended -nop4 -nosplash -nullrhi

The FBX files come from Scripts/blender/*.py (os_planet.py, os_living_wall_mesh.py)
and are not kept in the repository; the imported .uasset are. Each mesh is
imported without materials (build scripts assign them by slot), without
lightmap UVs, Nanite off; collision is the render mesh itself (complex as simple:
the planet is walked on, its caves too). For the planet the script checks the
axes: every surface spot of Scripts/planet_layout.json must have a vertex
within 1.2 m (the FBX round trip mirrors Y; os_planet.py pre-mirrors).
"""
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_build  # noqa: E402

FOLDERS = {"SM_OS_Planet": "/Game/OSArt/Meshes/Planet", "SM_OS_PlanetEye": "/Game/OSArt/Meshes/Planet",
           "SM_OS_Tentacle": "/Game/OSArt/Meshes/Planet",
           "SM_OS_LivingWall_Square": "/Game/OSArt/Meshes/LivingWall", "SM_OS_LivingWall_Round": "/Game/OSArt/Meshes/LivingWall"}
# The flyer's parts (Scripts/blender/os_flyer.py; Scripts/os_flyer_art.py gives them their material).
FOLDERS.update({n: "/Game/OSArt/Meshes/Flyer" for n in ("SM_OS_Flyer_Body", "SM_OS_Flyer_Wing", "SM_OS_Flyer_Tail", "SM_OS_Flyer_TailTip")})
SCRIPTS = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts")


def log(msg):
    unreal.log("OSMESH: " + msg)


def import_fbx(path, folder):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    data = ui.get_editor_property("static_mesh_import_data")
    data.set_editor_property("combine_meshes", True)
    data.set_editor_property("generate_lightmap_u_vs", False)
    data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("build_nanite", False)
    data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = list(task.get_editor_property("imported_object_paths"))
    if not paths:
        raise RuntimeError("nothing imported from " + path)
    mesh = unreal.load_asset(paths[0])
    body = mesh.get_editor_property("body_setup")
    body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    os_build.save(paths[0].split(".")[0])
    box = mesh.get_bounding_box()
    log("imported %s: %d triangles, %d material slots, bounds %s .. %s" % (
        paths[0], mesh.get_num_triangles(0), len(mesh.get_editor_property("static_materials")), box.min, box.max))
    return mesh


def check_planet_axes(mesh):
    with open(os.path.join(SCRIPTS, "planet_layout.json"), encoding="utf-8") as f:
        layout = json.load(f)
    desc = mesh.get_static_mesh_description(0)
    verts = [desc.get_vertex_position(unreal.VertexID(i)) for i in range(desc.get_vertex_count())]
    near_all, mirrored_all = [], []
    for name, spot in layout["spots"].items():
        x, y, z = spot["pos"]
        near_all.append(min(((v.x - x) ** 2 + (v.y - y) ** 2 + (v.z - z) ** 2) ** 0.5 for v in verts))
        mirrored_all.append(min(((v.x - x) ** 2 + (v.y + y) ** 2 + (v.z - z) ** 2) ** 0.5 for v in verts))
    log("axes check over %d surface spots: nearest vertex at most %.0f cm away (the Y-mirrored spots: up to %.0f cm)" % (
        len(near_all), max(near_all), max(mirrored_all)))
    if max(near_all) > 120.0:
        raise RuntimeError("planet axes do not match planet_layout.json")


fbx_dir = os.environ.get("OS_FBX_DIR", "")
names = [n for n in FOLDERS if os.path.isfile(os.path.join(fbx_dir, n + ".fbx"))]
if not names:
    raise RuntimeError("no known FBX in OS_FBX_DIR=" + fbx_dir)
for name in names:
    mesh = import_fbx(os.path.join(fbx_dir, name + ".fbx"), FOLDERS[name])
    if name == "SM_OS_Planet":
        check_planet_axes(mesh)
