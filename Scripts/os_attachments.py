"""The weapon attachments' assets (Docs/Attachments.md; editor Python, headless).

  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject -ExecutePythonScript="<path>/Scripts/os_attachments.py" -unattended -nop4 -nosplash -nullrhi

Rebuilt in place on every run; ffmpeg on PATH. Work files go to %TEMP%/os_attachments (not the repository).

M_OS_Projection - the suit projector's light function (UOSSuitProjectorComponent). The picture is
the device's canvas raster in the projector's own frame: a lit point P maps to
u = dot(P - Origin, AxisY) / (dot(P - Origin, AxisX) TanHalf.x), v the same with AxisZ and TanHalf.y,
the raster pixel ((0.5 + 0.5 u), (0.5 - 0.5 v)); outside the frustum, or behind it, dark. Channel picks
the laser (R core, G halo), as M_OS_WeldProjection does. Read with mips (the raster's own).
  Raster, Channel, TanHalf (x, y), Origin, AxisX, AxisY, AxisZ   set by the component

M_OS_ClearMag - the 20-round magazine's body (owner 22.10 14:19, the reference photo: an ETS-style
smoky clear polymer). Translucent, two-sided, lit: a dark smoke tint seen through face on, the walls
thickening toward the edges (Fresnel), a glossy surface; the rounds and the spring read through it.

SW_OS_Shot_Pistol_Suppressed - SW_OS_Shot_Pistol filtered: the crack gone (low-pass 2.2 kHz), the
lows thinned (high-pass 90 Hz), 12 dB down (the game plays it at full volume), the tail cut to 0.6 s.

Meshes, /Game/OSArt/Meshes/Weapons, low poly, flat shaded, in centimetres (UE axes):
  SM_OS_Attach_Laser       3.2 x 1.6 x 1.4 cm, centred, lens at +X (slots Body, Lens)
  SM_OS_Attach_Torch       3 x 2 x 2 cm, centred, lens at +X (Body, Lens)
  SM_OS_Attach_Suppressor  2.6 x 12 cm along Z, centred, bore at -Z (Body)
  SM_OS_USP_MagLong        SM_OS_USP_Mag cut at mid-height and its lower half moved 3 cm down the well
                           (FOSWeapon::MagTravelCm's axis) - the lips and the floor plate as they were
They replace the engine cube and cylinder the catalog used (OSMakeAttachments: SeatOffset's scale 1).

Lvl_OS_TestRoom: four AOSAttachmentPickup by the gun pickups (Pickup_ATT_<Id>), each showing its
module four times its size on a pedestal.

DA_Test_Attachments (/Game/OS/Debug/Presets): a debug menu preset whose gear opens the pistol and the
shotgun, puts the laser on the pistol and the torch, suppressor and 20-round magazine in the pouch.

No texture: shapes are maths, nothing third-party.
"""
import math
import os
import subprocess
import sys
import tempfile

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_build  # noqa: E402

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
WORK = os.path.join(tempfile.gettempdir(), "os_attachments")
MESHES = "/Game/OSArt/Meshes/Weapons"
GUN_METAL = "/Game/OSArt/Materials/Weapons/MI_OS_Gun_BlackOxide"
CLEAR = "/Game/OSArt/Materials/Weapons/M_OS_ClearMag"
DOT = "/Game/OSArt/Materials/Weapons/M_OS_SightDot"
# The USP's well (OSWeaponParts.cpp: Pistol.MagTravelCm, MagPivot) in the gun's frame, which the magazine's mesh shares.
WELL = (0.275, 0.0, -0.962)
MAG_TOP = (-0.26, -1.43, 0.5)
MAG_EXTRA_CM = 3.0


def log(msg):
    unreal.log("OSATTACH: " + msg)


# --- materials -------------------------------------------------------------------

PROJECTION_HLSL = """
float3 d = WorldPos - Origin;
float x = dot(d, AxisX.xyz);
if (x < 1.0) { return 0.0; }
float2 uv = float2(0.5 + 0.5 * dot(d, AxisY.xyz) / (x * TanHalf.x), 0.5 - 0.5 * dot(d, AxisZ.xyz) / (x * TanHalf.y));
if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { return 0.0; }
return saturate(dot(Texture2DSample(Tex, TexSampler, uv).rgb, Channel.rgb));
"""


