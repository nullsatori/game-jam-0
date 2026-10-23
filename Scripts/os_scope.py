"""The SV-98 scope's eyepiece and the pistol's sight dots for UOSScopeComponent (editor Python).

  UnrealEditor-Cmd.exe <path>/OuterSpace.uproject -ExecutePythonScript="<path>/Scripts/os_scope.py" -unattended -nop4 -nosplash -nullrhi

Rebuilt in place on every run. Three materials:

M_OS_ScopeLens - unlit, masked, on the engine plane (/Engine/BasicShapes/Plane)
the component lays on the eyepiece facing the eye: the plane's U (its local X)
runs up the gun, its V (local Y) to the gun's right, UV 0..1 edge to edge. UV,
not the local position: the gun is drawn first person, and a pixel's local
position comes back from its first-person (scaled) world position. The picture
with a slight falloff to a hard black field stop; the eye-relief shadow (a
bright disc shifted by the eye's offset and shrinking as the eye leaves its
relief - outside it the tube's dark wall); a 1-2 px darkened rim round the
reticle's lines so they read on a bright wall.

M_OS_ScopeReticle - the reticle as a red laser projection, on a second plane
just in front of the glass: additive, translucency pass After Motion Blur
(past TSR, which smeared 1 px lines drawn in the lens). PSO-1 style - the
aiming chevron, three holdover chevrons and a post under it, the side windage
scale with a tick per unit, and the rangefinder curve for a 1.7 m figure (200 m
on the left to 1000 m on the right) at the lower left - in units of Unit lens
radii (the field is wide at 3x, so PSO-1 proportions to the field rather than
true mils). Lines 1-1.4 px with a 1 px glow, no wide halo. The light is
MindKore's bodycam projector laser (owner 18.10: «в стиле майндкора красный
подсвет», «в майндкоре есть проектор, можно оттуда эмиссив глянуть»): the red
of its RedLaser / M_MK_Projector_Beam (1.0, 0.045, 0.02), added over the
picture like the beam and driven like the projector - 1 in the dark, up to its
SunDrive 4 when the picture round the aim point is lit (from its LitRoomLuma
0.12 to 0.6) - with its character in 2D, kept faint (owner: the fat sparkles
were too much): drifting raster lines, grain, the haze's shimmer, the drive's
flicker with a rare dip, rare single-pixel motes on the lines. It dims with
the eye shadow.

Both read the parameters the component sets:

  Scene     the scope capture's render target (linear HDR scene colour)
  Eye       x, y: the eye's offset from the eyepiece's axis, in lens radii
            (x right, y down); z: how far the eye is off its relief, cm
  Unit      the reticle's unit in lens radii (0.018)
  Red, Glow the reticle's light: MindKore's laser red and its strength

M_OS_SightDot - the pistol's sight inserts (2 mm engine spheres on its slide):
the same red, a bright core where the dot faces the eye and a dark ring round
it, a slow shimmer and a rare dip.

No texture: shapes are maths, nothing third-party.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_build  # noqa: E402

FOLDER = "/Game/OSArt/Materials/Weapons"
NAME = "M_OS_ScopeLens"
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
# MindKore's laser red (MKBodycamProjectorComponent RedLaser, M_MK_Projector_Beam RedColour).
RED = unreal.LinearColor(1.0, 0.045, 0.02, 1.0)

GEOM_HLSL = """
struct FLines
{
    float Seg(float2 q, float2 a, float2 b)
    {
        float2 pa = q - a, ba = b - a;
        float h = saturate(dot(pa, ba) / dot(ba, ba));
        return length(pa - ba * h);
    }
};
FLines F;
float2 p = float2(UV.y * 2 - 1, 1 - UV.x * 2);
float r = length(p);
// One pixel in lens radii (fwidth sums both axes: halve it).
float aa = max(0.5 * fwidth(r), 1e-4);
float2 q0 = p * 0.5 + 0.5;
float fringe = 0.012 * dot(p, p);
float3 c = float3(Texture2DSample(Tex, TexSampler, (p * (1 + fringe)) * 0.5 + 0.5).r,
                  Texture2DSample(Tex, TexSampler, q0).g,
                  Texture2DSample(Tex, TexSampler, (p * (1 - fringe)) * 0.5 + 0.5).b);
