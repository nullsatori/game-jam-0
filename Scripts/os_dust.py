"""Space dust: M_OS_Dust, the material of AOSZeroGPawn's dust specks (editor Python).

  UnrealEditor-Cmd.exe <path>/OuterSpace.uproject -ExecutePythonScript="<path>/Scripts/os_dust.py" -unattended -nop4 -nosplash -nullrhi

Also called by Scripts/build_zerog.py. Rebuilt in place on every run.

MindKore's dust motes (the Horror template's NS_DustMote in Lvl_MK_Admin) as a look,
smaller and dimmer (owner, 18.10): a glowing mote, additive, drifting slowly
and twinkling. The pawn's specks are engine cubes in an instanced mesh, kept still
in the world (they show the body's motion in zero-G); the material draws a round
dot inside each cube - the distance of the view ray from the cube's centre,
in the cube's own space - so the cube's edges never show.

Owner 22.10: "too bright white, blurry, big". The dot used to be a Gaussian sized in
the world, so a speck a hand's width from the lens grew into a soft blob tens of
pixels wide. Now it is a hard-edged disc whose radius is held between MinRadiusPx and
MaxRadiusPx screen pixels (a crisp 1-3 pixel dot, the low-res look); a speck smaller
than that loses brightness instead of growing, so far dust is dimmer. Warm grey and
dimmer, and Responsive AA so the temporal AA does not smear it.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_build  # noqa: E402

PATH = "/Game/OS/Materials/M_OS_Dust"
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

# Distance of the view ray from the speck's centre (engine cube: half edge 50 units).
# One screen pixel in the same units is fwidth(d). The disc keeps its world size
# (Core) while that lies between MinPx and MaxPx pixels; outside it is clamped, and a
# disc grown above Core is dimmer by Core/r, but never below FarDim (a full-energy
# version made far dust vanish under DLAA, run 22.10).
MOTE_HLSL = """
float d = length(cross(P, normalize(V)));
float px = max(fwidth(d), 1e-4);
float r = clamp(Core, MinPx * px, MaxPx * px);
float disc = saturate((r - d) / px + 0.5);
return disc * max(FarDim, min(1.0, Core / r));
"""
# Slow wander in world space, each speck its own phase.
DRIFT_HLSL = """
float p = R * 6.2831853;
return Amp * float3(sin(T * 0.31 + p), sin(T * 0.23 + p * 1.7), sin(T * 0.17 + p * 2.9));
"""


def _input(name):
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", name)
    return ci


def material(log=unreal.log):
    if eal.does_asset_exist(PATH):
        m = unreal.load_asset(PATH)
        mel.delete_all_material_expressions(m)
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_OS_Dust", "/Game/OS/Materials", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    # Less history for these pixels in TAA/TSR: a 1-3 pixel dot stays a dot.
    m.set_editor_property("enable_responsive_aa", True)
    y = [0]

    def node(cls, **props):
        y[0] += 120
        n = mel.create_material_expression(m, cls, -900, y[0])
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(a, b, pin="", out=""):
        if not mel.connect_material_expressions(a, out, b, pin):
            raise RuntimeError("link failed: %s -> %s.%s" % (a.get_name(), b.get_name(), pin))

    def scalar(name, value):
        return node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)

    def custom(code, desc, out_type, inputs):
        n = node(unreal.MaterialExpressionCustom, code=code, description=desc, output_type=out_type,
                 inputs=[_input(k) for k in inputs])
        for k, src in inputs.items():
            link(src, n, k)
        return n

    def mul(a, b):
        n = node(unreal.MaterialExpressionMultiply)
        link(a, n, "A")
        link(b, n, "B")
        return n

    # The mote: local position (vertex, interpolated) and the view direction in local space.
    local = node(unreal.MaterialExpressionVertexInterpolator)
    link(node(unreal.MaterialExpressionPreSkinnedPosition), local, "")
    view = node(unreal.MaterialExpressionTransform, transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    link(node(unreal.MaterialExpressionCameraVectorWS), view, "")
    mote = custom(MOTE_HLSL, "Mote", unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                  {"P": local, "V": view, "Core": scalar("CoreUnits", 5.0),
                   "MinPx": scalar("MinRadiusPx", 0.75), "MaxPx": scalar("MaxRadiusPx", 1.25),
                   "FarDim": scalar("FarDim", 0.35)})
    rand = node(unreal.MaterialExpressionPerInstanceRandom)
    time = node(unreal.MaterialExpressionTime)
    # Twinkle: 0.55..1 at its own pace.
    twinkle = custom("return 0.775 + 0.225 * sin(T * (0.6 + R) * S + R * 6.2831853);", "Twinkle",
                     unreal.CustomMaterialOutputType.CMOT_FLOAT1, {"T": time, "R": rand, "S": scalar("TwinkleSpeed", 1.3)})
    colour = node(unreal.MaterialExpressionVectorParameter, parameter_name="Colour", default_value=unreal.LinearColor(0.95, 0.82, 0.62, 1.0))
    emissive = mul(mul(mul(colour, scalar("Brightness", 0.3)), mote), twinkle)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    drift = custom(DRIFT_HLSL, "Drift", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   {"T": time, "R": rand, "Amp": scalar("DriftCm", 6.0)})
    mel.connect_material_property(drift, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(m)
    os_build.save(PATH)
    log("dust material: " + PATH)


if __name__ == "__main__":
    material()