def _input(name):
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", name)
    return ci


def _fresh_material(folder, name):
    path = folder + "/" + name
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    return m, path


def _node(m, cls, y, **props):
    n = mel.create_material_expression(m, cls, -700, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def build_projection():
    m, path = _fresh_material("/Game/OSArt/Materials/Hull", "M_OS_Projection")
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    pins = {
        "WorldPos": _node(m, unreal.MaterialExpressionWorldPosition, 0),
        "Origin": _node(m, unreal.MaterialExpressionVectorParameter, 140, parameter_name="Origin", default_value=unreal.LinearColor(0, 0, -100000, 0)),
        "AxisX": _node(m, unreal.MaterialExpressionVectorParameter, 280, parameter_name="AxisX", default_value=unreal.LinearColor(1, 0, 0, 0)),
        "AxisY": _node(m, unreal.MaterialExpressionVectorParameter, 420, parameter_name="AxisY", default_value=unreal.LinearColor(0, 1, 0, 0)),
        "AxisZ": _node(m, unreal.MaterialExpressionVectorParameter, 560, parameter_name="AxisZ", default_value=unreal.LinearColor(0, 0, 1, 0)),
        "TanHalf": _node(m, unreal.MaterialExpressionVectorParameter, 700, parameter_name="TanHalf", default_value=unreal.LinearColor(0.625, 0.354, 0, 0)),
        "Tex": _node(m, unreal.MaterialExpressionTextureObjectParameter, 840, parameter_name="Raster",
                     texture=unreal.load_asset("/Engine/EngineResources/Black"), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
        "Channel": _node(m, unreal.MaterialExpressionVectorParameter, 980, parameter_name="Channel", default_value=unreal.LinearColor(0, 1, 0, 0)),
    }
    out = _node(m, unreal.MaterialExpressionCustom, 400, code=PROJECTION_HLSL.strip(), description="Projection",
                output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1, inputs=[_input(k) for k in pins])
    out_pins = {"TanHalf": "RG"}
    for k, src in pins.items():
        if k in out_pins:
            mask = _node(m, unreal.MaterialExpressionComponentMask, 1100, r=True, g=True, b=False, a=False)
            mel.connect_material_expressions(src, "", mask, "")
            src = mask
        if not mel.connect_material_expressions(src, "", out, k):
            raise RuntimeError("projection link failed: " + k)
    # As M_OS_WeldProjection: the Substrate light function node when Substrate is on, emissive for the classic path.
    front = False
    if hasattr(unreal, "MaterialExpressionSubstrateLightFunction"):
        substrate = _node(m, unreal.MaterialExpressionSubstrateLightFunction, 1240)
        mel.connect_material_expressions(out, "", substrate, "Color")
        front = mel.connect_material_property(substrate, "", unreal.MaterialProperty.MP_FRONT_MATERIAL)
    emissive = mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert front or emissive
    mel.recompile_material(m)
    os_build.save(path)
    log("built %s (front %s, emissive %s)" % (path, front, emissive))


def build_clear_mag():
    m, path = _fresh_material("/Game/OSArt/Materials/Weapons", "M_OS_ClearMag")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    fresnel = _node(m, unreal.MaterialExpressionFresnel, 0, exponent=2.5, base_reflect_fraction=0.04)
    tint = _node(m, unreal.MaterialExpressionVectorParameter, 140, parameter_name="Smoke", default_value=unreal.LinearColor(0.045, 0.04, 0.05, 1))
    edge = _node(m, unreal.MaterialExpressionVectorParameter, 280, parameter_name="Edge", default_value=unreal.LinearColor(0.16, 0.15, 0.17, 1))
    face = _node(m, unreal.MaterialExpressionScalarParameter, 420, parameter_name="FaceOpacity", default_value=0.22)
    rim = _node(m, unreal.MaterialExpressionScalarParameter, 560, parameter_name="EdgeOpacity", default_value=0.85)
    colour = _node(m, unreal.MaterialExpressionLinearInterpolate, 700)
    opacity = _node(m, unreal.MaterialExpressionLinearInterpolate, 840)
    for src, dst, pin in ((tint, colour, "A"), (edge, colour, "B"), (fresnel, colour, "Alpha"),
                          (face, opacity, "A"), (rim, opacity, "B"), (fresnel, opacity, "Alpha")):
        if not mel.connect_material_expressions(src, "", dst, pin):
            raise RuntimeError("clear mag link failed: " + pin)
    rough = _node(m, unreal.MaterialExpressionScalarParameter, 980, parameter_name="Roughness", default_value=0.12)
    spec = _node(m, unreal.MaterialExpressionScalarParameter, 1120, parameter_name="Specular", default_value=0.7)
    for src, prop in ((colour, unreal.MaterialProperty.MP_BASE_COLOR), (opacity, unreal.MaterialProperty.MP_OPACITY),
                      (rough, unreal.MaterialProperty.MP_ROUGHNESS), (spec, unreal.MaterialProperty.MP_SPECULAR)):
        if not mel.connect_material_property(src, "", prop):
            raise RuntimeError("clear mag property failed: %s" % prop)
    mel.recompile_material(m)
    os_build.save(path)
    log("built " + path)


# --- the suppressed shot ---------------------------------------------------------

def build_suppressed_shot():
    task = unreal.AssetExportTask()
    task.object = unreal.load_asset("/Game/OSArt/Audio/Weapons/SW_OS_Shot_Pistol")
    task.filename = os.path.join(WORK, "SW_OS_Shot_Pistol.wav")
    task.automated = True
    task.replace_identical = True
    task.prompt = False
    if not unreal.Exporter.run_asset_export_task(task):
        raise RuntimeError("could not export SW_OS_Shot_Pistol")
    wav_dir = os.path.join(WORK, "wav")
    os.makedirs(wav_dir, exist_ok=True)
    out = os.path.join(wav_dir, "SW_OS_Shot_Pistol_Suppressed.wav")
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", task.filename, "-af",
                    "highpass=f=90,lowpass=f=2200,lowpass=f=2200,volume=-12dB,atrim=0:0.6,afade=t=out:st=0.35:d=0.25",
                    "-c:a", "pcm_s16le", out], check=True)
    imp = unreal.AssetImportTask()
    imp.filename = out
    imp.destination_path = "/Game/OSArt/Audio/Weapons"
    imp.replace_existing = True
    imp.automated = True
    imp.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([imp])
    if not imp.imported_object_paths:
        raise RuntimeError("SW_OS_Shot_Pistol_Suppressed did not import")
    wave = unreal.load_asset(imp.imported_object_paths[0])
    log("built %s, %.2f s" % (imp.imported_object_paths[0], wave.get_editor_property("duration")))


