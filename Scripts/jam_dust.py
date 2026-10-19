"""Space dust: M_Jam_Dust, the material of AJamZeroGPawn's dust specks (editor Python).

  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject -ExecutePythonScript="<path>/Scripts/jam_dust.py" -unattended -nop4 -nosplash -nullrhi

Also called by Scripts/build_zerog.py. Rebuilt in place on every run.

MindKore's dust motes (the Horror template's NS_DustMote in Lvl_MK_Admin) as a look,
smaller and dimmer (owner, 18.10): a soft glowing mote, additive, drifting slowly
and twinkling. The pawn's specks are engine cubes in an instanced mesh, kept still
in the world (they show the body's motion in zero-G); the material draws a soft
round core inside each cube - the distance of the view ray from the cube's centre,
in the cube's own space - so the cube's edges never show.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import jam_build  # noqa: E402

PATH = "/Game/Jam/Materials/M_Jam_Dust"
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

# Distance of the view ray from the speck's centre (engine cube: half edge 50 units).
MOTE_HLSL = """
float d = length(cross(P, normalize(V)));
return exp(-d * d / (Core * Core));
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
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Jam_Dust", "/Game/Jam/Materials", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("used_with_instanced_static_meshes", True)
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
                  {"P": local, "V": view, "Core": scalar("CoreUnits", 7.0)})
    rand = node(unreal.MaterialExpressionPerInstanceRandom)
    time = node(unreal.MaterialExpressionTime)
    # Twinkle: 0.55..1 at its own pace.
    twinkle = custom("return 0.775 + 0.225 * sin(T * (0.6 + R) * S + R * 6.2831853);", "Twinkle",
                     unreal.CustomMaterialOutputType.CMOT_FLOAT1, {"T": time, "R": rand, "S": scalar("TwinkleSpeed", 1.3)})
    colour = node(unreal.MaterialExpressionVectorParameter, parameter_name="Colour", default_value=unreal.LinearColor(0.75, 0.8, 0.9, 1.0))
    emissive = mul(mul(mul(colour, scalar("Brightness", 0.4)), mote), twinkle)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    drift = custom(DRIFT_HLSL, "Drift", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   {"T": time, "R": rand, "Amp": scalar("DriftCm", 6.0)})
    mel.connect_material_property(drift, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(m)
    jam_build.save(PATH)
    log("dust material: " + PATH)


if __name__ == "__main__":
    material()