float field = 1 - smoothstep(0.97 - aa, 0.97 + aa, r);
float vig = 1 - 0.45 * pow(saturate(r / 0.97), 4);
float open = saturate(1 - abs(E.z) * 0.07);
float lit = 1 - smoothstep(open * 1.05 - 0.12, open * 1.05 + 0.02, length(p - E.xy));
float2 m = p / Unit;
float am = aa / Unit;
float dc = 1e3;
dc = min(dc, F.Seg(m, float2(0, 0), float2(-2.4, 3.6)));
dc = min(dc, F.Seg(m, float2(0, 0), float2(2.4, 3.6)));
float dh = 1e3;
for (int i = 1; i <= 3; i++)
{
    float y = 3.6 + i * 2.2;
    dh = min(dh, F.Seg(m, float2(0, y), float2(-1.0, y + 1.4)));
    dh = min(dh, F.Seg(m, float2(0, y), float2(1.0, y + 1.4)));
}
float d = F.Seg(m, float2(0, 12.6), float2(0, 40.0));
d = min(d, F.Seg(m, float2(3, 0), float2(12, 0)));
d = min(d, F.Seg(m, float2(-3, 0), float2(-12, 0)));
for (int k = 3; k <= 12; k++)
{
    float t = (k % 5 == 0) ? 1.4 : 0.7;
    d = min(d, F.Seg(m, float2(k, 0), float2(k, t)));
    d = min(d, F.Seg(m, float2(-k, 0), float2(-k, t)));
}
d = min(d, F.Seg(m, float2(-19, 18), float2(-9, 18)));
float2 prev = float2(-19, 18 - 8.5);
for (int j = 1; j <= 20; j++)
{
    float x = -19 + j * 0.5;
    float2 q = float2(x, 18 - 1700 / (200 + (x + 19) / 10 * 800));
    d = min(d, F.Seg(m, prev, q));
    prev = q;
}
// Line widths in pixels (am = one pixel in units): a laser-drawn mark, crisp -
// the chevron 1.4 px, holdovers 1.2, scales 1 - with a 1 px glow and no wider
// halo (owner 18.10: the blurred halo was wrong).
float cC = 1 - smoothstep(0.7 * am - 0.5 * am, 0.7 * am + 0.5 * am, dc);
float cH = 1 - smoothstep(0.6 * am - 0.5 * am, 0.6 * am + 0.5 * am, dh);
float cL = 1 - smoothstep(0.5 * am - 0.5 * am, 0.5 * am + 0.5 * am, d);
float dAll = min(min(dc, dh), d);
float core = max(max(cC, cH * 0.9), cL * 0.7);
float edge = (1 - smoothstep(0.7 * am, 1.8 * am, dAll)) * 0.2;
// A faint darkened pixel round the lines keeps them on a bright wall.
float rim = (1 - smoothstep(1.2 * am, 2.2 * am, dAll)) * (1 - core);
"""

LENS_HLSL = GEOM_HLSL + """
// The light in the tube: the picture's mean round the middle, a faint bounce on the metal.
float3 amb = (Texture2DSample(Tex, TexSampler, float2(0.5, 0.5)).rgb + Texture2DSample(Tex, TexSampler, float2(0.3, 0.5)).rgb
    + Texture2DSample(Tex, TexSampler, float2(0.7, 0.5)).rgb + Texture2DSample(Tex, TexSampler, float2(0.5, 0.3)).rgb
    + Texture2DSample(Tex, TexSampler, float2(0.5, 0.7)).rgb) * 0.2;