# --- meshes ------------------------------------------------------------------------

class Mesh:
    """Flat-shaded polygons (convex, counter-clockwise seen from outside) with a material slot each."""

    def __init__(self):
        self.polys = []  # (slot, [points], [uvs] or None)

    def poly(self, slot, pts, uvs=None):
        self.polys.append((slot, [tuple(p) for p in pts], uvs))

    def prism_x(self, slot, profile, x0, x1, cap0=None, cap1=None):
        """A profile in (y, z), counter-clockwise seen from +X, extruded from x0 to x1."""
        n = len(profile)
        for i in range(n):
            (y0, z0), (y1, z1) = profile[i], profile[(i + 1) % n]
            self.poly(slot, [(x0, y0, z0), (x0, y1, z1), (x1, y1, z1), (x1, y0, z0)])
        self.poly(cap1 or slot, [(x1, y, z) for y, z in profile])
        self.poly(cap0 or slot, [(x0, y, z) for y, z in reversed(profile)])

    def prism_z(self, slot, profile, z0, z1, r0=1.0, r1=1.0, cap0=True, cap1=True):
        """A profile in (x, y), counter-clockwise seen from +Z, from z0 to z1, scaled r0 / r1 at its ends."""
        n = len(profile)
        for i in range(n):
            (xa, ya), (xb, yb) = profile[i], profile[(i + 1) % n]
            self.poly(slot, [(xa * r0, ya * r0, z0), (xb * r0, yb * r0, z0), (xb * r1, yb * r1, z1), (xa * r1, ya * r1, z1)])
        if cap1:
            self.poly(slot, [(x * r1, y * r1, z1) for x, y in profile])
        if cap0:
            self.poly(slot, [(x * r0, y * r0, z0) for x, y in reversed(profile)])


