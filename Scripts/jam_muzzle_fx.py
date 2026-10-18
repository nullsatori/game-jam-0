"""Muzzle flash and powder smoke materials for UJamMuzzleFxComponent (editor Python).

  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject -ExecutePythonScript="<path>/Scripts/jam_muzzle_fx.py" -unattended -nop4 -nosplash -nullrhi

Rebuilt in place on every run. Both draw a sprite on the engine plane
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
                     sparse sprites, so cheap enough): a grey-blue puff,
                     deliberately low-res - a texel grid per puff (~6 px at 1080p), 3-octave
                     value noise in a gaussian cut into 5 Bayer-dithered
                     density bands, a scanline fringe on the outer band, a
                     heat shimmer (2D-offset refraction) while fresh.
                     Data 0 opacity, 1 seed, 2 spin (rad), 3 tear, 4 shimmer,
                     5 and 6 the grid's texels along and across (set by the component).

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

# The smoke keeps the jam's low-res look on purpose (owner, 19.10): a texel grid
# per puff, L x W cells (the component sizes a texel to ~6 px on screen), its density cut into BANDS steps with an ordered 4x4 Bayer
# dither between them (crisp, not a blurry blob), a faint scanline fringe on the
# outer band. Returns (opacity, fringe rgb).
SMOKE_HLSL = """
const float BANDS = 5;
float2 GRID = max(floor(float2(L, W)), 4);
const float M[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
float2 cell = floor(UV * GRID);
float2 p = (cell + 0.5) / GRID * 2 - 1;
float c = cos(R), s = sin(R);
float2 pr = float2(c * p.x - s * p.y, s * p.x + c * p.y);
float r2 = dot(p, p);
float n = 0, amp = 0.5;
float2 q = pr * 2.2 + S * 17;
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
// Tear raises the noise floor: rags and holes instead of a whole puff.
float d = saturate(1 - r2) * exp(-r2 * 2.0) * saturate((n - 0.22 - 0.22 * T) * 2.6);
int2 b = int2(cell) & 3;
float band = floor(d * BANDS + (M[b.y * 4 + b.x] + 0.5) / 16);
float edge = band == 1 ? 1 : 0;
float row = fmod(floor(Parameters.SvPosition.y * 0.5), 2);
float3 fringe = edge * lerp(float3(0.10, 0.22, 0.30), float3(0.30, 0.14, 0.05), row);
return float4(A * band / BANDS, fringe);
"""

# Heat shimmer: a texel-stepped screen offset inside a fresh puff (2D-offset refraction).
SHIMMER_HLSL = """
float2 GRID = max(floor(float2(L, W)), 4);
float2 cell = floor(UV * GRID);
float2 p = (cell + 0.5) / GRID * 2 - 1;
float hx = frac(sin(dot(cell + S * 31, float2(127.1, 311.7))) * 43758.5453);
float hy = frac(sin(dot(cell + S * 31, float2(269.5, 183.3))) * 43758.5453);
return Sh * saturate(1 - dot(p, p)) * (float2(hx, hy) - 0.5) * 0.008;
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
    puff = custom(SMOKE_HLSL, "Puff", unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                  {"UV": uv, "A": data(0), "S": data(1), "R": data(2), "T": data(3), "L": data(5), "W": data(6)})
    opacity = node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False)
    mel.connect_material_expressions(puff, "", opacity, "")
    fringe = node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=True, a=True)
    mel.connect_material_expressions(puff, "", fringe, "")
    # Grey-blue, darker than before: lit by the muzzle light it warms, it never clips to white.
    tint = colour("Colour", unreal.LinearColor(0.30, 0.34, 0.40, 1.0))
    mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Thin smoke scatters light through itself: lit from behind (the sun past the cloud) it glows too.
    mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.connect_material_property(scalar("Roughness", 1.0), "", unreal.MaterialProperty.MP_ROUGHNESS)
    # A little light of its own so the veil stays readable against black space
    # (owner's call, 18.10), plus the scanline fringe on its outer band.
    glow = node(unreal.MaterialExpressionAdd)
    mel.connect_material_expressions(mul(tint, scalar("Ambient", 0.25)), "", glow, "A")
    mel.connect_material_expressions(mul(fringe, scalar("Fringe", 0.5)), "", glow, "B")
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    mode = [n for n in dir(unreal.RefractionMode) if "2D" in n.upper()]
    if mode:
        m.set_editor_property("refraction_method", getattr(unreal.RefractionMode, mode[0]))
        shimmer = custom(SHIMMER_HLSL, "Shimmer", unreal.CustomMaterialOutputType.CMOT_FLOAT2,
                         {"UV": uv, "S": data(1), "Sh": data(4), "L": data(5), "W": data(6)})
        mel.connect_material_property(shimmer, "", unreal.MaterialProperty.MP_REFRACTION)
    else:
        unreal.log_warning("muzzle fx: no 2D-offset refraction mode, no heat shimmer")


def build():
    _material("M_Jam_MuzzleFlash", unreal.BlendMode.BLEND_ADDITIVE, _flash)
    _material("M_Jam_MuzzleSmoke", unreal.BlendMode.BLEND_TRANSLUCENT, _smoke)


if __name__ == "__main__":
    build()
    unreal.SystemLibrary.quit_editor()
