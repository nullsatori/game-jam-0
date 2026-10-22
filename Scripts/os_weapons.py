"""The guns' first-person meshes (editor Python, headless).

  OS_FBX_DIR=<dir with the FBX files below> UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/os_weapons.py" -unattended -nop4 -nosplash -nullrhi

Imports SM_OS_Shotgun, SM_OS_Shotgun_Pump, SM_OS_SV98 and SM_OS_SV98_Bolt
(Scripts/blender/os_guns.py) into /Game/OSArt/Meshes/Weapons, makes
M_OS_GunPart (Color, Metallic, Roughness) and its walnut / black oxide / rubber
instances, and assigns them by slot. The pistol is MindKore's USP
(/Game/MindKore/Weapons/USP, copied byte for byte, left as it is): its frame and
slide, SM_OS_USP_Frame and SM_OS_USP_Slide (Scripts/blender/os_usp_split.py),
are imported next to the long guns with its material M_MK_USP_Soviet; the frame
gets SM_MK_USP_Soviet's Muzzle socket. The long guns get a Muzzle socket on the
bore at their front (the trigger origin sits 4.5 cm under the bore), X along it.
The parts for reloads and spent rounds come from the same scripts:
SM_OS_SV98_Mag, SM_OS_USP_Mag, SM_OS_USP_Hammer (the USP's material), and
the cases SM_OS_Case_9mm, SM_OS_Case_762x54R, SM_OS_Shell_12ga. Brass is
MindKore's own MI_MK_Post_Brass (copied byte for byte with its parent
M_CRTV03_Solid from MindKore's Admin art); the shell's hull is MI_OS_Shell_Hull.
The SV-98 magazine is worn parkerized steel: MI_OS_Gun_ParkerizedWorn on M_OS_GunPartWorn,
which lerps to bare steel by the vertex colour os_guns.py paints on its edges (no textures).
Each gun's parts together are checked against the whole gun's bounds.
Without OS_FBX_DIR the import is skipped and only the materials and sockets are (re)made.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_build  # noqa: E402

MESHES = "/Game/OSArt/Meshes/Weapons"
MATS = "/Game/OSArt/Materials/Weapons"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("OSGUNS: " + msg)


def base_material():
    path = MATS + "/M_OS_GunPart"
    if eal.does_asset_exist(path):
        return unreal.load_asset(path)
    m = tools.create_asset("M_OS_GunPart", MATS, unreal.Material, unreal.MaterialFactoryNew())
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
    os_build.save(path)
    return m


def part(name, color, metallic, roughness):
    path = MATS + "/" + name
    mi = unreal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, base_material())
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(*color, 1.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Metallic", metallic)
    mel.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
    os_build.save(path)
    return mi


def worn_part(name, color, worn, metallic, roughness):
    """M_OS_GunPartWorn: the plain part's three values, lerped to a worn set by vertex colour R (0 finish, 1 bare)."""
    path = MATS + "/M_OS_GunPartWorn"
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
    else:
        m = tools.create_asset("M_OS_GunPartWorn", MATS, unreal.Material, unreal.MaterialFactoryNew())
        wear = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -900, 200)
        for i, (prop, kind, a, b) in enumerate(((unreal.MaterialProperty.MP_BASE_COLOR, "vector", "Color", "WornColor"),
                                                  (unreal.MaterialProperty.MP_METALLIC, "scalar", "Metallic", "WornMetallic"),
                                                  (unreal.MaterialProperty.MP_ROUGHNESS, "scalar", "Roughness", "WornRoughness"))):
            lerp = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -300, i * 260)
            for j, (name_, pin) in enumerate(((a, "A"), (b, "B"))):
                cls = unreal.MaterialExpressionVectorParameter if kind == "vector" else unreal.MaterialExpressionScalarParameter
                param = mel.create_material_expression(m, cls, -650, i * 260 + j * 110)
                param.set_editor_property("parameter_name", name_)
                mel.connect_material_expressions(param, "", lerp, pin)
            mel.connect_material_expressions(wear, "R", lerp, "Alpha")
            mel.connect_material_property(lerp, "", prop)
        mel.recompile_material(m)
        os_build.save(path)
    path = MATS + "/" + name
    mi = unreal.load_asset(path) if eal.does_asset_exist(path) else         tools.create_asset(name, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, m)
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(*color, 1.0))
    mel.set_material_instance_vector_parameter_value(mi, "WornColor", unreal.LinearColor(*worn, 1.0))
    for key, value in zip(("Metallic", "WornMetallic", "Roughness", "WornRoughness"), metallic + roughness):
        mel.set_material_instance_scalar_parameter_value(mi, key, value)
    os_build.save(path)
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
    if "SV98_Mag" in fbx:
        data.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)  # the wear
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
    fbx = os.path.join(os.environ.get("OS_FBX_DIR", ""), name + ".fbx")
    return import_gun(fbx) if os.path.isfile(fbx) else unreal.load_asset(MESHES + "/" + name)


