"""Muzzle flash and powder smoke materials for UOSMuzzleFxComponent (editor Python).

  UnrealEditor-Cmd.exe <path>/OuterSpace.uproject -ExecutePythonScript="<path>/Scripts/os_muzzle_fx.py" -unattended -nop4 -nosplash -nullrhi

Rebuilt in place on every run. Both draw a sprite on the engine plane
(/Engine/BasicShapes/Plane, UV 0..1), one instance per sprite in an instanced
mesh; the component turns each plane to the camera and feeds per-instance
custom data:

  M_OS_MuzzleFlash  additive, unlit: one ragged lobe of hot gas - an iso-surface
                     of 4-octave value noise falling off from the centre, its colour
                     cooling from white-yellow to dim red with heat.
                     Data 0 intensity, 1 seed, 2 heat (1 white-hot .. 0), 3 turn (rad).
  M_OS_MuzzleSmoke  translucent, two-sided foliage (lit from behind it glows
                     through), lit per pixel (Surface ForwardShading: the
                     sun with its shadows, lamps, the flash's light; a few
                     sparse sprites, so cheap enough): a grey-blue puff,
                     deliberately low-res - a texel grid per puff (~6 px at 1080p), 3-octave
                     value noise in a gaussian cut into 5 Bayer-dithered
                     density bands, a scanline fringe on the outer band, a
                     heat shimmer (2D-offset refraction) while fresh.
                     Data 0 opacity, 1 seed, 2 spin (rad), 3 tear, 4 shimmer,
                     5 and 6 the grid's texels along and across (set by the component).

  M_OS_MuzzleSmokeVolume  Volume domain, Subsurface (the only model whose Subsurface Color pin - the
                     extinction - a heterogeneous volume reads): the smoke grid's atlas (FOSSmokeSim::Pack, a
                     transient texture from C++) drawn voxel for voxel. Docs/RecoilWeapons-Fx.md, «Живой дым».

No texture asset: the shapes are maths or the runtime grid, nothing third-party.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_build  # noqa: E402

FOLDER = "/Game/OSArt/FX"
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

# The smoke keeps the game's low-res look on purpose (owner, 19.10): a texel grid
# per puff, L x W cells (the component sizes a texel to ~6 px on screen), its density cut into BANDS steps with an ordered 4x4 Bayer
# dither between them (crisp, not a blurry blob), a faint scanline fringe on the
# outer band. Returns (opacity, fringe rgb).
SMOKE_HLSL = """
const float BANDS = 3;
float2 GRID = max(floor(float2(L, W)), 4);
const float M[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
float2 cell = floor(UV * GRID);
float2 p = (cell + 0.5) / GRID * 2 - 1;
float c = cos(R), s = sin(R);
float2 pr = float2(c * p.x - s * p.y, s * p.x + c * p.y);
// The rags keep moving while the puff lives (a domain warp scaled by its liveliness).
pr += 0.16 * Ag * float2(sin(Tm * 1.9 + S * 9 + pr.y * 2.6), cos(Tm * 1.4 + S * 5 + pr.x * 2.6));
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
float3 fringe = edge * lerp(float3(0.35, 0.55, 0.80), float3(0.50, 0.40, 0.30), row);
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


def _material(name, blend, build, shading=None):
    path = FOLDER + "/" + name
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", shading or (unreal.MaterialShadingModel.MSM_UNLIT if blend == unreal.BlendMode.BLEND_ADDITIVE
                                                       else unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE))
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
    os_build.save(path)
    unreal.log("muzzle fx material: " + path)


def _flash(m, node, custom, data, scalar, colour, mul):
    uv = node(unreal.MaterialExpressionTextureCoordinate)
    burst = custom(FLASH_HLSL, "Burst", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   {"UV": uv, "I": data(0), "S": data(1), "H": data(2), "R": data(3)})
    mel.connect_material_property(mul(burst, scalar("Brightness", 6.0)), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def _smoke(m, node, custom, data, scalar, colour, mul):
    uv = node(unreal.MaterialExpressionTextureCoordinate)
    puff = custom(SMOKE_HLSL, "Puff", unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                  {"UV": uv, "A": data(0), "S": data(1), "R": data(2), "T": data(3), "L": data(5), "W": data(6),
                   "Ag": data(7), "Tm": node(unreal.MaterialExpressionTime)})
    opacity = node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False)
    mel.connect_material_expressions(puff, "", opacity, "")
    fringe = node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=True, a=True)
    mel.connect_material_expressions(puff, "", fringe, "")
    # The cloud's grey-warm (Docs/RecoilWeapons-SmokeSim.md, 3.6): the shroud's wisps and the shot's cloud are one smoke.
    tint = colour("Colour", unreal.LinearColor(0.42, 0.42, 0.40, 1.0))
    mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Thin smoke scatters light through itself: lit from behind (the sun past the cloud) it glows too.
    mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.connect_material_property(scalar("Roughness", 1.0), "", unreal.MaterialProperty.MP_ROUGHNESS)
    # A little light of its own (the old 0.25 was milk), plus the scanline fringe on its outer band.
    glow = node(unreal.MaterialExpressionAdd)
    mel.connect_material_expressions(mul(tint, scalar("Ambient", 0.04)), "", glow, "A")
    mel.connect_material_expressions(mul(fringe, scalar("Fringe", 0.25)), "", glow, "B")
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