def chamfer_rect(hy, z0, z1, c, yc=0.0):
    """A rectangle |y - yc| <= hy, z0..z1, its corners cut by c; counter-clockwise seen from +X."""
    return [(yc - hy + c, z0), (yc + hy - c, z0), (yc + hy, z0 + c), (yc + hy, z1 - c),
            (yc + hy - c, z1), (yc - hy + c, z1), (yc - hy, z1 - c), (yc - hy, z0 + c)]


def circle(r, n, yc=0.0, zc=0.0, turn=0.5):
    return [(yc + r * math.cos(2 * math.pi * (i + turn) / n), zc + r * math.sin(2 * math.pi * (i + turn) / n)) for i in range(n)]


def laser_mesh():
    m = Mesh()
    m.prism_x("Body", chamfer_rect(0.8, -0.7, 0.42, 0.22), -1.5, 1.3)               # the housing
    m.prism_x("Body", chamfer_rect(0.55, 0.42, 0.7, 0.06), -1.15, 0.95)              # the rail clamp
    m.prism_x("Body", chamfer_rect(0.86, 0.47, 0.6, 0.03), -0.35, -0.05)             # the cross bolt
    m.prism_x("Body", circle(0.42, 8, 0.0, -0.12), 1.3, 1.5)                         # the lens barrel
    m.prism_x("Body", circle(0.3, 8, 0.0, -0.12), 1.5, 1.6, cap1="Lens")             # the lens
    m.prism_x("Body", chamfer_rect(0.32, -0.42, 0.12, 0.08), -1.6, -1.5)             # the switch pad
    return m


def torch_mesh():
    m = Mesh()
    m.prism_x("Body", chamfer_rect(0.75, -0.9, 0.72, 0.28), -1.5, 0.35)              # the body
    m.prism_x("Body", chamfer_rect(0.6, 0.72, 1.0, 0.07), -1.05, 0.45)               # the rail clamp
    m.prism_x("Body", circle(0.82, 10, 0.0, -0.05), 0.35, 0.6)                       # the neck
    m.prism_x("Body", circle(0.98, 10, 0.0, -0.05), 0.6, 1.35)                       # the head
    m.prism_x("Body", circle(0.98, 10, 0.0, -0.05), 1.35, 1.5, cap1="Lens")          # the bezel and the lens
    m.prism_x("Body", chamfer_rect(0.3, -0.6, 0.2, 0.08), -1.6, -1.5)                # the tail switch
    return m


def suppressor_mesh():
    m = Mesh()
    ring = [(x, y) for y, x in circle(1.0, 12)]  # (x, y): the swapped circle runs clockwise seen from +Z,
    ring.reverse()                               # reversed - counter-clockwise
    inner = list(reversed(ring))                 # clockwise: the bore's walls face in
    m.prism_z("Body", ring, -5.5, 5.4, 1.3, 1.3, cap0=False, cap1=False)             # the tube
    m.prism_z("Body", ring, 5.4, 6.0, 1.3, 0.95, cap0=False)                          # the thread end
    m.prism_z("Body", ring, -6.0, -5.5, 1.1, 1.3, cap0=False, cap1=False)             # the front chamfer
    for z in (-2.0, 1.5):                                                           # two raised grip bands
        m.prism_z("Body", ring, z, z + 0.25, 1.33, 1.33)
    m.prism_z("Body", ring, -6.0, -6.0, 0.42, 1.1, cap0=False, cap1=False)            # the front face round the bore
    m.prism_z("Body", inner, -6.0, -5.6, 0.42, 0.42, cap0=False, cap1=False)          # the bore
    m.poly("Body", [(x * 0.42, y * 0.42, -5.6) for x, y in inner])                    # its floor, facing out of the bore
    return m


