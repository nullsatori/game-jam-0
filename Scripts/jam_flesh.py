"""The gritty flesh set: textures and materials (editor Python, shared by the map builders).

Textures: the owner's own AI-generated set (CREDITS.md), the 256 px versions
(the 2048 px copies are exact 8x nearest-neighbour upscales of them), imported
once into /Game/JamArt/Textures/Flesh as T_Jam_Gritty_*. Filter Nearest keeps
the retro texel; mips are kept (point-sampled, mip selection too), so a far
surface steps down to coarser texels instead of shimmering. Normal maps
T_Jam_Gritty_{Muscle,Tendon,PlanetSkin}_N come from Scripts/make_flesh_normals.py.

  import_textures(src_dir, normals_dir, log) - once, from the owner's lowres_256 folder
  materials(log) - the master M_Jam_Gritty and its instances, graph and numbers
      rebuilt in place on every call.

M_Jam_Gritty: lit, base colour = BaseColor x Tint (+ a dark vessel overlay from
the Vessels mask), normal map x NormalStrength, roughness from the Wetness
mask (wet = glossy), emissive = colour x SelfLight. Static switches:
  Triplanar - world-aligned projection (TriplanarCm per tile): the planetoid,
      which has no UVs;
  LivingPieces - the mesh living wall's pieces: UV1 is the piece's cell in a
      CellsX x CellsZ grid, PieceData (a transient texture AJamLivingWall
      writes) holds growth (R), the side it regrows from (G, B as -1..1) and
      twitch (A); a piece at growth g is scaled by g toward the middle of that
      side. UV1 outside 0..1 (the frame) is never touched.
The membrane instance overrides the blend mode to translucent, two-sided.
"""
import os

import unreal

TEX = "/Game/JamArt/Textures/Flesh"
MATS = "/Game/JamArt/Materials/Flesh"
BASE = MATS + "/M_Jam_Gritty"

# Owner's file names (lowres_256 relative path) -> kind.
SOURCE = {
    "maps/T_Jam_Gritty_Muscle_BC.png": "colour",
    "maps/T_Jam_Gritty_Tendon_BC.png": "colour",
    "maps/T_Jam_Gritty_Vessels_BC.png": "colour",
    "maps/T_Jam_Gritty_MuscleEnd_BC.png": "colour",
    "maps/T_Jam_Gritty_Membrane_BC.png": "colour",
    "maps/T_Jam_Gritty_PlanetSkin_BC.png": "colour",
    "maps/T_Jam_Gritty_PlanetEye_BC.png": "colour",
    "maps/T_Jam_Gritty_Suckers_BC.png": "colour",
    "maps/T_Jam_Gritty_Vessels_Mask.png": "mask",
    "maps/T_Jam_Gritty_Wetness_Mask.png": "mask",
}
NORMALS = ["T_Jam_Gritty_Muscle_N.png", "T_Jam_Gritty_Tendon_N.png", "T_Jam_Gritty_PlanetSkin_N.png"]

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def tex(name):
    return unreal.load_asset("%s/T_Jam_Gritty_%s" % (TEX, name))


def _settings(texture, kind):
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    if kind == "normal":
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
    elif kind == "mask":
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        texture.set_editor_property("srgb", False)
    else:
        texture.set_editor_property("srgb", True)


def import_textures(src_dir, normals_dir, log):
    files = [(os.path.join(src_dir, rel), kind) for rel, kind in SOURCE.items()]
    files += [(os.path.join(normals_dir, name), "normal") for name in NORMALS]
    tasks = []
    for path, kind in files:
        if not os.path.isfile(path):
            raise RuntimeError("missing " + path)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", path)
        task.set_editor_property("destination_path", TEX)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        tasks.append((task, kind))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for t, _ in tasks])
    for task, kind in tasks:
        for path in task.get_editor_property("imported_object_paths"):
            texture = unreal.load_asset(path)
            _settings(texture, kind)
            eal.save_asset(path.split(".")[0], False)
            log("imported %s (%s) %dx%d" % (path, kind, texture.blueprint_get_size_x(), texture.blueprint_get_size_y()))


# --- the master -------------------------------------------------------------------------

DEFAULT_SCALARS = {
    "UTile": 1.0, "VTile": 1.0, "WetTiling": 1.0, "VesselTiling": 1.0, "VesselAmount": 0.0,
    "RoughDry": 0.75, "RoughWet": 0.22, "Specular": 0.6, "Metallic": 0.0, "NormalStrength": 1.0,
    "SelfLight": 0.15, "Opacity": 0.55, "TriplanarCm": 400.0, "VesselTriplanarScale": 4.0,
    "WallSizeCm": 400.0, "Cells": 10.0, "TwitchCm": 1.5, "PulseCm": 0.0, "PulseSpeed": 1.7,
}
DEFAULT_VECTORS = {"Tint": (1.0, 1.0, 1.0), "VesselColor": (0.35, 0.08, 0.12)}
WAT = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture"
WAN = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedNormal"