// The tube seen past the exit pupil: dark anodised wall with three baffle rings, each deeper ring
// smaller and shifted further by the eye's offset (parallax), darker inward.
float3 tube = float3(0.006, 0.0065, 0.008) * (1.2 - 0.6 * r);
for (int b = 1; b <= 3; b++)
{
    float depth = b / 3.0;
    float2 centre = E.xy * (0.25 + 0.45 * depth);
    float edge = abs(length(p - centre) - (0.93 - 0.14 * depth));
    float ring = 1 - smoothstep(aa, aa + 0.022, edge);
    tube += ring * (amb * 0.09 + 0.012) * (1.1 - 0.3 * b);
}
// Coated glass: a thin green-amber sheen just inside the field stop, a dark ring at the stop.
float coat = smoothstep(0.86, 0.95, r) * (1 - smoothstep(0.95, 0.968, r));
float3 sheen = coat * (float3(0.014, 0.024, 0.009) + 0.1 * amb * float3(0.6, 1.0, 0.45));
float stop = 1 - smoothstep(0.95, 0.965, r) * (1 - smoothstep(0.965, 0.975, r)) * 0.9;
// A soft glint on the ocular, upper left, drifting against the eye's offset (it moves with the head).
float2 g = p - (float2(-0.38, -0.42) - E.xy * 0.25);
float glint = exp(-dot(g * float2(2.6, 5.0), g * float2(2.6, 5.0))) * 0.035 + exp(-dot(g, g) * 260.0) * 0.07;
float3 picture = c * vig;
return (lerp(tube, picture, lit) * (1 - 0.6 * rim) * stop + sheen + glint * float3(0.9, 0.95, 1.0)) * field;
"""

RETICLE_HLSL = GEOM_HLSL + """
// MindKore's projector, in 2D: the raster's scan lines drifting through the
// light, per-pixel grain, the haze's slow shimmer along the mark, the drive's
// flicker with a rare dropout, and a few motes sparkling in the beam by the chevron.
float2 px = p / max(aa, 1e-4);
float scan = 0.88 + 0.12 * sin(px.y * 2.1 + Time * 31.0);
float2 cell = floor(px * 0.5);
float frame = floor(Time * 30.0);
float grain = frac(sin(dot(cell + frame * float2(17.3, 5.1), float2(127.1, 311.7))) * 43758.5453);
float shimmer = 0.92 + 0.08 * sin(Time * 6.3 + m.x * 2.1 + m.y * 1.3) * sin(Time * 10.7 - m.y * 2.9);
float tick = floor(Time * 18.0);
float dropout = frac(sin(tick * 12.9898) * 43758.5453) > 0.985 ? 0.7 : 1.0;
float flicker = (0.97 + 0.03 * sin(Time * 53.0) * sin(Time * 17.0)) * dropout;
// Motes: single pixels, rare, only on and right beside the lines (owner: the fat sparkles were too much).
float mote = step(0.996, frac(sin(dot(floor(px) + floor(Time * 12.0) * float2(3.7, 9.1), float2(269.5, 183.3))) * 43758.5453))
    * (1 - smoothstep(0.8 * am, 1.6 * am, dAll));
// The projector's drive from the picture round the aim point: 1 in the dark, up
// to its SunDrive 4 over a lit picture (its LitRoomLuma 0.12 .. 0.6).
float3 around = Texture2DSample(Tex, TexSampler, float2(0.5, 0.5)).rgb + Texture2DSample(Tex, TexSampler, float2(0.47, 0.5)).rgb
    + Texture2DSample(Tex, TexSampler, float2(0.53, 0.5)).rgb + Texture2DSample(Tex, TexSampler, float2(0.5, 0.47)).rgb
    + Texture2DSample(Tex, TexSampler, float2(0.5, 0.53)).rgb;