AXIS = {"swap": None, "flip": False}  # set by calibrate(): how the importer maps OBJ to UE, and its winding


def planar(pts):
    a, b, c = pts[0], pts[1], pts[2]
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    n = [abs(u[1] * v[2] - u[2] * v[1]), abs(u[2] * v[0] - u[0] * v[2]), abs(u[0] * v[1] - u[1] * v[0])]
    keep = [i for i in range(3) if i != n.index(max(n))]
    return [(p[keep[0]] * 0.1, p[keep[1]] * 0.1) for p in pts]


def write_obj(mesh, path):
    """UE (X fwd, Y right, Z up, cm) -> OBJ. Polygons come counter-clockwise seen from outside, read in a
    right-handed sense; the importer's axes and winding are measured by calibrate() on a known box."""
    slots = sorted({p[0] for p in mesh.polys})
    lines = ["# OSATTACH"]
    vi = 0
    for slot in slots:
        lines.append("usemtl " + slot)  # no groups: the importer would make a mesh of each
        for s, pts, uvs in mesh.polys:
            if s != slot:
                continue
            # The OBJ importer wants UVs: a polygon without its own is projected on its dominant plane, 1 per 10 cm.
            uvs = uvs or planar(pts)
            order = list(reversed(range(len(pts)))) if AXIS["flip"] else list(range(len(pts)))
            for k in order:
                lines.append("v %.5f %.5f %.5f" % (AXIS["swap"](pts[k]) if AXIS["swap"] else pts[k]))
                lines.append("vt %.6f %.6f" % uvs[k])
            idx = range(vi + 1, vi + 1 + len(pts))
            lines.append("f " + " ".join("%d/%d" % (i, i) for i in idx))
            vi += len(pts)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return slots


def import_obj(path, name):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = MESHES
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = unreal.load_asset(MESHES + "/" + name)
    if not mesh:
        raise RuntimeError("did not import " + path + ": " + str(list(task.imported_object_paths)))
    # The importer's by-products (materials, textures) are not wanted: slots are set below.
    for obj in task.imported_object_paths:
        if obj.split(".")[0] != MESHES + "/" + name:
            asset = obj.split(".")[0]
            if eal.does_asset_exist(asset):
                eal.delete_asset(asset)
    return mesh


def bounds(mesh):
    box = mesh.get_bounding_box()
    return box.min, box.max


def calibrate():
    """Import a known box (x 0..3, y 0..2, z 0..1 cm in UE axes, written unconverted) and read where it lands."""
    m = Mesh()
    m.poly("Body", [(0, 0, 0), (0, 2, 0), (3, 2, 0), (3, 0, 0)])
    m.poly("Body", [(0, 0, 1), (3, 0, 1), (3, 2, 1), (0, 2, 1)])
    path = os.path.join(WORK, "SM_OS_AttachCalib.obj")
    write_obj(m, path)
    mesh = import_obj(path, "SM_OS_AttachCalib")
    lo, hi = bounds(mesh)
    ext = (hi.x - lo.x, hi.y - lo.y, hi.z - lo.z)
    log("calibration box: min %s max %s" % (lo, hi))
    eal.delete_asset(MESHES + "/SM_OS_AttachCalib")
    # Which UE axis did each OBJ axis land on, with what sign and scale.
    size = {3.0: 0, 2.0: 1, 1.0: 2}
    scale = None
    land = [None] * 3
    for ue_axis, e in enumerate(ext):
        for s in (1.0, 0.01, 100.0):
            k = round(e / s, 3)
            if k in size and land[size[k]] is None:
                land[size[k]] = (ue_axis, s)
                scale = s
                break
    if None in land:
        raise RuntimeError("calibration: cannot read the axes from %s" % (ext,))
    sign = []
    for obj_axis, (ue_axis, s) in enumerate(land):
        sign.append(1.0 if (lo.x, lo.y, lo.z)[ue_axis] > -0.001 else -1.0)
    log("calibration: obj axis -> ue axis %s, signs %s, scale %s" % ([a for a, _ in land], sign, scale))

    def swap(p):
        q = [0.0, 0.0, 0.0]
        for obj_axis, (ue_axis, s) in enumerate(land):
            q[obj_axis] = p[ue_axis] * sign[obj_axis] / s
        return tuple(q)

    AXIS["swap"] = swap
    # The winding, with the axes now right: the box's top must face up.
    write_obj(m, path)
    mesh = import_obj(path, "SM_OS_AttachCalib")
    verts, tris, normals, uvs, tangents = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, 0)
    top = max(range(len(verts)), key=lambda i: verts[i].z)
    AXIS["flip"] = normals[top].z < 0.0
    lo, hi = bounds(mesh)
    log("calibration: rewritten box (%.2f %.2f %.2f)..(%.2f %.2f %.2f), top normal z %.2f -> flip %s"
        % (lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, normals[top].z, AXIS["flip"]))
    eal.delete_asset(MESHES + "/SM_OS_AttachCalib")


