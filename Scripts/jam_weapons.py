"""The guns' first-person meshes (editor Python, headless).

  JAM_FBX_DIR=<dir with SM_Jam_Shotgun.fbx> UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/jam_weapons.py" -unattended -nop4 -nosplash -nullrhi

Imports SM_Jam_Shotgun (from Scripts/blender/jam_shotgun.py) into
/Game/JamArt/Meshes/Weapons, makes M_Jam_GunPart (Color, Metallic, Roughness) and its
walnut / black oxide / rubber instances, and assigns them by slot. The pistol is
MindKore's USP (/Game/MindKore/Weapons/USP, copied byte for byte): only read here,
its bounds and Muzzle socket logged for UJamWeaponsComponent's view offsets.
Without JAM_FBX_DIR the import is skipped and only the materials are (re)made.
"""
import os

import unreal

MESHES = "/Game/JamArt/Meshes/Weapons"
MATS = "/Game/JamArt/Materials/Weapons"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("JAMGUNS: " + msg)


def base_material():
    path = MATS + "/M_Jam_GunPart"
    if eal.does_asset_exist(path):
        return unreal.load_asset(path)
    m = tools.create_asset("M_Jam_GunPart", MATS, unreal.Material, unreal.MaterialFactoryNew())
    color = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -400, -100)
    color.set_editor_property("parameter_name", "Color")
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for i, (name, prop, value) in enumerate((("Metallic", unreal.MaterialProperty.MP_METALLIC, 0.0),
                                             ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS, 0.5))):
        scalar = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -400, 100 + i * 120)
        scalar.set_editor_property("parameter_name", name)
        scalar.set_editor_property("default_value", value)
        mel.connect_material_property(scalar, "", prop)
    mel.recompile_material(m)
    eal.save_asset(path, False)
    return m


def part(name, color, metallic, roughness):
    path = MATS + "/" + name
    mi = unreal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, base_material())
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(*color, 1.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Metallic", metallic)
    mel.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
    eal.save_asset(path, False)
    return mi


def import_shotgun(fbx):
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
    task.set_editor_property("filename", fbx)
    task.set_editor_property("destination_path", MESHES)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    tools.import_asset_tasks([task])
    paths = list(task.get_editor_property("imported_object_paths"))
    if not paths:
        raise RuntimeError("nothing imported from " + fbx)
    return unreal.load_asset(paths[0])


fbx = os.path.join(os.environ.get("JAM_FBX_DIR", ""), "SM_Jam_Shotgun.fbx")
gun = import_shotgun(fbx) if os.path.isfile(fbx) else unreal.load_asset(MESHES + "/SM_Jam_Shotgun")
slots = {"Walnut": part("MI_Jam_Gun_Walnut", (0.16, 0.065, 0.028), 0.0, 0.55),
         "BlackOxide": part("MI_Jam_Gun_BlackOxide", (0.035, 0.036, 0.04), 1.0, 0.38),
         "Rubber": part("MI_Jam_Gun_Rubber", (0.015, 0.015, 0.015), 0.0, 0.9)}
materials = gun.get_editor_property("static_materials")
for index, slot in enumerate(materials):
    name = str(slot.get_editor_property("material_slot_name"))
    if name not in slots:
        raise RuntimeError("unexpected slot " + name)
    gun.set_material(index, slots[name])
eal.save_asset(MESHES + "/SM_Jam_Shotgun", False)
box = gun.get_bounding_box()
log("SM_Jam_Shotgun: %d triangles, slots %s, bounds %s .. %s" % (
    gun.get_num_triangles(0), [str(s.get_editor_property("material_slot_name")) for s in materials], box.min, box.max))

usp = unreal.load_asset("/Game/MindKore/Weapons/USP/SM_MK_USP_Soviet")
box = usp.get_bounding_box()
muzzle = usp.find_socket("Muzzle")
log("SM_MK_USP_Soviet: %d triangles, material %s, bounds %s .. %s, Muzzle %s" % (
    usp.get_num_triangles(0), usp.get_material(0).get_path_name(), box.min, box.max,
    "%s %s" % (muzzle.relative_location, muzzle.relative_rotation) if muzzle else "none"))
registry = unreal.AssetRegistryHelpers.get_asset_registry()
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
for package in ("/Game/MindKore/Weapons/USP/SM_MK_USP_Soviet", "/Game/MindKore/Weapons/USP/M_MK_USP_Soviet",
                "/Game/MindKore/Weapons/M_MK_Tracer", MESHES + "/SM_Jam_Shotgun"):
    deps = [str(d) for d in (registry.get_dependencies(package, options) or [])]
    missing = [d for d in deps if d.startswith("/Game/") and not eal.does_asset_exist(d)]
    log("%s depends on %s; missing %s" % (package, deps, missing or "none"))
unreal.SystemLibrary.quit_editor()