slots = {"Walnut": part("MI_OS_Gun_Walnut", (0.16, 0.065, 0.028), 0.0, 0.55),
         "BlackOxide": part("MI_OS_Gun_BlackOxide", (0.035, 0.036, 0.04), 1.0, 0.38),
         "Rubber": part("MI_OS_Gun_Rubber", (0.015, 0.015, 0.015), 0.0, 0.9),
         "Hull": part("MI_OS_Shell_Hull", (0.11, 0.018, 0.012), 0.0, 0.6),
         "Brass": unreal.load_asset("/Game/MindKore/Art/Admin/Post/MI_MK_Post_Brass"),
         # Worn parkerized steel: grey phosphate on the flats, bright bare steel where the vertex colour says worn.
         "Parkerized": worn_part("MI_OS_Gun_ParkerizedWorn", (0.075, 0.08, 0.075), (0.55, 0.55, 0.57), (0.5, 1.0), (0.7, 0.28))}
if not slots["Brass"]:
    raise RuntimeError("MI_MK_Post_Brass missing: copy it and M_CRTV03_Solid from MindKore's Content/MindKore/Art/Admin")
for name in ("SM_OS_Shotgun", "SM_OS_Shotgun_Pump", "SM_OS_SV98", "SM_OS_SV98_Bolt", "SM_OS_SV98_Mag", "SM_OS_USP_Mag",
             "SM_OS_Case_9mm", "SM_OS_Case_762x54R", "SM_OS_Shell_12ga"):
    gun = get_gun(name)
    materials = gun.get_editor_property("static_materials")
    if name == "SM_OS_SV98_Mag" and str(materials[0].get_editor_property("material_slot_name")) != "Parkerized":
        # A reimport keeps the old slot name (BlackOxide before 18.10's worn finish).
        slot = materials[0]
        slot.set_editor_property("material_slot_name", "Parkerized")
        materials[0] = slot
        gun.set_editor_property("static_materials", materials)
        materials = gun.get_editor_property("static_materials")
    for index, slot in enumerate(materials):
        slot_name = str(slot.get_editor_property("material_slot_name"))
        if slot_name not in slots:
            raise RuntimeError("unexpected slot " + slot_name)
        gun.set_material(index, slots[slot_name])
    if name.startswith(("SM_OS_Case", "SM_OS_Shell", "SM_OS_USP_Mag", "SM_OS_SV98_Mag")):
        # Loose in the world (UOSWeaponsComponent::SpawnDebris): a simulated body needs a collision shape.
        meshes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        meshes.remove_collisions(gun)
        meshes.add_simple_collisions(gun, unreal.ScriptCollisionShapeType.BOX)
    if "_" not in name[len("SM_OS_"):]:
        # On the bore (Z 4.5 over the trigger origin), at the very front.
        set_muzzle(gun, unreal.Vector(gun.get_bounding_box().max.x, 0.0, 4.5), unreal.Rotator(0.0, 0.0, 0.0))
    os_build.save(MESHES + "/" + name)
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
for name in ("SM_OS_USP_Frame", "SM_OS_USP_Slide", "SM_OS_USP_Hammer"):
    mesh = get_gun(name)
    for index in range(len(mesh.get_editor_property("static_materials"))):
        mesh.set_material(index, usp.get_material(0))
    if name.endswith("Frame"):
        set_muzzle(mesh, usp_muzzle.get_editor_property("relative_location"), usp_muzzle.get_editor_property("relative_rotation"))
    os_build.save(MESHES + "/" + name)
    if not name.endswith("Hammer"):  # the hammer sits on its pin, not in the gun's frame
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
                *(MESHES + "/" + n for n in ("SM_OS_Shotgun", "SM_OS_Shotgun_Pump", "SM_OS_SV98", "SM_OS_SV98_Bolt",
                                             "SM_OS_USP_Frame", "SM_OS_USP_Slide"))):
    deps = [str(d) for d in (registry.get_dependencies(package, options) or [])]
    missing = [d for d in deps if d.startswith("/Game/") and not eal.does_asset_exist(d)]
    log("%s depends on %s; missing %s" % (package, deps, missing or "none"))
unreal.SystemLibrary.quit_editor()