def outward(a, b, c, normals):
    """True when a, b, c run counter-clockwise (right-handed) seen from the side their vertex normals face."""
    u = (b.x - a.x, b.y - a.y, b.z - a.z)
    v = (c.x - a.x, c.y - a.y, c.z - a.z)
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    s = normals[0] + normals[1] + normals[2]
    return n[0] * s.x + n[1] * s.y + n[2] * s.z > 0.0


def finish_mesh(mesh, slots, materials, path):
    for slot, mat in materials.items():
        index = mesh.get_material_index(slot)
        if index < 0:
            raise RuntimeError("%s has no slot %s (%s)" % (path, slot, slots))
        mesh.set_material(index, unreal.load_asset(mat))
    unreal.EditorStaticMeshLibrary.remove_collisions(mesh)
    unreal.EditorStaticMeshLibrary.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
    os_build.save(path)
    lo, hi = bounds(mesh)
    log("built %s: %d slots, bounds (%.2f %.2f %.2f)..(%.2f %.2f %.2f)" % (path, mesh.get_num_sections(0), lo.x, lo.y, lo.z, hi.x, hi.y, hi.z))


def build_modules():
    for name, make, mats in (("SM_OS_Attach_Laser", laser_mesh, {"Body": GUN_METAL, "Lens": DOT}),
                             ("SM_OS_Attach_Torch", torch_mesh, {"Body": GUN_METAL, "Lens": CLEAR}),
                             ("SM_OS_Attach_Suppressor", suppressor_mesh, {"Body": GUN_METAL})):
        obj = os.path.join(WORK, name + ".obj")
        slots = write_obj(make(), obj)
        finish_mesh(import_obj(obj, name), slots, mats, MESHES + "/" + name)


def build_long_mag():
    stock = unreal.load_asset(MESHES + "/SM_OS_USP_Mag")
    m = Mesh()
    well = WELL
    top = MAG_TOP
    mats = {}
    reach = []
    sections = stock.get_num_sections(0)
    data = []
    for s in range(sections):
        verts, tris, normals, uvs, tangents = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(stock, 0, s)
        data.append((verts, tris, uvs, normals))
        reach += [sum((v.x - top[0], v.y - top[1], v.z - top[2])[i] * well[i] for i in range(3)) for v in verts]
    cut = (min(reach) + max(reach)) / 2.0
    for s, (verts, tris, uvs, normals) in enumerate(data):
        slot = "Mag%d" % s
        mats[slot] = stock.get_material(s).get_path_name() if stock.get_material(s) else GUN_METAL

        def moved(v):
            d = sum((v.x - top[0], v.y - top[1], v.z - top[2])[i] * well[i] for i in range(3))
            k = MAG_EXTRA_CM if d > cut else 0.0
            return (v.x + well[0] * k, v.y + well[1] * k, v.z + well[2] * k)

        for t in range(0, len(tris), 3):
            # Each triangle in the writer's order (counter-clockwise from outside), whatever the engine's index order.
            a, b, c = tris[t], tris[t + 1], tris[t + 2]
            if not outward(verts[a], verts[b], verts[c], [normals[a], normals[b], normals[c]]):
                b, c = c, b
            m.poly(slot, [moved(verts[a]), moved(verts[b]), moved(verts[c])],
                   [(uvs[i].x, 1.0 - uvs[i].y) for i in (a, b, c)] if len(uvs) == len(verts) else None)
    obj = os.path.join(WORK, "SM_OS_USP_MagLong.obj")
    slots = write_obj(m, obj)
    lo, hi = bounds(stock)
    log("stock magazine: %d sections, reach %.2f..%.2f cm down the well, cut at %.2f; bounds (%.2f %.2f %.2f)..(%.2f %.2f %.2f)"
        % (sections, min(reach), max(reach), cut, lo.x, lo.y, lo.z, hi.x, hi.y, hi.z))
    finish_mesh(import_obj(obj, "SM_OS_USP_MagLong"), slots, mats, MESHES + "/SM_OS_USP_MagLong")


