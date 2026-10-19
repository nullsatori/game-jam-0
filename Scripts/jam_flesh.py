"""The gritty flesh set: textures and materials (editor Python, shared by the map builders).

Textures: the owner's own AI-generated set (CREDITS.md), the 256 px versions
(the 2048 px copies are exact 8x nearest-neighbour upscales of them), imported
once into /Game/JamArt/Textures/Flesh as T_Jam_Gritty_*. Filter Nearest keeps
the retro texel; mips are kept (point-sampled, mip selection too), so a far
surface steps down to coarser texels instead of shimmering. Normal maps
T_Jam_Gritty_*_N (1024 px, smooth filtering, every flesh map) come from
Scripts/make_flesh_normals.py.

  import_textures(src_dir, normals_dir, log) - once, from the owner's lowres_256 folder
  materials(log, rebuild=False) - the master M_Jam_Gritty (made once; its graph
      rebuilt in place only with rebuild=True, from a fresh editor) and its
      instances, whose numbers are set on every call.

M_Jam_Gritty: lit, base colour = BaseColor x Tint (+ a dark vessel overlay from
the Vessels mask), normal map x NormalStrength, roughness from the Wetness
mask (wet = glossy), emissive = colour x SelfLight. Static switches:
  Triplanar - world-aligned projection (TriplanarCm per tile): the planetoid,
      which has no UVs;
  LivingHoles - the mesh living wall: AJamLivingWall writes its holes (up to 6:
      a ragged outline closing unevenly, HOLE_HLSL - the same function its
      collision cells follow) into HoleNA/B/C; the material cuts them out with a
      fibrous edge (the vessel mask, panning while the hole closes), a dark wet
      rim band and a twitch of the rim along the wall normal. The wall
      instances are masked; their membrane is translucent.
The membrane instances override the blend mode to translucent, two-sided.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import jam_build  # noqa: E402

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
NORMALS = ["T_Jam_Gritty_%s_N.png" % n for n in ("Muscle", "Tendon", "PlanetSkin", "Membrane", "Suckers", "PlanetEye", "MuscleEnd")]

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def tex(name):
    return unreal.load_asset("%s/T_Jam_Gritty_%s" % (TEX, name))


def _settings(texture, kind):
    # Colour and masks: the retro texel (Nearest). Normals: smooth (the texture group's
    # filtering) so the volume reads instead of stair-steps.
    texture.set_editor_property("filter", unreal.TextureFilter.TF_DEFAULT if kind == "normal" else unreal.TextureFilter.TF_NEAREST)
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


def import_textures(src_dir, normals_dir, log, normals_only=False):
    files = [] if normals_only else [(os.path.join(src_dir, rel), kind) for rel, kind in SOURCE.items()]
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
            jam_build.save(path.split(".")[0])
            log("imported %s (%s) %dx%d" % (path, kind, texture.blueprint_get_size_x(), texture.blueprint_get_size_y()))


# --- the master -------------------------------------------------------------------------
DEFAULT_SCALARS = {
    "UTile": 1.0, "VTile": 1.0, "WetTiling": 1.0, "VesselTiling": 1.0, "VesselAmount": 0.0,
    "RoughDry": 0.75, "RoughWet": 0.22, "Specular": 0.6, "Metallic": 0.0, "NormalStrength": 1.0,
    "SelfLight": 0.15, "Opacity": 0.55, "TriplanarCm": 400.0, "VesselTriplanarScale": 4.0,
    "TwitchCm": 1.5, "PulseCm": 0.0, "PulseSpeed": 1.7,
    # Living wall holes (LivingHoles): edge noise tile, jaggedness (share of the radius), pan per second,
    # the cut ramp, the dark wet rim band and its darkness.
    "HoleNoiseCm": 90.0, "HoleJag": 0.4, "HolePanX": 0.035, "HolePanZ": 0.05, "HoleSoftCm": 1.5,
    "HoleRimCm": 14.0, "HoleRimDark": 0.35,
    # Macro layers (all looks): scale of the large normal and its strength.
    "MacroTile": 0.25, "MacroTriplanarScale": 6.0, "MacroNormalStrength": 1.0,
    # The planet look (PlanetLook): macro colour, bruises, cavity darkening, sparse wet patches.
    "MacroColour": 0.5, "MacroColourGain": 2.6, "BruiseAmount": 0.55, "CavityDark": 0.35,
    "WetMacroScale": 9.0, "WetMacroTile": 0.15,
}
HOLES = 6  # hole slots of the living wall material; AJamLivingWall::MaxHoles
# The hole outline: keep in step with AJamLivingWall::WoundGap and OutlineClosing
# (JamLivingWall.cpp). Per hole A = (centre x, z, radius, age s), B = (seed, fibre
# angle, stretch, blobs), C = (stun s, close s, tear s); in the mesh's own space (cm).
HOLE_HLSL = """
float h = 100000.0;
float4 HA[6] = {Hole0A, Hole1A, Hole2A, Hole3A, Hole4A, Hole5A};
float4 HB[6] = {Hole0B, Hole1B, Hole2B, Hole3B, Hole4B, Hole5B};
float4 HC[6] = {Hole0C, Hole1C, Hole2C, Hole3C, Hole4C, Hole5C};
for (int i = 0; i < 6; i++)
{
    float4 a = HA[i];
    float4 b = HB[i];
    float4 c = HC[i];
    if (a.z <= 0.0) continue;
    float2 o = P - a.xy;
    float th = atan2(o.y, o.x);
    float wob = 1.0;
    for (int k = 2; k <= 5; k++)
    {
        wob += (0.05 + 0.12 * frac(b.x * 0.6180339 * k + 0.11 * k)) * sin(k * th + 6.2831853 * frac(b.x * 0.7548776 * k + 0.37));
    }
    float shape = a.z * clamp(wob, 0.75, 1.3) * (1.0 + b.z * (0.5 + 0.5 * cos(2.0 * (th - b.y))));
    float ac = 0.5 - 0.5 * cos(2.0 * (th - b.y));
    float op = 1.0 - pow(1.0 - saturate(a.w / max(c.z * (1.0 + 0.6 * ac), 0.001)), 3.0);
    float un = 0.6 * sin(th + 6.2831853 * frac(b.x * 1.3)) + 0.4 * sin(2.0 * th + 6.2831853 * frac(b.x * 2.7));
    float dl = c.z + c.x * (1.0 + 0.25 * un) + 0.25 * c.y * (1.0 - ac);
    h = min(h, length(o) - shape * op * (1.0 - smoothstep(dl, dl + c.y, a.w)) * Edge);
    for (int j = 0; j < 3; j++)
    {
        if (j + 0.5 > b.w) break;
        float be = 6.2831853 * frac(b.x * (j + 1) * 0.4142136 + 0.17 * j);
        float2 bc = a.xy + float2(cos(be), sin(be)) * a.z * 0.8;
        float ac2 = 0.5 - 0.5 * cos(2.0 * (be - b.y));
        float op2 = 1.0 - pow(1.0 - saturate(a.w / max(c.z * (1.0 + 0.6 * ac2), 0.001)), 3.0);
        float un2 = 0.6 * sin(be + 6.2831853 * frac(b.x * 1.3)) + 0.4 * sin(2.0 * be + 6.2831853 * frac(b.x * 2.7));
        float dl2 = c.z + c.x * (1.0 + 0.25 * un2) + 0.25 * c.y * (1.0 - ac2);
        float rb = a.z * (0.3 + 0.15 * frac(b.x * (j + 3) * 0.381966)) * op2 * (1.0 - smoothstep(dl2, dl2 + c.y, a.w));
        h = min(h, length(P - bc) - rb * Edge);
    }
}
return h;
"""
DEFAULT_VECTORS = {"Tint": (1.0, 1.0, 1.0), "VesselColor": (0.35, 0.08, 0.12), "BruiseTint": (0.55, 0.22, 0.4)}
WAT = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture"
WAN = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedNormal"


def _custom_input(name):
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", name)
    return ci


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

    def mask(src, out="", **channels):
        n = node(unreal.MaterialExpressionComponentMask, r=channels.get("r", False), g=channels.get("g", False),
                 b=channels.get("b", False), a=channels.get("a", False))
        link(src, n, "", out)
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
    rough = lerp(scalar("RoughDry"), scalar("RoughWet"), mask(wet, r=True))

    # Macro layers: the same maps at a larger scale (MacroTile of the UVs, MacroTriplanarScale
    # of the triplanar tile) - a large normal for volume, and on the planet a colour break-up.
    macro_uv = op(mul, uv, scalar("MacroTile"))
    macro_tri = op(mul, tri3, scalar("MacroTriplanarScale"))
    nrm_macro = switch("Triplanar", world_aligned(WAN, n_obj, macro_tri), sample(n_obj, macro_uv, ST.SAMPLERTYPE_NORMAL), xyz)
    bc_macro = switch("Triplanar", world_aligned(WAT, bc_obj, macro_tri), sample(bc_obj, macro_uv, ST.SAMPLERTYPE_COLOR), xyz)
    wet_macro = switch("Triplanar", world_aligned(WAT, wet_obj, op(mul, tri3, scalar("WetMacroScale"))),
                       sample(wet_obj, op(mul, uv, scalar("WetMacroTile")), ST.SAMPLERTYPE_LINEAR_GRAYSCALE), xyz)

    # The planet (switch PlanetLook): vertex colour R = cavity baked in Blender, G = blotches.
    # Folds darker and wet, sparse wet patches, bruise-like patches, the macro skin over the detail.
    vc = node(unreal.MaterialExpressionVertexColor)
    cavity = mask(vc, r=True)
    blotch = node(unreal.MaterialExpressionSmoothStep)
    link(node(unreal.MaterialExpressionConstant, r=0.55), blotch, "Min")
    link(node(unreal.MaterialExpressionConstant, r=0.85), blotch, "Max")
    link(mask(vc, g=True), blotch, "Value")
    planet = lerp(colour, op(mul, colour, op(mul, mask(bc_macro, r=True, g=True, b=True), scalar("MacroColourGain"))), scalar("MacroColour"))
    planet = lerp(planet, op(mul, planet, vector("BruiseTint")), op(mul, blotch, scalar("BruiseAmount")))
    planet = op(mul, planet, lerp(node(unreal.MaterialExpressionConstant, r=1.0), scalar("CavityDark"), cavity))
    patches = node(unreal.MaterialExpressionSaturate)
    link(op(mul, op(add, mask(wet_macro, r=True), const_b=-0.62), const_b=4.0), patches)
    wet_planet = node(unreal.MaterialExpressionSaturate)
    wet_planet_in = node(unreal.MaterialExpressionMax)
    link(op(mul, cavity, const_b=1.6), wet_planet_in, "A")
    link(patches, wet_planet_in, "B")
    link(wet_planet_in, wet_planet)
    colour = switch("PlanetLook", planet, colour)
    rough = switch("PlanetLook", lerp(scalar("RoughDry"), scalar("RoughWet"), wet_planet), rough)

    # Breathing: a swell along the normal (all of them; PulseCm 0 is none).
    phase = node(unreal.MaterialExpressionDotProduct)
    link(node(unreal.MaterialExpressionWorldPosition), phase, "A")
    link(node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0.004, 0.006, 0.005, 0)), phase, "B")
    breath = node(unreal.MaterialExpressionSine)
    link(op(add, op(mul, node(unreal.MaterialExpressionTime), scalar("PulseSpeed")), phase), breath)
    swell = op(mul, op(mul, node(unreal.MaterialExpressionVertexNormalWS), breath), scalar("PulseCm"))

    # The living wall's holes (switch LivingHoles): AJamLivingWall writes up to HOLES
    # holes as HoleN = (x, z, radius) in the mesh's own space (cm). h = distance to the
    # nearest hole's edge, the edge pushed in and out by the vessel mask (panning: strands
    # weave across while the hole closes). h < 0 is cut away (opacity mask), a dark wet band
    # rims the hole, and the rim twitches along the wall normal.
    local = node(unreal.MaterialExpressionTransformPosition,
                 transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                 transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    link(node(unreal.MaterialExpressionWorldPosition), local)
    lxz = append(mask(local, r=True), mask(local, b=True))
    pan = op(mul, node(unreal.MaterialExpressionTime), append(scalar("HolePanX"), scalar("HolePanZ")))
    jag_uv = op(add, op(unreal.MaterialExpressionDivide, lxz, scalar("HoleNoiseCm")), pan)
    jag = op(add, mask(sample(ves_obj, jag_uv, ST.SAMPLERTYPE_LINEAR_GRAYSCALE), r=True), const_b=-0.5)
    edge_scale = op(add, op(mul, jag, scalar("HoleJag")), const_b=1.0)
    # The outline of every hole, the same function AJamLivingWall::WoundGap uses for
    # its collision cells (HOLE_HLSL), the edge pushed in and out by the vessel noise.
    names = ["P", "Edge"] + ["Hole%d%s" % (k, part) for k in range(HOLES) for part in "ABC"]
    nearest = node(unreal.MaterialExpressionCustom, code=HOLE_HLSL, description="LivingHoles",
                   output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                   inputs=[_custom_input(n) for n in names])
    link(lxz, nearest, "P")
    link(edge_scale, nearest, "Edge")
    defaults = {"A": (1.0e5, 1.0e5, 0.0, 0.0), "B": (0.0, 0.0, 0.0, 0.0), "C": (1.0, 1.0, 0.0, 0.0)}
    for k in range(HOLES):
        for part in "ABC":
            hole = node(unreal.MaterialExpressionVectorParameter, parameter_name="Hole%d%s" % (k, part),
                        default_value=unreal.LinearColor(*defaults[part]))
            link(hole, nearest, "Hole%d%s" % (k, part), "RGBA")
    cut = node(unreal.MaterialExpressionSaturate)
    link(op(unreal.MaterialExpressionDivide, nearest, scalar("HoleSoftCm")), cut)
    rim = node(unreal.MaterialExpressionSaturate)
    link(op(unreal.MaterialExpressionDivide, nearest, scalar("HoleRimCm")), rim)
    holed_colour = op(mul, colour, lerp(scalar("HoleRimDark"), node(unreal.MaterialExpressionConstant, r=1.0), rim))
    holed_rough = lerp(scalar("RoughWet"), rough, rim)
    one = node(unreal.MaterialExpressionConstant, r=1.0)
    opacity = switch("LivingHoles", cut, one)
    colour = switch("LivingHoles", holed_colour, colour)
    rough = switch("LivingHoles", holed_rough, rough)
    jerk_phase = node(unreal.MaterialExpressionDotProduct)
    link(lxz, jerk_phase, "A")
    link(node(unreal.MaterialExpressionConstant2Vector, r=0.21, g=0.17), jerk_phase, "B")
    jerk = node(unreal.MaterialExpressionSine)
    link(op(add, op(mul, node(unreal.MaterialExpressionTime), const_b=29.0), jerk_phase), jerk)
    jerk_y = op(mul, op(mul, jerk, op(sub, one, rim)), scalar("TwitchCm"))
    zero = node(unreal.MaterialExpressionConstant, r=0.0)
    to_world = node(unreal.MaterialExpressionTransform,
                    transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                    transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(append(append(zero, jerk_y), zero), to_world)
    offset = switch("LivingHoles", op(add, to_world, swell), swell)

    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(scalar("Specular"), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(scalar("Metallic"), "", unreal.MaterialProperty.MP_METALLIC)
    # Volume: the macro normal under the detail one (angle-corrected blend), each with its strength
    # (over 1 exaggerates); NormalStrength 0 and MacroNormalStrength 0 is flat.
    flat = node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0, 0, 1, 0))
    detail = node(unreal.MaterialExpressionNormalize)
    link(lerp(flat, mask(nrm, r=True, g=True, b=True), scalar("NormalStrength")), detail)
    large = node(unreal.MaterialExpressionNormalize)
    link(lerp(flat, mask(nrm_macro, r=True, g=True, b=True), scalar("MacroNormalStrength")), large)
    blend = node(unreal.MaterialExpressionMaterialFunctionCall,
                 material_function=unreal.load_asset("/Engine/Functions/Engine_MaterialFunctions02/Utility/BlendAngleCorrectedNormals"))
    link(large, blend, "BaseNormal")
    link(detail, blend, "AdditionalNormal")
    mel.connect_material_property(blend, "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(op(mul, colour, scalar("SelfLight")), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(op(mul, scalar("Opacity"), opacity), "", unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.connect_material_property(offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(m)
    jam_build.save(BASE)
    log("built " + BASE)


def _instance(name, parent, scalars=None, vectors=None, textures=None, switches=None, translucent=False, masked=False):
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
    if masked:
        o = mi.get_editor_property("base_property_overrides")
        o.set_editor_property("override_blend_mode", True)
        o.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mi.set_editor_property("base_property_overrides", o)
    if translucent:
        o = mi.get_editor_property("base_property_overrides")
        o.set_editor_property("override_blend_mode", True)
        o.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        o.set_editor_property("override_two_sided", True)
        o.set_editor_property("two_sided", True)
        mi.set_editor_property("base_property_overrides", o)
    mel.update_material_instance(mi)
    jam_build.save(path)
    return mi


def _t(name):
    return TEX + "/T_Jam_Gritty_" + name


RUST_BC = "/Game/MindKore/Art/Admin/Yard/Props2/Textures/T_MK_Yard_Rust_BC"
RUST_N = "/Game/MindKore/Art/Admin/Yard/Props2/Textures/T_MK_Yard_Rust_N"

# look: (scalars, vectors, textures). Tuned on rendered frames; the texel look kept (Nearest).
LOOKS = {
    "Muscle": ({"VesselAmount": 0.5, "VesselTiling": 0.5, "NormalStrength": 1.3, "MacroNormalStrength": 1.2, "SelfLight": 0.18},
               {"Tint": (1.25, 1.0, 1.0)}, {"BaseColor": _t("Muscle_BC"), "Normal": _t("Muscle_N")}),
    "Tendon": ({"NormalStrength": 1.2, "MacroNormalStrength": 1.0, "RoughDry": 0.6, "SelfLight": 0.12}, {"Tint": (1.05, 1.0, 0.95)},
               {"BaseColor": _t("Tendon_BC"), "Normal": _t("Tendon_N")}),
    "Vessel": ({"NormalStrength": 1.0, "SelfLight": 0.15}, {"Tint": (0.6, 0.22, 0.28)},
               {"BaseColor": _t("Muscle_BC"), "Normal": _t("Muscle_N")}),
    "Membrane": ({"NormalStrength": 1.0, "MacroNormalStrength": 0.8, "Opacity": 0.5, "SelfLight": 0.25, "RoughDry": 0.35},
                 {"Tint": (1.1, 0.95, 0.9)}, {"BaseColor": _t("Membrane_BC"), "Normal": _t("Membrane_N")}),
    "MuscleEnd": ({"NormalStrength": 1.0, "SelfLight": 0.18}, {"Tint": (1.2, 1.0, 1.0)},
                  {"BaseColor": _t("MuscleEnd_BC"), "Normal": _t("MuscleEnd_N")}),
    "FrameMetal": ({"Metallic": 0.7, "RoughDry": 0.75, "RoughWet": 0.45, "Specular": 0.5, "SelfLight": 0.0, "MacroNormalStrength": 0.0},
                   {"Tint": (0.9, 0.85, 0.8)}, {"BaseColor": RUST_BC, "Normal": RUST_N}),
    # Owner 18.10: «слишком блестящая, похожа на линолеум» - dry and matte, wet only in the folds and in
    # sparse patches, low specular, the skin at two scales, bruises, veins, a stronger large normal.
    "PlanetSkin": ({"TriplanarCm": 500.0, "VesselAmount": 0.45, "VesselTriplanarScale": 5.0, "NormalStrength": 1.2,
                    "MacroNormalStrength": 1.6, "MacroTriplanarScale": 6.0, "SelfLight": 0.2, "RoughDry": 0.85, "RoughWet": 0.35,
                    "Specular": 0.3, "CavityDark": 0.5, "BruiseAmount": 0.4}, {"Tint": (1.1, 0.95, 0.95)}, {"BaseColor": _t("PlanetSkin_BC"), "Normal": _t("PlanetSkin_N")}),
    "PlanetEye": ({"NormalStrength": 0.8, "MacroNormalStrength": 0.0, "SelfLight": 1.2, "RoughWet": 0.1, "RoughDry": 0.1}, {},
                  {"BaseColor": _t("PlanetEye_BC"), "Normal": _t("PlanetEye_N")}),
    "Suckers": ({"NormalStrength": 1.3, "MacroNormalStrength": 0.8, "SelfLight": 0.15, "VTile": 3.0, "RoughDry": 0.7},
                {"Tint": (1.1, 0.95, 0.95)}, {"BaseColor": _t("Suckers_BC"), "Normal": _t("Suckers_N")}),
}

# The mesh living wall's slots: the same looks with LivingHoles on.
WALL_SLOTS = ["Muscle", "Tendon", "Vessel", "Membrane"]


def materials(log, rebuild=False):
    """Make (once) the master and (every call) the instances; returns {look: MI} plus {"Wall_<look>": MI}."""
    # The graph is rebuilt in place only on request: emptying the graph of a loaded,
    # rooted master asserts !IsRooted (18.10, build_zerog.py after a wall spawn).
    if rebuild or not eal.does_asset_exist(BASE):
        _master(log)
    base = unreal.load_asset(BASE)
    out = {}
    for name, (scalars, vectors, textures) in LOOKS.items():
        out[name] = _instance("MI_Jam_" + name, base, scalars, vectors, textures,
                              {"Triplanar": name == "PlanetSkin", "PlanetLook": name == "PlanetSkin"}, translucent=name == "Membrane")
    for name in WALL_SLOTS:
        scalars, vectors, textures = LOOKS[name]
        # The wall's back membrane is nearly opaque: a lit room behind must not read as a hole.
        # Its self light is raised: the round walls sit in dark caves and the owner keeps the lighting as it is.
        # No vessel web on the walls: raw meat (owner's reference, 18.10), not a ball of yarn.
        wall_scalars = dict(scalars, PulseCm=0.5, SelfLight=scalars.get("SelfLight", 0.15) * 2.0, VesselAmount=0.0,
                            **({"Opacity": 0.88} if name == "Membrane" else {}))
        out["Wall_" + name] = _instance("MI_Jam_Wall_" + name, base, wall_scalars, vectors, textures,
                                        {"LivingHoles": True}, translucent=name == "Membrane", masked=name != "Membrane")
    log("flesh materials: %s" % ", ".join(sorted(out)))
    return out


# --- the mesh living wall --------------------------------------------------------------------------
SKIN = {False: "/Game/JamArt/Meshes/LivingWall/SM_Jam_LivingWall_Square", True: "/Game/JamArt/Meshes/LivingWall/SM_Jam_LivingWall_Round"}
# The mesh slots (Scripts/blender/jam_living_wall_mesh.py): muscle, tendon, vessel, membrane, frame.
SKIN_SLOTS = {"Muscle": "Wall_Muscle", "Tendon": "Wall_Tendon", "Vessel": "Wall_Vessel", "Membrane": "Wall_Membrane", "FrameMetal": "FrameMetal"}


def skin_wall(wall, mats, round_=False):
    """Give an AJamLivingWall the mesh look; its cells (rods, hidden) stay the collision."""
    wall.set_editor_property("skin_mesh", unreal.load_asset(SKIN[round_]))
    skin = wall.get_editor_property("skin")
    for slot, name in SKIN_SLOTS.items():
        # The round mesh's frame slot is its fleshy lip: the planet's own (world-aligned) skin, no seam.
        skin.set_material_by_name(slot, mats["PlanetSkin" if round_ and slot == "FrameMetal" else name])