def _master(log):
    if eal.does_asset_exist(BASE):
        m = unreal.load_asset(BASE)
        mel.delete_all_material_expressions(m)
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Jam_Gritty", MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_instanced_static_meshes", True)
    m.set_editor_property("phys_material", unreal.load_asset("/Game/MindKore/Materials/Physical/PM_MK_Flesh"))
    y = [0]
    ST = unreal.MaterialSamplerType

    def node(cls, **props):
        y[0] += 110
        n = mel.create_material_expression(m, cls, -1800, y[0])
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(a, b, pin="", out=""):
        if not mel.connect_material_expressions(a, out, b, pin):
            raise RuntimeError("link failed: %s.%s -> %s.%s" % (a.get_name(), out, b.get_name(), pin))

    def scalar(name):
        return node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=DEFAULT_SCALARS[name])

    def vector(name):
        r, g, b = DEFAULT_VECTORS[name]
        return node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(r, g, b, 1))

    def op(cls, a, b=None, const_b=None, out_a=""):
        n = node(cls)
        link(a, n, "A", out_a)
        if b is not None:
            link(b, n, "B")
        elif const_b is not None:
            n.set_editor_property("const_b", const_b)
        return n

    def lerp(a, b, alpha):
        n = node(unreal.MaterialExpressionLinearInterpolate)
        link(a, n, "A")
        link(b, n, "B")
        link(alpha, n, "Alpha")
        return n

    def append(a, b):
        n = node(unreal.MaterialExpressionAppendVector)
        link(a, n, "A")
        link(b, n, "B")
        return n

    def mask(src, **channels):
        n = node(unreal.MaterialExpressionComponentMask, r=channels.get("r", False), g=channels.get("g", False),
                 b=channels.get("b", False), a=channels.get("a", False))
        link(src, n)
        return n

    def switch(name, on, off, on_out=""):
        n = node(unreal.MaterialExpressionStaticSwitchParameter, parameter_name=name, default_value=False)
        link(on, n, "True", on_out)
        link(off, n, "False")
        return n

    def tex_object(name, path, sampler):
        return node(unreal.MaterialExpressionTextureObjectParameter, parameter_name=name, texture=unreal.load_asset(path), sampler_type=sampler)

    def sample(obj, uv, sampler):
        n = node(unreal.MaterialExpressionTextureSample, sampler_type=sampler)
        link(obj, n, "Tex")
        link(uv, n, "UVs")
        return n

    def world_aligned(fn, obj, size):
        n = node(unreal.MaterialExpressionMaterialFunctionCall, material_function=unreal.load_asset(fn))
        link(obj, n, "TextureObject")
        link(size, n, "TextureSize")
        return n

    mul, add, sub = unreal.MaterialExpressionMultiply, unreal.MaterialExpressionAdd, unreal.MaterialExpressionSubtract
    # UVs (tiled) and the triplanar tile size (cm).
    uv = op(mul, node(unreal.MaterialExpressionTextureCoordinate), append(scalar("UTile"), scalar("VTile")))
    tri = scalar("TriplanarCm")
    tri3 = append(append(tri, tri), tri)
    bc_obj = tex_object("BaseColor", TEX + "/T_Jam_Gritty_Muscle_BC", ST.SAMPLERTYPE_COLOR)
    n_obj = tex_object("Normal", TEX + "/T_Jam_Gritty_Muscle_N", ST.SAMPLERTYPE_NORMAL)
    wet_obj = tex_object("Wetness", TEX + "/T_Jam_Gritty_Wetness_Mask", ST.SAMPLERTYPE_LINEAR_GRAYSCALE)
    ves_obj = tex_object("Vessels", TEX + "/T_Jam_Gritty_Vessels_Mask", ST.SAMPLERTYPE_LINEAR_GRAYSCALE)
    xyz = "XYZ Texture"
    bc = switch("Triplanar", world_aligned(WAT, bc_obj, tri3), sample(bc_obj, uv, ST.SAMPLERTYPE_COLOR), xyz)
    nrm = switch("Triplanar", world_aligned(WAN, n_obj, tri3), sample(n_obj, uv, ST.SAMPLERTYPE_NORMAL), xyz)
    wet = switch("Triplanar", world_aligned(WAT, wet_obj, tri3),
                 sample(wet_obj, op(mul, uv, scalar("WetTiling")), ST.SAMPLERTYPE_LINEAR_GRAYSCALE), xyz)
    ves = switch("Triplanar", world_aligned(WAT, ves_obj, op(mul, tri3, scalar("VesselTriplanarScale"))),
                 sample(ves_obj, op(mul, uv, scalar("VesselTiling")), ST.SAMPLERTYPE_LINEAR_GRAYSCALE), xyz)

    colour = op(mul, mask(bc, r=True, g=True, b=True), vector("Tint"))
    vessel = op(mul, mask(ves, r=True), scalar("VesselAmount"))
    colour = lerp(colour, op(mul, colour, vector("VesselColor")), vessel)
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(lerp(scalar("RoughDry"), scalar("RoughWet"), mask(wet, r=True)), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(scalar("Specular"), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(scalar("Metallic"), "", unreal.MaterialProperty.MP_METALLIC)
    flat = node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0, 0, 1, 0))
    normal = node(unreal.MaterialExpressionNormalize)
    link(lerp(flat, mask(nrm, r=True, g=True, b=True), scalar("NormalStrength")), normal)
    mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(op(mul, colour, scalar("SelfLight")), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(scalar("Opacity"), "", unreal.MaterialProperty.MP_OPACITY)

    # World position offset: breathing (all), plus the living wall's pieces (switch).
    phase = node(unreal.MaterialExpressionDotProduct)
    link(node(unreal.MaterialExpressionWorldPosition), phase, "A")
    link(node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0.004, 0.006, 0.005, 0)), phase, "B")
    breath = node(unreal.MaterialExpressionSine)
    link(op(add, op(mul, node(unreal.MaterialExpressionTime), scalar("PulseSpeed")), phase), breath)
    swell = op(mul, op(mul, node(unreal.MaterialExpressionVertexNormalWS), breath), scalar("PulseCm"))

    uv1 = node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=1)
    data_obj = tex_object("PieceData", "/Engine/EngineResources/WhiteSquareTexture", ST.SAMPLERTYPE_LINEAR_COLOR)
    data = sample(data_obj, uv1, ST.SAMPLERTYPE_LINEAR_COLOR)
    # The frame (UV1 = -1) is always whole.
    frame = node(unreal.MaterialExpressionSaturate)
    link(op(mul, mask(uv1, r=True), const_b=-1000.0), frame)
    growth = node(unreal.MaterialExpressionMax)
    link(mask(data, r=True), growth, "A")
    link(frame, growth, "B")
    size = scalar("WallSizeCm")
    pivot = op(mul, op(add, uv1, const_b=-0.5), size)                       # (x, z) in wall space, cm
    side = op(add, op(mul, mask(data, g=True, b=True), const_b=2.0), const_b=-1.0)
    half_cell = op(mul, op(unreal.MaterialExpressionDivide, size, scalar("Cells")), const_b=0.5)
    anchor = op(add, pivot, op(mul, side, half_cell))
    zero = node(unreal.MaterialExpressionConstant, r=0.0)
    anchor3 = append(append(mask(anchor, r=True), zero), mask(anchor, g=True))
    local = node(unreal.MaterialExpressionTransformPosition,
                 transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                 transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    link(node(unreal.MaterialExpressionWorldPosition), local)
    shrink = op(mul, op(sub, anchor3, local), op(sub, node(unreal.MaterialExpressionConstant, r=1.0), growth))
    # Twitch of a torn rim: along the wall normal (local Y), each piece out of phase.
    jerk_phase = node(unreal.MaterialExpressionDotProduct)
    link(pivot, jerk_phase, "A")
    link(node(unreal.MaterialExpressionConstant2Vector, r=0.13, g=0.07), jerk_phase, "B")
    jerk = node(unreal.MaterialExpressionSine)
    link(op(add, op(mul, node(unreal.MaterialExpressionTime), const_b=37.0), jerk_phase), jerk)
    jerk_y = op(mul, op(mul, jerk, mask(data, a=True)), scalar("TwitchCm"))
    twitch = append(append(zero, jerk_y), zero)
    to_world = node(unreal.MaterialExpressionTransform,
                    transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                    transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(op(add, shrink, twitch), to_world)
    pieces = op(add, to_world, swell)
    mel.connect_material_property(switch("LivingPieces", pieces, swell), "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(m)
    eal.save_asset(BASE, False)
    log("built " + BASE)


def _instance(name, parent, scalars=None, vectors=None, textures=None, switches=None, translucent=False):
    path = MATS + "/" + name
    if eal.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATS, unreal.MaterialInstanceConstant,
                                                                     unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, (r, g, b) in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(r, g, b, 1.0))
    for k, v in (textures or {}).items():
        mel.set_material_instance_texture_parameter_value(mi, k, unreal.load_asset(v))
    for k, v in (switches or {}).items():
        mel.set_material_instance_static_switch_parameter_value(mi, k, v)
    if translucent:
        o = mi.get_editor_property("base_property_overrides")
        o.set_editor_property("override_blend_mode", True)
        o.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        o.set_editor_property("override_two_sided", True)
        o.set_editor_property("two_sided", True)
        mi.set_editor_property("base_property_overrides", o)
    mel.update_material_instance(mi)
    eal.save_asset(path, False)
    return mi


def _t(name):
    return TEX + "/T_Jam_Gritty_" + name


RUST_BC = "/Game/MindKore/Art/Admin/Yard/Props2/Textures/T_MK_Yard_Rust_BC"
RUST_N = "/Game/MindKore/Art/Admin/Yard/Props2/Textures/T_MK_Yard_Rust_N"

# look: (scalars, vectors, textures). Tuned on rendered frames; the texel look kept (Nearest).
LOOKS = {
    "Muscle": ({"VesselAmount": 0.5, "VesselTiling": 0.5, "NormalStrength": 0.8, "SelfLight": 0.18},
               {"Tint": (1.25, 1.0, 1.0)}, {"BaseColor": _t("Muscle_BC"), "Normal": _t("Muscle_N")}),
    "Tendon": ({"NormalStrength": 0.7, "RoughDry": 0.6, "SelfLight": 0.12}, {"Tint": (1.05, 1.0, 0.95)},
               {"BaseColor": _t("Tendon_BC"), "Normal": _t("Tendon_N")}),
    "Vessel": ({"NormalStrength": 0.5, "SelfLight": 0.15}, {"Tint": (0.6, 0.22, 0.28)},
               {"BaseColor": _t("Muscle_BC"), "Normal": _t("Muscle_N")}),
    "Membrane": ({"NormalStrength": 0.3, "Opacity": 0.5, "SelfLight": 0.25, "RoughDry": 0.35}, {"Tint": (1.1, 0.95, 0.9)},
                 {"BaseColor": _t("Membrane_BC"), "Normal": _t("Tendon_N")}),
    "MuscleEnd": ({"NormalStrength": 0.0, "SelfLight": 0.18}, {"Tint": (1.2, 1.0, 1.0)}, {"BaseColor": _t("MuscleEnd_BC")}),
    "FrameMetal": ({"Metallic": 0.7, "RoughDry": 0.75, "RoughWet": 0.45, "Specular": 0.5, "SelfLight": 0.0},
                   {"Tint": (0.9, 0.85, 0.8)}, {"BaseColor": RUST_BC, "Normal": RUST_N}),
    "PlanetSkin": ({"TriplanarCm": 500.0, "VesselAmount": 0.45, "VesselTriplanarScale": 5.0, "NormalStrength": 0.9, "SelfLight": 0.15},
                   {"Tint": (1.1, 0.95, 0.95)}, {"BaseColor": _t("PlanetSkin_BC"), "Normal": _t("PlanetSkin_N")}),
    "PlanetEye": ({"NormalStrength": 0.0, "SelfLight": 1.2, "RoughWet": 0.1, "RoughDry": 0.1}, {}, {"BaseColor": _t("PlanetEye_BC")}),
    "Suckers": ({"NormalStrength": 0.6, "SelfLight": 0.15, "VTile": 3.0}, {"Tint": (1.1, 0.95, 0.95)},
                {"BaseColor": _t("Suckers_BC"), "Normal": _t("PlanetSkin_N")}),
}
# The mesh living wall's slots: the same looks with LivingPieces on.
WALL_SLOTS = ["Muscle", "Tendon", "Vessel", "Membrane", "MuscleEnd"]


def materials(log):
    """Make (once) the master and (every call) the instances; returns {look: MI} plus {"Wall_<look>": MI}."""
    _master(log)  # the graph is rebuilt in place on every call
    base = unreal.load_asset(BASE)
    out = {}
    for name, (scalars, vectors, textures) in LOOKS.items():
        out[name] = _instance("MI_Jam_" + name, base, scalars, vectors, textures,
                              {"Triplanar": name == "PlanetSkin"}, translucent=name == "Membrane")
    for name in WALL_SLOTS:
        scalars, vectors, textures = LOOKS[name]
        out["Wall_" + name] = _instance("MI_Jam_Wall_" + name, base, dict(scalars, PulseCm=0.5), vectors, textures,
                                        {"LivingPieces": True}, translucent=name == "Membrane")
    log("flesh materials: %s" % ", ".join(sorted(out)))
    return out
