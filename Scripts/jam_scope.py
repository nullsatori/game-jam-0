"""The SV-98 scope's eyepiece material for UJamScopeComponent (editor Python).

  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject -ExecutePythonScript="<path>/Scripts/jam_scope.py" -unattended -nop4 -nosplash -nullrhi

Rebuilt in place on every run. M_Jam_ScopeLens is unlit and masked, drawn on
the engine plane (/Engine/BasicShapes/Plane) that the component lays on the
eyepiece facing the eye: the plane's U (its local X) runs up the gun, its V
(local Y) to the gun's right, UV 0..1 edge to edge. UV, not the local position:
the gun is drawn first person, and a pixel's local position comes back from
its first-person (scaled) world position. Parameters the component sets:

  Scene     the scope capture's render target (linear HDR scene colour)
  Eye       x, y: the eye's offset from the eyepiece's axis, in lens radii
            (x right, y down); z: how far the eye is off its relief, cm
  Unit      the reticle's unit in lens radii (its proportions; 0.03: the field
            stop is 33 units out)

Inside: the picture with a slight falloff to a hard black field stop; the
eye-relief shadow (a bright disc shifted by the eye's offset and shrinking as
the eye leaves its relief - outside it the tube's dark wall); a black PSO-1
style reticle - the aiming chevron, three holdover chevrons and a post under
it, the side windage scale with a tick per unit, and the rangefinder curve for
a 1.7 m figure (200 m on the left to 1000 m on the right) at the lower left.
At 3x the field is ~27 deg, so the reticle keeps PSO-1 proportions to the
field rather than true mils (true mils would be a few pixels). No texture: shapes are maths, nothing third-party.
"""
import unreal

FOLDER = "/Game/JamArt/Materials/Weapons"
NAME = "M_Jam_ScopeLens"
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

LENS_HLSL = """
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
float aa = max(fwidth(r), 1e-4);
float3 c = Texture2DSample(Tex, TexSampler, p * 0.5 + 0.5).rgb;
float field = 1 - smoothstep(0.97 - aa, 0.97 + aa, r);
float vig = 1 - 0.3 * pow(saturate(r / 0.97), 6);
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
// Half widths in units: a bold chevron, lighter holdovers, thin scales - thick
// enough to survive the upscaler (thin lines went grey at 1.5 px).
float ink = max(max(1 - smoothstep(0.35 - am, 0.35 + am, dc), 1 - smoothstep(0.25 - am, 0.25 + am, dh)),
    1 - smoothstep(0.16 - am, 0.16 + am, d));
float3 glass = float3(0.004, 0.006, 0.01);
return lerp(glass, c * vig, lit) * field * (1 - ink);
"""

MASK_HLSL = """
return length(UV * 2 - 1) <= 1.0 ? 1.0 : 0.0;
"""


def _input(name):
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", name)
    return ci


def build():
    path = FOLDER + "/" + NAME
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    y = [0]

    def node(cls, **props):
        y[0] += 140
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

    uv = node(unreal.MaterialExpressionTextureCoordinate)
    scene = node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="Scene",
                 texture=unreal.load_asset("/Engine/EngineResources/Black"))
    eye = node(unreal.MaterialExpressionVectorParameter, parameter_name="Eye",
               default_value=unreal.LinearColor(10.0, 10.0, 20.0, 0.0))
    unit = node(unreal.MaterialExpressionScalarParameter, parameter_name="Unit", default_value=0.03)
    lens = custom(LENS_HLSL, "ScopeLens", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                  {"UV": uv, "Tex": scene, "E": eye, "Unit": unit})
    mask = custom(MASK_HLSL, "ScopeMask", unreal.CustomMaterialOutputType.CMOT_FLOAT1, {"UV": uv})
    mel.connect_material_property(lens, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(m)
    eal.save_asset(path, False)
    unreal.log("JAMSCOPE: built %s" % path)


if __name__ == "__main__":
    build()
    unreal.SystemLibrary.quit_editor()
