"""The guns' first-person meshes (editor Python, headless).

  JAM_FBX_DIR=<dir with the FBX files below> UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/jam_weapons.py" -unattended -nop4 -nosplash -nullrhi

Imports SM_Jam_Shotgun, SM_Jam_Shotgun_Pump, SM_Jam_SV98 and SM_Jam_SV98_Bolt
(Scripts/blender/jam_guns.py) into /Game/JamArt/Meshes/Weapons, makes
M_Jam_GunPart (Color, Metallic, Roughness) and its walnut / black oxide / rubber
instances, and assigns them by slot. The pistol is MindKore's USP
(/Game/MindKore/Weapons/USP, copied byte for byte, left as it is): its frame and
slide, SM_Jam_USP_Frame and SM_Jam_USP_Slide (Scripts/blender/jam_usp_split.py),
are imported next to the long guns with its material M_MK_USP_Soviet; the frame
gets SM_MK_USP_Soviet's Muzzle socket. The long guns get a Muzzle socket on the
bore at their front (the trigger origin sits 4.5 cm under the bore), X along it.
Each gun's parts together are checked against the whole gun's bounds.
Without JAM_FBX_DIR the import is skipped and only the materials and sockets are (re)made.
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


def import_gun(fbx):
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
    data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
                             if "USP" in fbx else unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx)
    task.set_editor_property("destination_path", MESHES)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    # The legacy FBX importer, as MindKore imported the USP: it honours FbxImportUI.
    flag = "Interchange.FeatureFlags.Import.FBX"
    old = unreal.SystemLibrary.get_console_variable_bool_value(flag)
    unreal.SystemLibrary.execute_console_command(None, flag + " 0")
    try:
        tools.import_asset_tasks([task])
    finally:
        unreal.SystemLibrary.execute_console_command(None, "%s %d" % (flag, int(old)))
    paths = list(task.get_editor_property("imported_object_paths"))
    if not paths:
        raise RuntimeError("nothing imported from " + fbx)
    return unreal.load_asset(paths[0])


def bounds(mesh):
    box = mesh.get_bounding_box()
    return [round(v, 2) for v in (box.min.x, box.min.y, box.min.z)], [round(v, 2) for v in (box.max.x, box.max.y, box.max.z)]


def set_muzzle(mesh, location, rotation):
    socket = mesh.find_socket("Muzzle")
    if not socket:
        socket = unreal.StaticMeshSocket(outer=mesh)
        socket.set_editor_property("socket_name", "Muzzle")
        mesh.add_socket(socket)
    socket.set_editor_property("relative_location", location)
    socket.set_editor_property("relative_rotation", rotation)


def get_gun(name):
    fbx = os.path.join(os.environ.get("JAM_FBX_DIR", ""), name + ".fbx")
    return import_gun(fbx) if os.path.isfile(fbx) else unreal.load_asset(MESHES + "/" + name)


slots = {"Walnut": part("MI_Jam_Gun_Walnut", (0.16, 0.065, 0.028), 0.0, 0.55),
         "BlackOxide": part("MI_Jam_Gun_BlackOxide", (0.035, 0.036, 0.04), 1.0, 0.38),
         "Rubber": part("MI_Jam_Gun_Rubber", (0.015, 0.015, 0.015), 0.0, 0.9)}
for name in ("SM_Jam_Shotgun", "SM_Jam_Shotgun_Pump", "SM_Jam_SV98", "SM_Jam_SV98_Bolt"):
    gun = get_gun(name)
    materials = gun.get_editor_property("static_materials")
    for index, slot in enumerate(materials):
        slot_name = str(slot.get_editor_property("material_slot_name"))
        if slot_name not in slots:
            raise RuntimeError("unexpected slot " + slot_name)
        gun.set_material(index, slots[slot_name])
    if "_" not in name[len("SM_Jam_"):]:
        # On the bore (Z 4.5 over the trigger origin), at the very front.
        set_muzzle(gun, unreal.Vector(gun.get_bounding_box().max.x, 0.0, 4.5), unreal.Rotator(0.0, 0.0, 0.0))
    eal.save_asset(MESHES + "/" + name, False)
    muzzle = gun.find_socket("Muzzle")
    log("%s: %d triangles, slots %s, bounds %s, Muzzle %s" % (
        name, gun.get_num_triangles(0), [str(s.get_editor_property("material_slot_name")) for s in materials], bounds(gun),
        muzzle.get_editor_property("relative_location") if muzzle else "none"))

usp = unreal.load_asset("/Game/MindKore/Weapons/USP/SM_MK_USP_Soviet")
usp_muzzle = usp.find_socket("Muzzle")
log("SM_MK_USP_Soviet: %d triangles, material %s, bounds %s, Muzzle %s %s" % (
    usp.get_num_triangles(0), usp.get_material(0).get_path_name(), bounds(usp),
    usp_muzzle.get_editor_property("relative_location"), usp_muzzle.get_editor_property("relative_rotation")))
usp_parts = []
for name in ("SM_Jam_USP_Frame", "SM_Jam_USP_Slide"):
    mesh = get_gun(name)
    for index in range(len(mesh.get_editor_property("static_materials"))):
        mesh.set_material(index, usp.get_material(0))
    if name.endswith("Frame"):
        set_muzzle(mesh, usp_muzzle.get_editor_property("relative_location"), usp_muzzle.get_editor_property("relative_rotation"))
    eal.save_asset(MESHES + "/" + name, False)
    usp_parts.append(mesh)
    log("%s: %d triangles, bounds %s" % (name, mesh.get_num_triangles(0), bounds(mesh)))
# The parts together must be the whole USP, in its frame (a shifted import would show here).
lo = [min(bounds(m)[0][i] for m in usp_parts) for i in range(3)]
hi = [max(bounds(m)[1][i] for m in usp_parts) for i in range(3)]
whole = bounds(usp)
log("USP parts union %s .. %s vs SM_MK_USP_Soviet %s .. %s: %s" % (lo, hi, whole[0], whole[1],
    "MATCH" if all(abs(a - b) < 0.05 for a, b in zip(lo + hi, whole[0] + whole[1])) else "MISMATCH"))
registry = unreal.AssetRegistryHelpers.get_asset_registry()
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
for package in ("/Game/MindKore/Weapons/USP/M_MK_USP_Soviet", "/Game/MindKore/Weapons/M_MK_Tracer",
                *(MESHES + "/" + n for n in ("SM_Jam_Shotgun", "SM_Jam_Shotgun_Pump", "SM_Jam_SV98", "SM_Jam_SV98_Bolt",
                                             "SM_Jam_USP_Frame", "SM_Jam_USP_Slide"))):
    deps = [str(d) for d in (registry.get_dependencies(package, options) or [])]
    missing = [d for d in deps if d.startswith("/Game/") and not eal.does_asset_exist(d)]
    log("%s depends on %s; missing %s" % (package, deps, missing or "none"))
unreal.SystemLibrary.quit_editor()
