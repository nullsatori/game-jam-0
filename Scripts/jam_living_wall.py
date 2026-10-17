"""AJamLivingWall's flesh material, shared by build_zerog.py and build_ship.py (editor Python).

M_Jam_LivingWall is made once (delete the .uasset to rebuild it after a graph
change); MI_Jam_LivingWall's numbers are rewritten on every map build. Exposed
wet muscle: T_MK_WoundWall_BC x Tint, striations around the fibre (along the
cylinder), paler tendon ends, darker thin vessels (instance custom data 1),
per-cell colour variation (custom data 2), a glow on thin spots (custom data 0),
a faint breathing swell (world position offset along the normal). Physical
material PM_MK_Flesh; used with instanced static meshes.
"""
import unreal

MATS = "/Game/Jam/Materials"
BASE = MATS + "/M_Jam_LivingWall"
INSTANCE = MATS + "/MI_Jam_LivingWall"

# Tuned on rendered frames (jam.Wall.Shots on both maps).
VECTORS = {
    "Tint": (2.4, 1.0, 0.95),          # T_MK_WoundWall_BC is dark: lift it to raw muscle red-pink
    "VesselTint": (0.7, 0.22, 0.25),   # thin threads: darker, bluish-red vessels
    "TendonColor": (0.85, 0.62, 0.55), # pale ends where fibres meet
    "GlowColor": (1.0, 0.3, 0.35),
}
SCALARS = {
    "AlongV": 0.0,        # 0: the cylinder's U runs along the fibre (1 drew rings across it on the frames)
    "StripeCount": 9.0,   # striations around a fibre
    "StripeDark": 0.55,   # brightness of the dark stripe
    "TendonAmount": 0.55,
    "Roughness": 0.3,     # wet
    "Specular": 0.75,
    "SelfLight": 0.4,     # red under the teal star of the ship map, which red flesh mostly absorbs
    "GlowIntensity": 0.8,
    "PulseCm": 0.5,       # breathing swell
    "PulseSpeed": 1.7,
}


def material(log):
    eal = unreal.EditorAssetLibrary
    mel = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    if not eal.does_asset_exist(BASE):
        m = tools.create_asset("M_Jam_LivingWall", MATS, unreal.Material, unreal.MaterialFactoryNew())
        m.set_editor_property("used_with_instanced_static_meshes", True)
        m.set_editor_property("phys_material", unreal.load_asset("/Game/MindKore/Materials/Physical/PM_MK_Flesh"))
        y = [0]

        def node(cls, **props):
            y[0] += 120
            n = mel.create_material_expression(m, cls, -1400, y[0])
            for k, v in props.items():
                n.set_editor_property(k, v)
            return n

        def link(a, b, pin=""):
            mel.connect_material_expressions(a, "", b, pin)

        def scalar(name):
            return node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=SCALARS[name])

        def vector(name):
            r, g, b = VECTORS[name]
            return node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(r, g, b, 1))

        def op(cls, a, b=None, const_b=None):
            n = node(cls)
            link(a, n, "A")
            if b is not None:
                link(b, n, "B")
            elif const_b is not None:
                n.set_editor_property("const_b", const_b)
            return n

        def lerp(a, b, alpha):
            n = node(unreal.MaterialExpressionLinearInterpolate)
            link(a, n, "A")
            link(b, n, "B")
            link(alpha, n, "Alpha")
            return n

        def custom(index):
            return node(unreal.MaterialExpressionPerInstanceCustomData, data_index=index)

        mul, add = unreal.MaterialExpressionMultiply, unreal.MaterialExpressionAdd
        uv = node(unreal.MaterialExpressionTextureCoordinate)
        u = node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False)
        v = node(unreal.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False)
        link(uv, u)
        link(uv, v)
        along_v = scalar("AlongV")
        along = lerp(u, v, along_v)
        around = lerp(v, u, along_v)
        # Striations: a sine around the fibre, 0..1.
        wave = node(unreal.MaterialExpressionSine)
        link(op(mul, around, scalar("StripeCount")), wave)
        stripe = op(add, op(mul, wave, const_b=0.5), const_b=0.5)
        # Tendon ends: |2 along - 1| ^ 6.
        ends_abs = node(unreal.MaterialExpressionAbs)
        link(op(add, op(mul, along, const_b=2.0), const_b=-1.0), ends_abs)
        ends = node(unreal.MaterialExpressionPower, const_exponent=6.0)
        link(ends_abs, ends, "Base")
        tex = node(unreal.MaterialExpressionTextureSample,
                   texture=unreal.load_asset("/Game/MindKore/Wounds/Textures/T_MK_WoundWall_BC"))
        link(uv, tex, "UVs")
        striped = lerp(scalar("StripeDark"), node(unreal.MaterialExpressionConstant, r=1.0), stripe)
        muscle = op(mul, op(mul, tex, vector("Tint")), striped)
        vessel = custom(1)
        colour = lerp(muscle, op(mul, muscle, vector("VesselTint")), vessel)
        not_vessel = node(unreal.MaterialExpressionOneMinus)
        link(vessel, not_vessel)
        tendon = op(mul, op(mul, ends, scalar("TendonAmount")), not_vessel)
        colour = lerp(colour, vector("TendonColor"), tendon)
        colour = op(mul, colour, op(add, op(mul, custom(2), const_b=0.45), const_b=0.78))
        mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(scalar("Roughness"), "", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.connect_material_property(scalar("Specular"), "", unreal.MaterialProperty.MP_SPECULAR)
        glow = op(mul, op(mul, vector("GlowColor"), scalar("GlowIntensity")), custom(0))
        emissive = op(add, op(mul, colour, scalar("SelfLight")), glow)
        mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        # Breathing: normal x PulseCm x sin(time x speed + a phase from the world position).
        phase = node(unreal.MaterialExpressionDotProduct)
        link(node(unreal.MaterialExpressionWorldPosition), phase, "A")
        link(node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0.004, 0.006, 0.005, 0)), phase, "B")
        breath = node(unreal.MaterialExpressionSine)
        link(op(add, op(mul, node(unreal.MaterialExpressionTime), scalar("PulseSpeed")), phase), breath)
        swell = op(mul, op(mul, node(unreal.MaterialExpressionVertexNormalWS), breath), scalar("PulseCm"))
        mel.connect_material_property(swell, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        mel.recompile_material(m)
        eal.save_asset(BASE, False)
        log("made " + BASE)
    if eal.does_asset_exist(INSTANCE):
        mi = unreal.load_asset(INSTANCE)
    else:
        mi = tools.create_asset("MI_Jam_LivingWall", MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, unreal.load_asset(BASE))
    for name, (r, g, b) in VECTORS.items():
        mel.set_material_instance_vector_parameter_value(mi, name, unreal.LinearColor(r, g, b, 1.0))
    for name, value in SCALARS.items():
        mel.set_material_instance_scalar_parameter_value(mi, name, value)
    eal.save_asset(INSTANCE, False)
    return mi