float luma = dot(around * 0.2, float3(0.2126, 0.7152, 0.0722));
float drive = lerp(1.0, 4.0, saturate((luma - 0.12) / 0.48)) * flicker;
float laser = (core * scan * (0.9 + 0.2 * grain) * shimmer + edge * shimmer + mote * 0.6) * drive;
return Red.rgb * Glow * laser * lit * field;
"""

MASK_HLSL = """
return length(UV * 2 - 1) <= 1.0 ? 1.0 : 0.0;
"""


def _input(name):
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", name)
    return ci


def _fresh(name):
    path = FOLDER + "/" + name
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    return m, path


DOT_HLSL = """
// A tritium / fibre insert seen end on: a small bright core where the dot faces
// the eye, the rest of the insert dark - the ring round it; the projector's slow
// shimmer and a rare dip, no per-frame grain (the upscaler would smear it).
float facing = saturate(dot(normalize(N), normalize(V)));
float core = pow(facing, 12.0);
float shimmer = 0.9 + 0.1 * sin(Time * 6.3) * sin(Time * 10.7);
float dip = frac(sin(floor(Time * 18.0) * 12.9898) * 43758.5453) > 0.985 ? 0.6 : 1.0;
return Red.rgb * Glow * core * shimmer * dip + float3(0.012, 0.004, 0.004) * (1.0 - core);
"""


def build_dot():
    """The pistol's sight inserts: tiny, a bright core in a dark ring, the projector's red."""
    m, path = _fresh("M_OS_SightDot")
    custom = mel.create_material_expression(m, unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property("code", DOT_HLSL)
    custom.set_editor_property("description", "SightDot")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("inputs", [_input(k) for k in ("N", "V", "Time", "Red", "Glow")])
    normal = mel.create_material_expression(m, unreal.MaterialExpressionVertexNormalWS, -600, 0)
    view = mel.create_material_expression(m, unreal.MaterialExpressionCameraVectorWS, -600, 100)
    time = mel.create_material_expression(m, unreal.MaterialExpressionTime, -600, 200)
    colour = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -600, 300)
    colour.set_editor_property("parameter_name", "Red")
    colour.set_editor_property("default_value", RED)
    glow = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -600, 400)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 6.0)
    for src, pin in ((normal, "N"), (view, "V"), (time, "Time"), (colour, "Red"), (glow, "Glow")):
        if not mel.connect_material_expressions(src, "", custom, pin):
            raise RuntimeError("dot link failed: " + pin)
    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    os_build.save(path)
    unreal.log("OSSCOPE: built %s" % path)


def _graph(m):
    y = [0]

    def node(cls, **props):
        y[0] += 140
        n = mel.create_material_expression(m, cls, -900, y[0])
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def custom(code, desc, out_type, inputs):
        n = node(unreal.MaterialExpressionCustom, code=code, description=desc, output_type=out_type,
                 inputs=[_input(k) for k in inputs])
        for k, src in inputs.items():
            if not mel.connect_material_expressions(src, "", n, k):
                raise RuntimeError("link failed: %s -> %s.%s" % (src.get_name(), n.get_name(), k))
        return n

    inputs = {
        "UV": node(unreal.MaterialExpressionTextureCoordinate),
        "Tex": node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="Scene",
                    texture=unreal.load_asset("/Engine/EngineResources/Black")),
        "E": node(unreal.MaterialExpressionVectorParameter, parameter_name="Eye",
                  default_value=unreal.LinearColor(10.0, 10.0, 20.0, 0.0)),
        "Unit": node(unreal.MaterialExpressionScalarParameter, parameter_name="Unit", default_value=0.018),
        "Red": node(unreal.MaterialExpressionVectorParameter, parameter_name="Red", default_value=RED),
        "Glow": node(unreal.MaterialExpressionScalarParameter, parameter_name="Glow", default_value=2.5),
        "Time": node(unreal.MaterialExpressionTime),
    }
    return node, custom, inputs


def build():
    m, path = _fresh(NAME)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    node, custom, inputs = _graph(m)
    lens = custom(LENS_HLSL, "ScopeLens", unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs)
    mask = custom(MASK_HLSL, "ScopeMask", unreal.CustomMaterialOutputType.CMOT_FLOAT1, {"UV": inputs["UV"]})
    mel.connect_material_property(lens, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(m)
    os_build.save(path)
    unreal.log("OSSCOPE: built %s" % path)


def build_reticle():
    """The laser layer: additive, drawn after motion blur - past TSR, which ate 1 px lines in the lens."""
    m, path = _fresh("M_OS_ScopeReticle")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)
    node, custom, inputs = _graph(m)
    laser = custom(RETICLE_HLSL, "ScopeReticle", unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs)
    mel.connect_material_property(laser, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    os_build.save(path)
    unreal.log("OSSCOPE: built %s" % path)


if __name__ == "__main__":
    build()
    build_reticle()
    build_dot()
    unreal.SystemLibrary.quit_editor()
