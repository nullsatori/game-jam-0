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


# The volumetric cloud (jam.Fx.VolumetricSmoke 1): a heterogeneous volume (engine,
# experimental) voxelises this Volume-domain material, lights it with real
# self-shadowing and scattering from the star, the lamps and the muzzle light. The
# low-res identity lives in the density itself: sampled at voxel centres
# (VoxelCm), cut into 5 bands with a 3D ordered dither, a cool/warm fringe on the
# outer band. Rounds bore tunnels and shed vortex rings (wakes), the eye clears
# what is within EyeClearCm. Everything in cloud space (CloudX/Y/Z, cm).
VOLUME_HLSL = """
const float M[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
float3 d = W - C;
float3 l = float3(dot(d, BX), dot(d, BY), dot(d, BZ));
float3 vox = floor(l / V);
float3 p = (vox + 0.5) * V;
float4 P[8] = { P0, P1, P2, P3, P4, P5, P6, P7 };
float dens = 0;
for (int i = 0; i < 8; i++)
{
    if (P[i].w > 0)
    {
        float3 q = p - P[i].xyz;
        dens += exp(-dot(q, q) / (P[i].w * P[i].w));
    }
}
float n = 0, amp = 0.5;
float3 q3 = p / 9 + S * 17;
for (int o = 0; o < 3; o++)
{
    float3 c0 = floor(q3);
    float3 f = frac(q3);
    f = f * f * (3 - 2 * f);
    float3 k = float3(127.1, 311.7, 74.7);
    float h000 = frac(sin(dot(c0, k)) * 43758.5453);
    float h100 = frac(sin(dot(c0 + float3(1, 0, 0), k)) * 43758.5453);
    float h010 = frac(sin(dot(c0 + float3(0, 1, 0), k)) * 43758.5453);
    float h110 = frac(sin(dot(c0 + float3(1, 1, 0), k)) * 43758.5453);
    float h001 = frac(sin(dot(c0 + float3(0, 0, 1), k)) * 43758.5453);
    float h101 = frac(sin(dot(c0 + float3(1, 0, 1), k)) * 43758.5453);
    float h011 = frac(sin(dot(c0 + float3(0, 1, 1), k)) * 43758.5453);
    float h111 = frac(sin(dot(c0 + float3(1, 1, 1), k)) * 43758.5453);
    n += amp * lerp(lerp(lerp(h000, h100, f.x), lerp(h010, h110, f.x), f.y),
                    lerp(lerp(h001, h101, f.x), lerp(h011, h111, f.x), f.y), f.z);
    q3 = q3 * 2.03 + 3.7;
    amp *= 0.5;
}
dens = saturate(dens) * saturate((n - 0.18 - 0.25 * K) * 2.6);
float dens0 = dens;
float4 WA[3] = { A0, A1, A2 };
float4 WB[3] = { B0, B1, B2 };
float WR[3] = { R0, R1, R2 };
float ring = 0;
for (int w = 0; w < 3; w++)
{
    if (WA[w].w <= 0)
    {
        continue;
    }
    float age = WB[w].w;
    float3 rel = p - WA[w].xyz;
    float along = dot(rel, WB[w].xyz);
    float rd = length(rel - along * WB[w].xyz);
    // The tunnel: bored at once, widening as the wake spreads, closing after a second or so.
    float r = WA[w].w * (1 + 1.5 * saturate(age / 0.6));
    float hold = saturate(1.6 - age);
    dens *= lerp(1, smoothstep(r * 0.7, r * 1.3, rd), hold);
    if (WR[w] > 0)
    {
        // The vortex ring: smoke rolled into a torus that travels on along the wake and widens.
        float R = WA[w].w * 2.2 * (1 + 1.2 * age);
        float2 t = float2(rd - R, along - WR[w] * age);
        float tube = max(V * 1.2, WA[w].w * 0.8);
        ring += exp(-dot(t, t) / (tube * tube)) * saturate(1 - age / 1.2);
    }
}
dens = max(dens, ring * saturate(dens0 * 3 + 0.6));
float de = length(p - E.xyz);
dens *= smoothstep(E.w * 0.4, E.w, de) * F;
// Fade out before the box faces: the volume ends there, a straight cut would show.
float3 al = abs(l);
dens *= smoothstep(H, H * 0.7, max(al.x, max(al.y, al.z)));
int3 b = int3(vox) & 3;
float th = (M[((b.x + b.z) & 3) + 4 * ((b.y + 2 * b.z) & 3)] + 0.5) / 16;
float band = floor(saturate(dens) * 5 + th);
float edge = band == 1 ? 1 : 0;
float3 fringe = edge * lerp(float3(0.10, 0.22, 0.30), float3(0.30, 0.14, 0.05), fmod(abs(vox.y), 2));
// The vortex ring glows a little: rolled smoke catching the light, read against the tunnel.
fringe += saturate(ring) * (band > 0 ? 1 : 0) * float3(1.0, 1.1, 1.3) * 1.5;
return float4(band / 5, fringe);
"""


