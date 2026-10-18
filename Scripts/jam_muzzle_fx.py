"""Muzzle flash and powder smoke materials for UJamMuzzleFxComponent (editor Python).

  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject -ExecutePythonScript="<path>/Scripts/jam_muzzle_fx.py" -unattended -nop4 -nosplash -nullrhi

Rebuilt in place on every run. Both draw a soft round sprite on the engine plane
(/Engine/BasicShapes/Plane, UV 0..1), one instance per sprite in an instanced
mesh; the component turns each plane to the camera and feeds per-instance
custom data:

  M_Jam_MuzzleFlash  additive, unlit: one ragged lobe of hot gas - an iso-surface
                     of 4-octave value noise falling off from the centre, its colour
                     cooling from white-yellow to dim red with heat.
                     Data 0 intensity, 1 seed, 2 heat (1 white-hot .. 0), 3 turn (rad).
  M_Jam_MuzzleSmoke  translucent, two-sided foliage (lit from behind it glows
                     through), lit per pixel (Surface ForwardShading: the
                     sun with its shadows, lamps, the flash's light; a few
                     sparse sprites, so cheap enough): a thin grey puff, 3-octave value noise
                     inside a gaussian. Data 0 opacity, 1 seed, 2 spin (rad).

No texture: the shapes are maths, nothing third-party.
"""
import unreal

FOLDER = "/Game/JamArt/FX"
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

FLASH_HLSL = """
float2 p = UV * 2 - 1;
float c = cos(R), s = sin(R);
p = float2(c * p.x - s * p.y, s * p.x + c * p.y);
float r2 = dot(p, p);
float n = 0, amp = 0.5;
float2 q = p * 2.0 + S * 19;
for (int o = 0; o < 4; o++)
{
    float2 i = floor(q);
    float2 f = frac(q);
    f = f * f * (3 - 2 * f);
    float h00 = frac(sin(dot(i, float2(127.1, 311.7))) * 43758.5453);
    float h10 = frac(sin(dot(i + float2(1, 0), float2(127.1, 311.7))) * 43758.5453);
    float h01 = frac(sin(dot(i + float2(0, 1), float2(127.1, 311.7))) * 43758.5453);
    float h11 = frac(sin(dot(i + float2(1, 1), float2(127.1, 311.7))) * 43758.5453);
    n += amp * lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
    q = q * 2.07 + 5.3;
    amp *= 0.5;
}
// A billowy iso-surface of the noise: a ragged outline, lumps inside, no symmetry.
float gas = saturate((n - 0.15 - 0.85 * r2) * 3.0);
float t = saturate(H * gas * 1.4);
float3 col = lerp(float3(0.7, 0.14, 0.03), float3(1.0, 0.5, 0.15), saturate(t * 3));
col = lerp(col, float3(1.0, 0.92, 0.78), saturate(t * 2.5 - 1.0));
return I * (gas * 0.8 + gas * gas * gas * 2.5) * col;
"""

SMOKE_HLSL = """
float2 p = UV * 2 - 1;
float c = cos(R), s = sin(R);
p = float2(c * p.x - s * p.y, s * p.x + c * p.y);
float r2 = dot(p, p);
float n = 0, amp = 0.5;
float2 q = p * 2.2 + S * 17;
for (int o = 0; o < 3; o++)
{
    float2 i = floor(q);
    float2 f = frac(q);
    f = f * f * (3 - 2 * f);
    float h00 = frac(sin(dot(i, float2(127.1, 311.7))) * 43758.5453);
    float h10 = frac(sin(dot(i + float2(1, 0), float2(127.1, 311.7))) * 43758.5453);
    float h01 = frac(sin(dot(i + float2(0, 1), float2(127.1, 311.7))) * 43758.5453);
    float h11 = frac(sin(dot(i + float2(1, 1), float2(127.1, 311.7))) * 43758.5453);
    n += amp * lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
    q = q * 2.03 + 3.7;
    amp *= 0.5;
}
return A * saturate(1 - r2) * exp(-r2 * 2.5) * saturate(n * 1.8 - 0.35);
"""


def _input(name):
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", name)
    return ci


def _material(name, blend, build):
    path = FOLDER + "/" + name
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT if blend == unreal.BlendMode.BLEND_ADDITIVE
                          else unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    m.set_editor_property("blend_mode", blend)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    y = [0]

    def node(cls, **props):
        y[0] += 120
        n = mel.create_material_expression(m, cls, -900, y[0])
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(a, b, pin=""):
        if not mel.connect_material_expressions(a, "", b, pin):
            raise RuntimeError("link failed: %s -> %s.%s" % (a.get_name(), b.get_name(), pin))

    def custom(code, desc, out_type, inputs):
        n = node(unreal.MaterialExpressionCustom, code=code, description=desc, output_type=out_type,
                 inputs=[_input(k) for k in inputs])
        for k, src in inputs.items():
            link(src, n, k)
        return n

    def data(index):
        return node(unreal.MaterialExpressionPerInstanceCustomData, data_index=index)

    def scalar(pname, value):
        return node(unreal.MaterialExpressionScalarParameter, parameter_name=pname, default_value=value)

    def colour(pname, value):
        return node(unreal.MaterialExpressionVectorParameter, parameter_name=pname, default_value=value)

    def mul(a, b):
        n = node(unreal.MaterialExpressionMultiply)
        link(a, n, "A")
        link(b, n, "B")
        return n

    build(m, node, custom, data, scalar, colour, mul)
    mel.recompile_material(m)
    eal.save_asset(path, False)
    unreal.log("muzzle fx material: " + path)


def _flash(m, node, custom, data, scalar, colour, mul):
    uv = node(unreal.MaterialExpressionTextureCoordinate)
    burst = custom(FLASH_HLSL, "Burst", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   {"UV": uv, "I": data(0), "S": data(1), "H": data(2), "R": data(3)})
    mel.connect_material_property(mul(burst, scalar("Brightness", 6.0)), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def _smoke(m, node, custom, data, scalar, colour, mul):
    uv = node(unreal.MaterialExpressionTextureCoordinate)
    puff = custom(SMOKE_HLSL, "Puff", unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                  {"UV": uv, "A": data(0), "S": data(1), "R": data(2)})
    grey = colour("Colour", unreal.LinearColor(0.6, 0.6, 0.62, 1.0))
    mel.connect_material_property(grey, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Thin smoke scatters light through itself: lit from behind (the sun past the cloud) it glows too.
    mel.connect_material_property(grey, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.connect_material_property(scalar("Roughness", 1.0), "", unreal.MaterialProperty.MP_ROUGHNESS)
    # A little light of its own: a thin grey veil stays readable against black space
    # (owner's call, 18.10), the scene's light still dominates where there is any.
    mel.connect_material_property(mul(grey, scalar("Ambient", 0.15)), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(puff, "", unreal.MaterialProperty.MP_OPACITY)


def build():
    _material("M_Jam_MuzzleFlash", unreal.BlendMode.BLEND_ADDITIVE, _flash)
    _material("M_Jam_MuzzleSmoke", unreal.BlendMode.BLEND_TRANSLUCENT, _smoke)


if __name__ == "__main__":
    build()
    unreal.SystemLibrary.quit_editor()