# The volumetric cloud (os.Fx.VolumetricSmoke 1, Docs/RecoilWeapons-SmokeSim.md, «Правка v2»): a heterogeneous
# volume marches this Volume-domain material and lights it with real self-shadowing and scattering. The gas is
# simulated on the CPU (FOSSmokeSim) and arrives as an atlas (slice z at tile (z % 8, z / 8), 8 tiles across):
# r the drawn density (the gas less its blur: rags, not a cushion), g heat, b the key light's shadow, a the ambient's.
# The low-res identity is tied to the screen, not to the grid: the material's pixel is ~5 px at 1080p at any
# distance (a power of two in cm, from the distance to the eye: it does not swim as the cloud drifts), the grid
# sampled trilinearly at the pixel's centre and cut into 4 bands - a very thin lowest one (a veil, not a step into
# grey) - with a 3D Bayer dither on the pixel grid. Erosion from 0.5 s eats thin pixels first (a per-pixel
# threshold); the hottest pixels of the fresh core glow as flickering embers. Returns (extinction share, warm
# share, ember, lit = key shadow x ambient shadow). Everything in box space (CloudX/Y/Z, cm).
VOLUME_HLSL = """
const float M[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
const float SHARE[5] = { 0, 0.1, 0.35, 0.65, 1 };
float3 d = W - C;
float3 l = float3(dot(d, BX), dot(d, BY), dot(d, BZ));
if (any(abs(l) >= H)) return 0;
float eye = length(l - E.xyz);
// The eye clears: nothing this close (flying through a cloud never fills the lens).
if (eye < E.w) return 0;
float cell = exp2(floor(log2(max(1.0, eye * Px))));
float3 q = floor(l / cell);
float3 u = ((q + 0.5) * cell) / (2 * H) + 0.5;
if (any(u < 0) || any(u >= 1)) return 0;
int n = (int)(N + 0.5);
float3 p = u * n - 0.5;
// Trilinear: the texture's bilinear filter within a slice (two taps), a lerp between the two slices (8 loads cost
// 3.8 ms for three clouds). The tile's UV stays half a texel inside it: no bleed from the next slice.
float2 aw = float2(8 * n, ((n + 7) / 8) * n);
float2 xy = clamp(p.xy, 0, n - 1) + 0.5;
float zf = clamp(p.z, 0, n - 1);
int z0 = (int)floor(zf);
int z1 = min(z0 + 1, n - 1);
float4 g = lerp(Grid.SampleLevel(GridSampler, (xy + float2((z0 % 8) * n, (z0 / 8) * n)) / aw, 0),
                Grid.SampleLevel(GridSampler, (xy + float2((z1 % 8) * n, (z1 / 8) * n)) / aw, 0), zf - z0);
int3 qi = int3(q);
float th = (M[((qi.x + qi.z) & 3) + 4 * ((qi.y + 2 * qi.z) & 3)] + 0.5) / 16;
float hv = frac(sin(dot(q + S * 31, float3(127.1, 311.7, 74.7))) * 43758.5453);
float erode = Er * saturate((T - 0.5) / max(L - 0.5, 0.1)) * (0.35 + 0.65 * hv);
float dens = g.r * Th * Gn - erode;
int band = (int)min(floor(saturate(dens) * 4 + th), 4);
if (band < 1) return 0;
float flick = frac(sin(dot(q, float3(12.9898, 78.233, 37.719)) + floor(Tm * 24) * 0.618) * 43758.5453);
float ember = (g.g > 0.7 + 0.25 * th) ? saturate(g.g - 0.6) * 2.5 * (0.4 + 0.6 * flick) : 0;
return float4(SHARE[band], saturate(g.g * 1.4), ember, g.b * g.a);
"""


