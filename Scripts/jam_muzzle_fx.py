"""Muzzle flash and powder smoke materials for UJamMuzzleFxComponent (editor Python).

  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject -ExecutePythonScript="<path>/Scripts/jam_muzzle_fx.py" -unattended -nop4 -nosplash -nullrhi

Rebuilt in place on every run. Both draw a soft round sprite on the engine plane
(/Engine/BasicShapes/Plane, UV 0..1), one instance per sprite in an instanced
mesh; the component turns each plane to the camera and feeds per-instance
custom data:

  M_Jam_MuzzleFlash  additive, unlit: hot white core + warm halo broken into a
                     faint ray burst. Data 0 intensity, 1 seed.
  M_Jam_MuzzleSmoke  translucent, unlit: a thin grey puff, 3-octave value noise
                     inside a gaussian. Data 0 opacity, 1 seed, 2 spin (rad).

No texture: the shapes are maths, nothing third-party.
"""
import unreal

FOLDER = "/Game/JamArt/FX"
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

FLASH_HLSL = """
float2 p = UV * 2 - 1;
float r2 = dot(p, p);
float a = atan2(p.y, p.x);
float rays = 0.7 + 0.3 * sin(a * 7 + S * 40) * sin(a * 11 - S * 23);
float core = exp(-r2 * 28);
float halo = exp(-r2 * 5) * rays;
return I * saturate(1 - r2) * (float3(1, 0.95, 0.85) * core * 4 + Col * halo);
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
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", blend)
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
                   {"UV": uv, "I": data(0), "S": data(1), "Col": colour("Colour", unreal.LinearColor(1.0, 0.62, 0.28, 1.0))})
    mel.connect_material_property(mul(burst, scalar("Brightness", 30.0)), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def _smoke(m, node, custom, data, scalar, colour, mul):
    uv = node(unreal.MaterialExpressionTextureCoordinate)
    puff = custom(SMOKE_HLSL, "Puff", unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                  {"UV": uv, "A": data(0), "S": data(1), "R": data(2)})
    mel.connect_material_property(mul(colour("Colour", unreal.LinearColor(0.62, 0.64, 0.68, 1.0)), scalar("Brightness", 1.5)),
                                  "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(puff, "", unreal.MaterialProperty.MP_OPACITY)


def build():
    _material("M_Jam_MuzzleFlash", unreal.BlendMode.BLEND_ADDITIVE, _flash)
    _material("M_Jam_MuzzleSmoke", unreal.BlendMode.BLEND_TRANSLUCENT, _smoke)


if __name__ == "__main__":
    build()
    unreal.SystemLibrary.quit_editor()
