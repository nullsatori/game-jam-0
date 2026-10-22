"""The suit projector's materials for hull repair (UOSWeldProjectorComponent, UOSHullToolsComponent; editor Python).

  UnrealEditor-Cmd.exe <path>/OuterSpace.uproject -ExecutePythonScript="<path>/Scripts/os_weld_projector.py" -unattended -nop4 -nosplash -nullrhi

Rebuilt in place on every run. After MindKore's bodycam projector (L22: its
Scripts/author_projector_materials.py, M_MK_Projector_Picture and M_MK_Projector_Beam),
in the owner's noir key (19.10: dark blue, not MindKore's red): an ink-navy halo and a
pale blue-white core. The colours live on UOSWeldProjectorComponent (HaloColour,
CoreColour); the defaults here only stand in until the component sets them.

M_OS_WeldProjection - a Light Function on the suit projector's spot light. The
picture is a canvas raster the component draws on the device's 12 Hz clock, in the
breach's own frame (X along the hull, Y across, the hole round the origin), so the
contour stays nailed to the hull while the light moves with the camera between
ticks. MindKore's picture function maps the cone's UV instead (its raster is in the
projector's frame); here the lit pixel's world position is taken into the breach
frame: Raster covers 2 HalfCm square, and only a slab round the hull's outer face
(SurfaceZ - SlabBelowCm .. SurfaceZ + SlabAboveCm) takes light - the cone casts no
shadow (the hands would block the bead under the torch), so the slab keeps the
picture off the floor seen through the hole. The raster is read with mips: from
8 m a 1 px stroke would shimmer.
  Raster, Origin, AxisX, AxisY, AxisZ   set by the component (Origin in cm, axes unit)
  Channel   which raster channel this light reads: G the halo laser, R the core laser (MindKore's two lasers)
  HalfCm, SurfaceZ, SlabBelowCm, SlabAboveCm

M_OS_StrapLaser - a strap line as a laser thread: the engine cylinder stretched
between the line's two ends, additive, unlit, drawn after motion blur (past TSR,
which smears 1 px lines - the reticle's lesson). Its vertices are pushed out so it
is never under three pixels wide (MindKore's motes: "never under two pixels"), and
a thread pushed out dims by the square root of how much. The tension reads in its
form, not only its brightness: slack - a broken dashed navy line whose dashes drop
out on device ticks; taut - solid, brighter with the pull, pale beads running down
it to the anchor faster the harder it pulls, stepped on the 12 Hz clock; at the
limit - the whole core pale, flickering tick to tick. The beam's haze shimmer and
rare motes, anchored along the thread.
  Length (cm), Tension (0..1, negative = slack), RadiusCm, PixelAngle (rad per
  pixel of the view), Halo, Core, Glow, CoreGlow; ZMin, ZSpan: the mesh's local Z
  range (its bounds)

No texture: shapes are maths, nothing third-party.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_build  # noqa: E402

FOLDER = "/Game/OSArt/Materials/Hull"
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
# Defaults only: the component sets both from UOSWeldProjectorComponent's HaloColour / CoreColour.
HALO = unreal.LinearColor(0.045, 0.065, 0.15, 1.0)
CORE = unreal.LinearColor(0.62, 0.74, 1.0, 1.0)

PROJECTION_HLSL = """
float3 d = WorldPos - Origin;
float3 l = float3(dot(d, AxisX.xyz), dot(d, AxisY.xyz), dot(d, AxisZ.xyz));
if (l.z < SurfaceZ - SlabBelow || l.z > SurfaceZ + SlabAbove) { return 0.0; }
float2 uv = l.xy / (2.0 * Half) + 0.5;
if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { return 0.0; }
return saturate(dot(Texture2DSample(Tex, TexSampler, uv).rgb, Channel.rgb));
"""

INFLATE_HLSL = """
float dist = length(WorldPos - CamPos);
float seen = max(Radius, dist * PixelAngle * 1.5);
return normalize(Normal) * (seen - Radius);
"""

THREAD_HLSL = """
struct FThread
{
    float H(float a, float b) { return frac(sin(a * 12.9898 + b * 78.233) * 43758.5453); }
};
FThread F;
float facing = abs(dot(normalize(N), normalize(V)));
float s = sqrt(saturate(1.0 - facing * facing));
float dist = length(WorldPos - CamPos);
float seen = max(Radius, dist * PixelAngle * 1.5);
float thin = sqrt(Radius / seen);
float core = exp(-s * s * 14.0);
float glow = exp(-s * s * 2.5) * 0.3;
// Centimetres from end A; the device redraws on its 12 Hz clock.
float along = saturate((Local.z - ZMin) / ZSpan) * Length;
float tick = floor(Time * 12.0);
float level;
float hot = 0.0;
if (Tension < 0.0)
{
    // Slack: a broken line, its dashes dropping out tick to tick.
    float dash = floor(along / 14.0);
    float on = step(frac(along / 14.0), 0.55) * step(F.H(dash, tick), 0.78);
    level = 0.45 * on;
}
else
{
    // Taut: solid, brighter with the pull; pale beads run to the anchor (end A) faster the harder it pulls.
    float speed = 30.0 + 420.0 * Tension;
    float phase = frac((along + tick / 12.0 * speed) / 55.0);
    float bead = exp(-pow((phase - 0.5) * 55.0 / 5.0, 2.0));
    level = 0.55 + 0.6 * Tension;
    hot = bead * (0.3 + 0.7 * Tension);
    if (Tension >= 0.95)
    {
        // At the limit: the whole thread turns pale and unsteady.
        float flick = 0.65 + 0.35 * F.H(tick, 3.1);
        level = 1.3 * flick;
        hot = (0.55 + 0.6 * bead) * flick;
    }
}
// The beam's haze, along the thread, and a rare mote caught in it.
float shimmer = 0.85 + 0.15 * sin(along * 0.05 + Time * 1.3) * sin(along * 0.13 - Time * 0.7);
float mote = step(0.975, F.H(floor(along / 8.0), tick)) * (Tension < 0.0 ? 0.0 : 0.6);
// Noir: an ink-navy thread, a pale sliver only where it is brightest; the falloff is the thread's own, not bloom.
float3 halo = Halo.rgb * Glow * level * shimmer * (core + glow);
float3 sliver = Core.rgb * CoreGlow * (hot + mote) * core * core;
return (halo + sliver) * thin;
"""


def _input(name):
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", name)
    return ci


def _fresh(name):
    path = FOLDER + "/" + name
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        while mel.get_num_material_expressions(m):
            mel.delete_all_material_expressions(m)
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    return m, path


def _graph(m):
    y = [0]

    def node(cls, **props):
        y[0] += 140
        n = mel.create_material_expression(m, cls, -900, y[0])
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def custom(code, desc, out_type, inputs):
        n = node(unreal.MaterialExpressionCustom, code=code.strip(), description=desc, output_type=out_type,
                 inputs=[_input(k) for k in inputs])
        for k, src in inputs.items():
            if not mel.connect_material_expressions(src, "", n, k):
                raise RuntimeError("link failed: %s -> %s.%s" % (src.get_name(), desc, k))
        return n

    def scalar(name, value):
        return node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)

    def vector(name, value):
        return node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=value)

    return node, custom, scalar, vector


def build_projection():
    m, path = _fresh("M_OS_WeldProjection")
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    node, custom, scalar, vector = _graph(m)
    out = custom(PROJECTION_HLSL, "WeldProjection", unreal.CustomMaterialOutputType.CMOT_FLOAT1, {
        "WorldPos": node(unreal.MaterialExpressionWorldPosition),
        "Origin": vector("Origin", unreal.LinearColor(0.0, 0.0, -100000.0, 0.0)),
        "AxisX": vector("AxisX", unreal.LinearColor(1.0, 0.0, 0.0, 0.0)),
        "AxisY": vector("AxisY", unreal.LinearColor(0.0, 1.0, 0.0, 0.0)),
        "AxisZ": vector("AxisZ", unreal.LinearColor(0.0, 0.0, 1.0, 0.0)),
        "Tex": node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="Raster",
                    texture=unreal.load_asset("/Engine/EngineResources/Black"),
                    sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
        "Channel": vector("Channel", unreal.LinearColor(0.0, 1.0, 0.0, 0.0)),
        "Half": scalar("HalfCm", 220.0),
        "SurfaceZ": scalar("SurfaceZ", 10.0),
        "SlabBelow": scalar("SlabBelowCm", 40.0),
        "SlabAbove": scalar("SlabAboveCm", 70.0),
    })
    # With Substrate on, a light function's colour goes through the Substrate Light Function node;
    # emissive is wired as well for the classic path (MindKore's author_projector_materials.py).
    front = False
    if hasattr(unreal, "MaterialExpressionSubstrateLightFunction"):
        substrate = node(unreal.MaterialExpressionSubstrateLightFunction)
        mel.connect_material_expressions(out, "", substrate, "Color")
        front = mel.connect_material_property(substrate, "", unreal.MaterialProperty.MP_FRONT_MATERIAL)
    emissive = mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert front or emissive
    mel.recompile_material(m)
    os_build.save(path)
    unreal.log("OSWELD: built %s front %s emissive %s" % (path, front, emissive))


def build_strap():
    m, path = _fresh("M_OS_StrapLaser")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)
    node, custom, scalar, vector = _graph(m)
    world = node(unreal.MaterialExpressionWorldPosition)
    camera = node(unreal.MaterialExpressionCameraPositionWS)
    normal = node(unreal.MaterialExpressionVertexNormalWS)
    radius = scalar("RadiusCm", 0.6)
    pixel = scalar("PixelAngle", 0.0037)
    inflate = custom(INFLATE_HLSL, "StrapInflate", unreal.CustomMaterialOutputType.CMOT_FLOAT3, {
        "WorldPos": world, "CamPos": camera, "Normal": normal, "Radius": radius, "PixelAngle": pixel,
    })
    assert mel.connect_material_property(inflate, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    thread = custom(THREAD_HLSL, "StrapThread", unreal.CustomMaterialOutputType.CMOT_FLOAT3, {
        "N": normal, "V": node(unreal.MaterialExpressionCameraVectorWS), "WorldPos": world, "CamPos": camera,
        "Local": node(unreal.MaterialExpressionLocalPosition), "Time": node(unreal.MaterialExpressionTime),
        "Length": scalar("Length", 300.0), "Tension": scalar("Tension", 0.5), "Radius": radius,
        "PixelAngle": pixel, "Halo": vector("Halo", HALO), "Core": vector("Core", CORE),
        "Glow": scalar("Glow", 18.0), "CoreGlow": scalar("CoreGlow", 2.5),
        "ZMin": scalar("ZMin", -50.0), "ZSpan": scalar("ZSpan", 100.0),
    })
    assert mel.connect_material_property(thread, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    os_build.save(path)
    unreal.log("OSWELD: built %s" % path)


if __name__ == "__main__":
    build_projection()
    build_strap()
    unreal.SystemLibrary.quit_editor()