def _volume(m, node, custom, data, scalar, colour, mul):
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_VOLUME)
    m.set_editor_property("used_with_heterogeneous_volumes", True)
    world = node(unreal.MaterialExpressionWorldPosition)
    vec = {}
    for name in ["CloudCentre", "CloudX", "CloudY", "CloudZ", "Eye"]:
        vec[name] = colour(name, unreal.LinearColor(0, 0, 0, 0))
    grid = node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="Grid",
                texture=unreal.load_asset("/Engine/EngineResources/DefaultTexture"))
    inputs = {"W": world, "H": scalar("HalfCm", 80.0), "N": scalar("Cells", 32.0), "S": scalar("Seed", 0.0),
              "T": scalar("Age", 0.0), "L": scalar("Life", 6.0), "Th": scalar("Thin", 1.0), "Er": scalar("Erode", 0.9),
              "Gn": scalar("Gain", 1.0), "Px": scalar("PixelPerCm", 0.01227), "Tm": node(unreal.MaterialExpressionTime), "Grid": grid}
    rgba = {"C": "CloudCentre", "BX": "CloudX", "BY": "CloudY", "BZ": "CloudZ", "E": "Eye"}
    n = node(unreal.MaterialExpressionCustom, code=VOLUME_HLSL, description="Cloud",
             output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4,
             inputs=[_input(k) for k in list(inputs) + list(rgba)])
    for k, src in inputs.items():
        if not mel.connect_material_expressions(src, "", n, k):
            raise RuntimeError("link failed: " + k)
    for k, pname in rgba.items():
        # The 4-wide output of a vector parameter (the eye's w is its clear radius).
        if not mel.connect_material_expressions(vec[pname], "RGBA", n, k):
            raise RuntimeError("link failed: " + k)

    def channel(i):
        c = node(unreal.MaterialExpressionComponentMask, r=i == 0, g=i == 1, b=i == 2, a=i == 3)
        mel.connect_material_expressions(n, "", c, "")
        return c

    share, warm, ember, lit = channel(0), channel(1), channel(2), channel(3)
    # Grey-blue powder gas, a warm fresh core.
    albedo = node(unreal.MaterialExpressionLinearInterpolate)
    tint = colour("Colour", unreal.LinearColor(0.50, 0.52, 0.56, 1.0))
    mel.connect_material_expressions(tint, "", albedo, "A")
    mel.connect_material_expressions(colour("WarmColour", unreal.LinearColor(0.62, 0.50, 0.38, 1.0)), "", albedo, "B")
    mel.connect_material_expressions(warm, "", albedo, "Alpha")
    mel.connect_material_property(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Volume domain: the Subsurface Color pin is the extinction, integrated per local unit (a grid cell, UnitCm long:
    # HeterogeneousVolumesLiveShadingPipeline.usf, StepSize = world / LocalToWorldScale), so per cm times UnitCm.
    unit = scalar("UnitCm", 5.0)
    sigma = mul(mul(share, scalar("Extinction", 0.04)), unit)
    mel.connect_material_property(sigma, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    # The key light (C++: the brightest light at the cloud, the flash while it burns) as scattering, shadowed by the
    # gas toward it and around - a lit rim, a dark belly - plus a faint ambient (the old 0.12 was the milk). Embers.
    light = node(unreal.MaterialExpressionAdd)
    mel.connect_material_expressions(colour("KeyColour", unreal.LinearColor(1.0, 0.9, 0.8, 1.0)), "", light, "A")
    mel.connect_material_expressions(mul(colour("AmbientColour", unreal.LinearColor(0.7, 0.75, 0.85, 1.0)),
                                         mul(scalar("Ambient", 0.02), scalar("AmbientShare", 1.0))), "", light, "B")
    scatter = mul(mul(mul(sigma, albedo), light), mul(lit, scalar("Key", 1.0)))
    glow = node(unreal.MaterialExpressionAdd)
    mel.connect_material_expressions(scatter, "", glow, "A")
    mel.connect_material_expressions(mul(mul(ember, colour("EmberColour", unreal.LinearColor(2.2, 0.55, 0.10, 1.0))), unit), "", glow, "B")
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def build():
    _material("M_OS_MuzzleFlash", unreal.BlendMode.BLEND_ADDITIVE, _flash)
    _material("M_OS_MuzzleSmoke", unreal.BlendMode.BLEND_TRANSLUCENT, _smoke)
    # Subsurface, not Unlit or Default Lit: the volume reads its extinction from the Subsurface Color pin
    # (HeterogeneousVolumesLiveShadingUtils.ush, SampleExtinctionCoefficients: not for an Unlit material), and the
    # pin is compiled only for a subsurface shading model. Until 22.10 it was Unlit - no extinction, no
    # self-shadow, no scattering: the cloud was its emission alone (the milky veil).
    _material("M_OS_MuzzleSmokeVolume", unreal.BlendMode.BLEND_ADDITIVE, _volume, unreal.MaterialShadingModel.MSM_SUBSURFACE)


if __name__ == "__main__":
    build()
    unreal.SystemLibrary.quit_editor()