# --- the test room's pickups ----------------------------------------------------------

def place_pickups():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not les.load_level("/Game/OS/Maps/Lvl_OS_TestRoom"):
        raise RuntimeError("could not open Lvl_OS_TestRoom")
    for actor in eas.get_all_level_actors():
        if actor.get_actor_label().startswith("Pickup_ATT_"):
            eas.destroy_actor(actor)
    # Beside the gun pickups (x 1000..1240, y 120), on the same wall, a step further along.
    for i, (ident, mesh) in enumerate((("Laser", "SM_OS_Attach_Laser"), ("Torch", "SM_OS_Attach_Torch"),
                                       ("Suppressor", "SM_OS_Attach_Suppressor"), ("BigMag", "SM_OS_USP_MagLong"))):
        loc = unreal.Vector(1360.0 + 70.0 * i, 120.0, 90.0)
        pickup = eas.spawn_actor_from_class(unreal.OSAttachmentPickup, loc, unreal.Rotator(0, 0, 0))
        pickup.set_actor_label("Pickup_ATT_" + ident)
        pickup.set_editor_property("attachment_id", ident)
        body = pickup.get_editor_property("mesh")
        body.set_static_mesh(unreal.load_asset(MESHES + "/" + mesh))
        pickup.set_actor_scale3d(unreal.Vector(4.0, 4.0, 4.0))
        stand = eas.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/Cube"), unreal.Vector(loc.x, loc.y, 35.0), unreal.Rotator(0, 0, 0))
        stand.set_actor_label("Pickup_ATT_%s_Stand" % ident)
        stand.set_actor_scale3d(unreal.Vector(0.3, 0.3, 0.7))
    if not les.save_current_level():
        raise RuntimeError("could not save Lvl_OS_TestRoom")
    log("placed 4 attachment pickups in Lvl_OS_TestRoom")


def build_preset():
    """A built-in debug preset for a playtest: the pistol and the shotgun open, the laser on the pistol, the rest in the pouch."""
    name, folder = "DA_Test_Attachments", "/Game/OS/Debug/Presets"
    path = folder + "/" + name
    preset = unreal.load_asset(path) if eal.does_asset_exist(path) else         unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.OSTestPreset, unreal.DataAssetFactory())
    preset.set_editor_property("json", """{
  "format": 1,
  "name": "attachments",
  "note": "обвесы: ЛЦУ на пистолете, фонарь, глушитель, магазин 20 в сумке",
  "map": "Lvl_OS_TestRoom",
  "gear": { "guns": ["Pistol", "Shotgun"], "attachments": ["Laser@Pistol", "Torch", "Suppressor", "BigMag"] }
}""")
    os_build.save(path)
    log("built " + path)


def main():
    os.makedirs(WORK, exist_ok=True)
    steps = sys.argv[1:] or ["materials", "sound", "meshes", "pickups", "preset"]
    if "materials" in steps:
        build_projection()
        build_clear_mag()
    if "sound" in steps:
        build_suppressed_shot()
    if "meshes" in steps:
        calibrate()
        build_modules()
        build_long_mag()
    if "pickups" in steps:
        place_pickups()
    if "preset" in steps:
        build_preset()
    log("done")


main()
