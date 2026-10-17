"""The star sky of Lvl_Jam_Ship (editor Python, called by build_ship.py).

Texture: NASA SVS «Deep Star Maps 2020», galactic projection, equirectangular
3840 x 1920 (CREDITS.md), imported once as /Game/JamArt/Textures/Sky/T_Jam_Starmap_Gal:
  import_starmap(path, log) - BC1 (DXT1), sRGB, linear filtering, no mips (the
      dome is sampled about 1:1 with the screen, and without mips the
      equirect seam needs no derivative fix), never streamed.
Material M_Jam_SkyStars (unlit, two-sided; graph and numbers rebuilt on every
call): the map sampled by the direction from the dome centre (equirect: u
from the azimuth around Z, v from the polar angle; the galactic centre lies
toward +X on the horizon, YawDeg turns it), emissive = Tint (the old dark
violet) + map ^ Contrast x Brightness.

The dome that shows it (Sky_Stars, build_ship.py) is hidden from reflection
and sky-light captures: the SkyLight keeps capturing the old dark-violet dome
behind it, so the scene light does not change (owner: «освещение не трогаем»).
"""
import unreal

TEX = "/Game/JamArt/Textures/Sky"
STARMAP = TEX + "/T_Jam_Starmap_Gal"
MATS = "/Game/JamArt/Materials/Sky"
BASE = MATS + "/M_Jam_SkyStars"
INSTANCE = MATS + "/MI_Jam_SkyStars"

# Tuned on rendered frames (jam.Ship.SkyShots).
SCALARS = {"Brightness": 3.0, "Contrast": 1.15, "YawDeg": 0.0}
TINT = (0.035, 0.02, 0.07)  # the old dome colour (MI_Jam_SkyDome)

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def import_starmap(path, log):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", TEX)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(STARMAP)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("never_stream", True)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_SKYBOX)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    eal.save_asset(STARMAP, False)
    log("imported %s %dx%d" % (STARMAP, texture.blueprint_get_size_x(), texture.blueprint_get_size_y()))


def material(log):
    """Build M_Jam_SkyStars (in place) and set MI_Jam_SkyStars; returns the instance."""
    if eal.does_asset_exist(BASE):
        m = unreal.load_asset(BASE)
        mel.delete_all_material_expressions(m)
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Jam_SkyStars", MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    y = [0]

    def node(cls, **props):
        y[0] += 110
        n = mel.create_material_expression(m, cls, -1400, y[0])
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(a, b, pin="", out=""):
        if not mel.connect_material_expressions(a, out, b, pin):
            raise RuntimeError("link failed: %s -> %s.%s" % (a.get_name(), b.get_name(), pin))

    def op(cls, a, b=None, const_b=None):
        n = node(cls)
        link(a, n, "A")
        if b is not None:
            link(b, n, "B")
        elif const_b is not None:
            n.set_editor_property("const_b", const_b)
        return n

    def one(cls, src, **props):
        n = node(cls, **props)
        link(src, n)
        return n

    def scalar(name):
        return node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=SCALARS[name])

    mul, add, sub, div = (unreal.MaterialExpressionMultiply, unreal.MaterialExpressionAdd,
                          unreal.MaterialExpressionSubtract, unreal.MaterialExpressionDivide)
    # Direction from the dome centre (the actor position) to the pixel.
    d = one(unreal.MaterialExpressionNormalize, op(sub, node(unreal.MaterialExpressionWorldPosition), node(unreal.MaterialExpressionObjectPositionWS)))
    x = one(unreal.MaterialExpressionComponentMask, d, r=True, g=False, b=False, a=False)
    yv = one(unreal.MaterialExpressionComponentMask, d, r=False, g=True, b=False, a=False)
    z = one(unreal.MaterialExpressionComponentMask, d, r=False, g=False, b=True, a=False)
    azimuth = node(unreal.MaterialExpressionArctangent2Fast)
    link(yv, azimuth, "Y")
    link(x, azimuth, "X")
    yaw = op(div, scalar("YawDeg"), node(unreal.MaterialExpressionConstant, r=360.0))
    u = op(add, op(add, op(div, azimuth, node(unreal.MaterialExpressionConstant, r=6.2831853)), const_b=0.5), yaw)
    polar = one(unreal.MaterialExpressionArccosine, one(unreal.MaterialExpressionClamp, z, min_default=-1.0, max_default=1.0))
    v = op(div, polar, node(unreal.MaterialExpressionConstant, r=3.14159265))
    uv = node(unreal.MaterialExpressionAppendVector)
    link(u, uv, "A")
    link(v, uv, "B")
    tex = node(unreal.MaterialExpressionTextureSample, texture=unreal.load_asset(STARMAP))
    link(uv, tex, "UVs")
    power = node(unreal.MaterialExpressionPower)
    link(tex, power, "Base")
    link(scalar("Contrast"), power, "Exp")
    tint = node(unreal.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=unreal.LinearColor(*TINT, 1.0))
    emissive = op(add, tint, op(mul, power, scalar("Brightness")))
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_asset(BASE, False)
    if eal.does_asset_exist(INSTANCE):
        mi = unreal.load_asset(INSTANCE)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset("MI_Jam_SkyStars", MATS, unreal.MaterialInstanceConstant,
                                                                     unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, m)
    for k, v in SCALARS.items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(*TINT, 1.0))
    eal.save_asset(INSTANCE, False)
    log("built %s, %s" % (BASE, INSTANCE))
    return mi