def _volume(m, node, custom, data, scalar, colour, mul):
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_VOLUME)
    m.set_editor_property("used_with_heterogeneous_volumes", True)
    world = node(unreal.MaterialExpressionWorldPosition)
    vec = {}
    for name in ["CloudCentre", "CloudX", "CloudY", "CloudZ", "Eye"] + ["Puff%d" % i for i in range(8)] \
            + ["WakeA%d" % i for i in range(3)] + ["WakeB%d" % i for i in range(3)]:
        vec[name] = colour(name, unreal.LinearColor(0, 0, 0, 0))
    inputs = {"W": world, "V": scalar("VoxelCm", 2.0), "F": scalar("Fade", 1.0), "S": scalar("Seed", 0.0),
              "K": scalar("Tear", 0.2), "H": scalar("HalfCm", 35.0)}
    rgba = {"C": "CloudCentre", "BX": "CloudX", "BY": "CloudY", "BZ": "CloudZ", "E": "Eye"}
    for i in range(8):
        rgba["P%d" % i] = "Puff%d" % i
    for i in range(3):
        rgba["A%d" % i] = "WakeA%d" % i
        rgba["B%d" % i] = "WakeB%d" % i
        inputs["R%d" % i] = scalar("WakeRing%d" % i, 0.0)
    n = node(unreal.MaterialExpressionCustom, code=VOLUME_HLSL, description="Cloud",
             output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4,
             inputs=[_input(k) for k in list(inputs) + list(rgba)])
    for k, src in inputs.items():
        if not mel.connect_material_expressions(src, "", n, k):
            raise RuntimeError("link failed: " + k)
    for k, pname in rgba.items():
        # The 4-wide output of a vector parameter (w carries a radius, an age, a size).
        if not mel.connect_material_expressions(vec[pname], "RGBA", n, k):
            raise RuntimeError("link failed: " + k)
    band = node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False)
    mel.connect_material_expressions(n, "", band, "")
    fringe = node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=True, a=True)
    mel.connect_material_expressions(n, "", fringe, "")
    albedo = colour("Colour", unreal.LinearColor(0.55, 0.6, 0.68, 1.0))
    mel.connect_material_property(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Volume domain: the Subsurface Color pin is the extinction, 1/cm.
    mel.connect_material_property(mul(band, scalar("Extinction", 0.04)), "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    glow = node(unreal.MaterialExpressionAdd)
    mel.connect_material_expressions(mul(mul(albedo, band), scalar("Ambient", 0.08)), "", glow, "A")
    mel.connect_material_expressions(mul(mul(fringe, band), scalar("Fringe", 0.05)), "", glow, "B")
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def build():
    _material("M_Jam_MuzzleFlash", unreal.BlendMode.BLEND_ADDITIVE, _flash)
    _material("M_Jam_MuzzleSmoke", unreal.BlendMode.BLEND_TRANSLUCENT, _smoke)
    _material("M_Jam_MuzzleSmokeVolume", unreal.BlendMode.BLEND_ADDITIVE, _volume)


if __name__ == "__main__":
    build()
    unreal.SystemLibrary.quit_editor()
