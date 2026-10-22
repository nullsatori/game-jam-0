"""Offline preview of the Nixie material: numpy mirror of UE_Nixie.usf (v5).

Not an engine screenshot. It reproduces the shader's layer math, atlas sampling
and event clocks on a flat 2.4 x 1.8 cm face seen by a perspective camera, with
a crude light model for BaseColor (ambient + clearcoat sky) and a filmic tone
curve. Use it to judge layout, depth and timing before rebuilding in UE.

    python preview_nixie.py                      # contact sheet into renders/
    python preview_nixie.py --digit 3 --yaw 25   # one frame

Keep in step with UE_Nixie.usf: same names, same constants.
"""
from pathlib import Path
import argparse
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
TEX = HERE.parents[1] / 'Weapons/RMB93/v02/textures'
DEFAULTS = {
    'DepthCm': .11, 'StackSpacingCm': .032, 'FaceWidthCm': 2.4, 'FaceHeightCm': 1.8,
    'GhostStrength': .06, 'CoreIntensity': 3.2, 'DischargeIntensity': 1.6, 'HaloIntensity': .40,
    'InternalRadiance': 2.6, 'GlassEchoStrength': .07, 'EdgeRadiance': .6, 'MinCorePixels': .85,
    'SegmentFlickerStrength': .06, 'SegmentFlickerRate': 4., 'SegmentFlickerScale': 14., 'FlickerPhase': 0.,
    'FlickerStrength': .006, 'CathodeShadow': .85, 'CathodeBounce': 1.4,
    'MeshCellCm': .085, 'MeshWireCm': .0055, 'MeshOpacity': 1., 'MeshGlow': .5, 'DustScatter': 1.2,
    'IgniteSeconds': .09, 'AfterglowSeconds': .045, 'StrikeOvershoot': .6, 'WarmupSeconds': .30,
}
CORE, DISCHARGE, HALO = np.array((1., .27, .045)), np.array((1., .065, .002)), np.array((1., .025, .001))
ORDER = [1, 6, 2, 7, 5, 0, 4, 9, 8, 3]


def load(name, srgb=False):
    a = np.asarray(Image.open(name), dtype=np.float32) / 255
    if srgb:
        a = np.where(a <= .04045, a / 12.92, ((a + .055) / 1.055) ** 2.4)
    return a


def bilinear(tex, uv):
    """Clamp, bilinear, LOD0 - Texture2DSampleLevel(..., 0)."""
    h, w = tex.shape[:2]
    x = np.clip(uv[..., 0], 0, 1) * w - .5
    y = np.clip(uv[..., 1], 0, 1) * h - .5
    x0 = np.clip(np.floor(x).astype(int), 0, w - 1); y0 = np.clip(np.floor(y).astype(int), 0, h - 1)
    x1 = np.clip(x0 + 1, 0, w - 1); y1 = np.clip(y0 + 1, 0, h - 1)
    fx = np.clip(x - x0, 0, 1)[..., None]; fy = np.clip(y - y0, 0, 1)[..., None]
    return (tex[y0, x0] * (1 - fx) * (1 - fy) + tex[y0, x1] * fx * (1 - fy)
            + tex[y1, x0] * (1 - fx) * fy + tex[y1, x1] * fx * fy)


def sat(x): return np.clip(x, 0, 1)
def smoothstep(a, b, x): t = sat((x - a) / (b - a)); return t * t * (3 - 2 * t)
def step(edge, x): return (x >= edge).astype(np.float32)


def hash1(p):
    p = np.mod(p * .1031, 1.0)
    d = (p * (p[..., [1, 2, 0]] + 33.33)).sum(-1, keepdims=True)
    p = p + d
    return np.mod((p[..., 0] + p[..., 1]) * p[..., 2], 1.0)


def noise(p):
    c = np.floor(p); t = p - c; t = t * t * (3 - 2 * t)
    def h(o): return hash1(c + np.array(o, dtype=np.float32))
    a = (h((0, 0, 0)) * (1 - t[..., 0]) + h((1, 0, 0)) * t[..., 0]) * (1 - t[..., 1]) + (h((0, 1, 0)) * (1 - t[..., 0]) + h((1, 1, 0)) * t[..., 0]) * t[..., 1]
    b = (h((0, 0, 1)) * (1 - t[..., 0]) + h((1, 0, 1)) * t[..., 0]) * (1 - t[..., 1]) + (h((0, 1, 1)) * (1 - t[..., 0]) + h((1, 1, 1)) * t[..., 0]) * t[..., 1]
    return a * (1 - t[..., 2]) + b * t[..., 2]


class Textures:
    def __init__(self):
        self.atlas = load(HERE / 'T_Jam_Nixie_Distance.png')
        if self.atlas.shape[-1] == 3:
            self.atlas = np.concatenate((self.atlas, np.zeros_like(self.atlas[..., :1])), -1)
        self.masks = load(TEX / 'T_Jam_RMB93_Counter_Masks.png')
        self.back = load(TEX / 'T_Jam_RMB93_Counter_BC.png', srgb=True)


def atlas_uv(index, uv):
    cell = np.stack((np.mod(index, 4.), np.floor(index / 4.)), -1)
    return (cell * 256 + 16.5 + sat(uv) * 223) / 1024


def cell(T, index, uv):
    index = np.broadcast_to(np.asarray(index, dtype=np.float32), uv.shape[:-1])
    inside = (step(0, uv[..., 0]) * step(uv[..., 0], 1) * step(0, uv[..., 1]) * step(uv[..., 1], 1) * step(index, 9.5))[..., None]
    e = bilinear(T.atlas, atlas_uv(index, uv))
    return np.array((1, 0, 0, 0), np.float32) * (1 - inside) + e * inside


def coverage(e, aa, min_radius):
    d = (e[..., 0] - .5) * 16; r = e[..., 2] * 2
    c = np.maximum(d + r, 0); dr = np.maximum(r, min_radius); aa = np.maximum(aa, .025)
    return sat((dr + aa - c) / (2 * aa)) - sat((-dr + aa - c) / (2 * aa))


def hex_edge(p):
    s = np.array((1., 1.7320508))
    a = p - s * np.floor(p / s) - s * .5
    q = p - s * .5; b = q - s * np.floor(q / s) - s * .5
    g = np.where(((a * a).sum(-1) < (b * b).sum(-1))[..., None], a, b)
    return .5 - np.maximum((np.abs(g) * s * .5).sum(-1), np.abs(g[..., 0]))


def warmup(t, w):
    if w <= 0: return np.ones_like(t)
    x = t / w
    g = np.select([x < .12, x < .25, x < .40, x < .50], [.70, .04, .95, .22], 1 + .30 * np.exp(-(x - .5) * 7))
    return np.where(x >= 4, 1., g)


def camera(width, height, yaw, pitch, distance, fov):
    """Face in the XY plane at the origin, normal +Z toward the camera. Units: cm."""
    yw, pt = np.radians(yaw), np.radians(pitch)
    cam = distance * np.array((np.sin(yw) * np.cos(pt), np.sin(pt), np.cos(yw) * np.cos(pt)))
    fwd = -cam / np.linalg.norm(cam)
    right = np.cross(fwd, (0, 1, 0)); right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    f = 1 / np.tan(np.radians(fov) / 2)
    ys, xs = np.mgrid[:height, :width].astype(np.float32)
    sx = (xs + .5) / width * 2 - 1; sy = 1 - (ys + .5) / height * 2
    d = fwd + right * sx[..., None] / f + up * sy[..., None] * height / width / f
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    t = -cam[2] / d[..., 2]
    hit = cam + d * t[..., None]
    return hit, cam


def ddx(a): return np.gradient(a, axis=1)
def ddy(a): return np.gradient(a, axis=0)


def shade_v5(T, P, uv, wpos, camvec, digit, prev, now, change_time, power_time, enabled):
    nx_layer = lambda z: uv - np.clip(ray_uv * z[..., None] if np.ndim(z) else ray_uv * z, -.3, .3)
    digit = float(np.clip(np.floor(digit + .5), 0, 10)); prev = float(np.clip(np.floor(prev + .5), 0, 10))
    dpX, dpY = ddx(wpos), ddy(wpos); duX, duY = ddx(uv), ddy(uv)
    det = duX[..., 0] * duY[..., 1] - duX[..., 1] * duY[..., 0]
    safe = np.where(det < 0, -1, 1) * np.maximum(np.abs(det), 1e-12)
    tu = (dpX * duY[..., 1:2] - dpY * duX[..., 1:2]) / safe[..., None]; tu /= np.linalg.norm(tu, axis=-1, keepdims=True)
    tv = (dpY * duX[..., 0:1] - dpX * duY[..., 0:1]) / safe[..., None]; tv /= np.linalg.norm(tv, axis=-1, keepdims=True)
    pn = np.cross(tu, tv); pn /= np.linalg.norm(pn, axis=-1, keepdims=True)
    eye = camvec
    cos_view = np.abs((eye * pn).sum(-1)); facing = np.maximum(cos_view, .3)
    size = np.array((P['FaceWidthCm'], P['FaceHeightCm']))
    ray_uv = np.stack(((eye * tu).sum(-1), (eye * tv).sum(-1)), -1) / facing[..., None] / size
    depth0, spacing = P['DepthCm'], P['StackSpacingCm']
    dx, dy = duX * .25, duY * .25
    pixel = np.maximum(np.maximum(np.linalg.norm(duX * 223, axis=-1), np.linalg.norm(duY * 223, axis=-1)), .05)

    t_change = max(now - change_time, 0.); t_power = max(now - power_time, 0.)
    on = float(np.clip(enabled, 0, 1))
    power = float(warmup(np.array(t_power), P['WarmupSeconds'])) if on > .5 else float(np.exp(-t_power / max(P['AfterglowSeconds'], .001)) * (t_power <= max(P['AfterglowSeconds'], .001) * 8))
    ignite = min(max(t_change / P['IgniteSeconds'], 0), 1) if P['IgniteSeconds'] > 0 else 1.
    strike = 1 + max(P['StrikeOvershoot'], 0) * np.exp(-t_change / .08)
    prev_fade = np.exp(-t_change / max(P['AfterglowSeconds'], .001)) * (abs(prev - digit) >= .5) * (prev <= 9.5)
    rank_lit = sum(r for r, d in enumerate(ORDER) if abs(d - digit) < .5)
    z_lit = depth0 + rank_lit * spacing
    lit_uv = nx_layer(z_lit)
    lit = cell(T, digit, lit_uv)
    core = 0
    for tap in range(4):
        o = (-dx if tap < 2 else dx) + (-dy if tap % 2 == 0 else dy)
        core = core + coverage(cell(T, digit, lit_uv + o), pixel * .25, pixel * np.clip(P['MinCorePixels'], 0, 1.2) * .5)
    core = core * .25
    outside = np.maximum((lit[..., 0] - .5) * 16, 0)
    sheath = np.exp(-outside * 1.75) * (1 - core * .92)
    halo = np.exp(-(outside / 3.25) ** 2) * (1 - core) * (1 - smoothstep(6, 8, outside))
    arc = lit[..., 3]; front = ignite * 1.12
    kindled = sat((front - arc) / .10)
    front_flash = np.exp(-((arc - front) / .05) ** 2) * (1 - ignite) * 1.4
    patches = noise(np.stack((arc * max(P['SegmentFlickerScale'], 1), lit_uv[..., 1] * 3, np.full_like(arc, now * max(P['SegmentFlickerRate'], 0) + P['FlickerPhase'])), -1))
    wire_gain = (1 + np.clip(P['SegmentFlickerStrength'], 0, .15) * (patches * 2 - 1)) * (kindled + front_flash)
    spread = max(spacing, .02) * 1.5
    gas_front = cell(T, digit, nx_layer(max(z_lit - spread, 0)))[..., 1]
    gas_back = cell(T, digit, nx_layer(z_lit + spread))[..., 1]
    volume = (gas_front + lit[..., 1] + gas_back) / 3 * ignite

    mesh_p = nx_layer(depth0 * .45) * size / max(P['MeshCellCm'], .01)
    fw = np.maximum(np.maximum(np.abs(ddx(mesh_p[..., 0])) + np.abs(ddy(mesh_p[..., 0])), np.abs(ddx(mesh_p[..., 1])) + np.abs(ddy(mesh_p[..., 1]))), 1e-4)
    half = max(P['MeshWireCm'], 0) / max(P['MeshCellCm'], .01) * .5
    sharp = sat((half + fw * .5 - hex_edge(mesh_p)) / fw)
    mesh = sat(4 * half) + (sharp - sat(4 * half)) * (1 - smoothstep(.15, .40, fw))
    transmit = 1 - mesh * np.clip(P['MeshOpacity'], 0, 1)
    metal = mesh * max(P['GhostStrength'], 0) * .9
    through = np.ones_like(arc); lit_shadow = np.ones_like(arc); prev_shadow = np.ones_like(arc)
    bounce = np.zeros_like(arc); prev_core = np.zeros_like(arc)
    for k, d in enumerate(ORDER):
        z = depth0 + k * spacing
        is_lit = float(abs(d - digit) < .5); is_prev = float(abs(d - prev) < .5)
        cov = coverage(cell(T, d, nx_layer(z)), pixel * .5, 0)
        lit_shadow = lit_shadow + (through - lit_shadow) * is_lit
        prev_shadow = prev_shadow + (through - prev_shadow) * is_prev
        prev_core = prev_core + cov * is_prev
        near = np.exp(-abs(z - z_lit) / max(spacing * 2.5, .02))
        bounce = bounce + (1 - is_lit) * through * cov * near * volume
        metal = metal + (1 - is_lit) * through * cov * max(P['GhostStrength'], 0) * (1 - mesh)
        through = through * (1 - (1 - is_lit) * cov * np.clip(P['CathodeShadow'], 0, 1))
    metal = metal + (1 - float(on * power >= .5)) * lit_shadow * core * max(P['GhostStrength'], 0) * (1 - mesh)

    back_uv = sat(nx_layer(depth0 + 10.5 * spacing))
    front3 = bilinear(T.back, uv); back = bilinear(T.back, back_uv)
    baked = bilinear(T.masks, back_uv)[..., 1:2]
    enc = np.where(back >= .0031308, 1.055 * np.maximum(back, 0) ** (1 / 2.4) - .055, back * 12.92)
    enc = np.maximum(enc - baked * np.array((.083, .086, .067)), 0)
    back = np.where(enc >= .04045, ((enc + .055) / 1.055) ** 2.4, enc / 12.92)
    plate_field = cell(T, digit, back_uv)[..., 1] * ignite
    glass = bilinear(T.masks, uv); aperture = glass[..., 0:1]
    base = front3 * (1 - aperture) + aperture * back * .72 * (through * (1 - mesh))[..., None] + aperture * metal[..., None] * np.array((.78, .74, .68))

    fres = .04 + .96 * (1 - cos_view) ** 5
    flick = 1 + np.clip(P['FlickerStrength'], 0, .05) * (.65 * np.sin(now * 57.3) + .35 * np.sin(now * 93.7))
    hot = CORE + (np.array((1, .46, .13)) - CORE) * min(max((strike - 1) * 1.2, 0), 1)
    E = lambda a, c: a[..., None] * c
    wire = (E(core * P['CoreIntensity'], hot) + E(sheath * P['DischargeIntensity'], DISCHARGE) + E(halo * P['HaloIntensity'], HALO)) * (wire_gain * strike * lit_shadow)[..., None]
    gas = E(P['InternalRadiance'] * volume * .35 * (1 + (lit_shadow - 1) * .5), DISCHARGE)
    caught = E(P['CathodeBounce'] * bounce, DISCHARGE + .25 * CORE) + E(P['MeshGlow'] * mesh * volume, DISCHARGE)
    plate = E(P['InternalRadiance'] * plate_field * .12 * through, DISCHARGE)
    echo_uv = uv - np.clip(ray_uv * z_lit * .35, -.3, .3) + np.array((.006, -.004))
    echo = coverage(cell(T, digit, echo_uv), pixel * .5, 0) * (1 - core)
    rim = np.exp(-((np.abs(uv[..., 0] - .5) - .365) / .035) ** 2) * volume * .12
    optics = E(P['GlassEchoStrength'] * echo * ignite, DISCHARGE) + E(P['EdgeRadiance'] * rim, DISCHARGE) + E(P['DustScatter'] * glass[..., 2] * volume, DISCHARGE)
    after = E(prev_core * prev_shadow * prev_fade * on * P['CoreIntensity'] * .6, np.array((1, .12, .01)))
    tube = (wire + gas + caught + plate) * (transmit * power)[..., None] + after * transmit[..., None]
    emissive = aperture * (1 - fres)[..., None] * flick * (tube + optics * power)
    return emissive, base, fres


def shade_v4(T, P, uv, wpos, camvec, digit, now):
    """The previous (GPT) material, for side-by-side comparison."""
    dpX, dpY = ddx(wpos), ddy(wpos); duX, duY = ddx(uv), ddy(uv)
    det = duX[..., 0] * duY[..., 1] - duX[..., 1] * duY[..., 0]
    safe = np.where(det < 0, -1, 1) * np.maximum(np.abs(det), 1e-12)
    tu = (dpX * duY[..., 1:2] - dpY * duX[..., 1:2]) / safe[..., None]; tu /= np.linalg.norm(tu, axis=-1, keepdims=True)
    tv = (dpY * duX[..., 0:1] - dpX * duY[..., 0:1]) / safe[..., None]; tv /= np.linalg.norm(tv, axis=-1, keepdims=True)
    pn = np.cross(tu, tv); pn /= np.linalg.norm(pn, axis=-1, keepdims=True)
    facing = np.maximum(np.abs((camvec * pn).sum(-1)), .3)
    ray = np.stack(((camvec * tu).sum(-1), (camvec * tv).sum(-1)), -1) / facing[..., None]
    par = np.clip(ray * .11 / np.array((2.4, 1.8)), -.1, .1)
    act = uv - par; backuv = uv - par * 2.25
    aperture = bilinear(T.masks, uv)[..., 0:1]
    pixel = np.maximum(np.maximum(np.linalg.norm(duX * 223, axis=-1), np.linalg.norm(duY * 223, axis=-1)), .05)
    aa = pixel * .5
    def wire(index, u, minc):
        e = cell(T, index, u); d = (e[..., 0] - .5) * 16; r = e[..., 2] * 2
        c = np.maximum(d + r, 0); dr = np.maximum(r, pixel * min(minc, 1.2) * .5)
        core = sat((dr + aa - c) / (2 * aa)) - sat((-dr + aa - c) / (2 * aa))
        if minc > 0:
            acc = 0
            for tap in range(4):
                o = (-duX * .25 if tap < 2 else duX * .25) + (-duY * .25 if tap % 2 == 0 else duY * .25)
                e2 = cell(T, index, u + o); r2 = e2[..., 2] * 2; c2 = np.maximum((e2[..., 0] - .5) * 16 + r2, 0)
                dr2 = np.maximum(r2, pixel * min(minc, 1.2) * .5); sa = np.maximum(aa * .5, .025)
                acc = acc + sat((dr2 + sa - c2) / (2 * sa)) - sat((-dr2 + sa - c2) / (2 * sa))
            core = acc * .25
        out = np.maximum(d, 0)
        return core, np.exp(-out * 1.75) * (1 - core * .92), np.exp(-(out / 3.25) ** 2) * (1 - core) * (1 - smoothstep(6, 8, out)), e
    core, sheath, halo, e = wire(digit, act, .85)
    ghost = 0
    for g, (dg, m, off, w) in enumerate([(0, 1.35, (.002, -.003), .9), (2, 1.65, (-.003, .002), .72), (7, 1.95, (.003, .004), .55), (8, 2.25, (-.004, .005), .4)]):
        ghost = ghost + wire(dg, uv - par * m + np.array(off), 0)[0] * (abs(digit - dg) >= .5) * w
    front = bilinear(T.back, uv); back = bilinear(T.back, sat(backuv))
    base = front * (1 - aperture) + back * .72 * aperture + aperture * ghost[..., None] * .01 * np.array((.8, .66, .49))
    flick = 1 + .006 * (.65 * np.sin(now * 57.3) + .35 * np.sin(now * 93.7))
    field = cell(T, digit, backuv)[..., 1]
    gu = backuv * np.array((42., 34.)); gd = np.abs(np.mod(gu - .5, 1) - .5)
    gaa = np.maximum(np.abs(ddx(gu)) + np.abs(ddy(gu)), .001); h = gaa * .5
    gl = sat((.015 + h - gd) / (2 * h)) - sat((-.015 + h - gd) / (2 * h))
    gv = 1 - smoothstep(.15, .40, np.maximum(gaa[..., 0], gaa[..., 1]))
    mesh = np.maximum(gl[..., 0], gl[..., 1]) * .7 * gv
    volume = field * (.35 + mesh * .08)
    echo = wire(digit, uv - par * .35 + np.array((.006, -.004)), 0)[0] * (1 - core)
    rim = np.exp(-((np.abs(uv[..., 0] - .5) - .365) / .035) ** 2) * field * .12
    patches = noise(np.stack((act[..., 0] * 18, act[..., 1] * 18, np.full_like(core, now * 5)), -1))
    seg = 1 + .05 * (patches * 2 - 1)
    gas = .97 + .03 * np.sin(act[..., 1] * 13 + now * .7)
    E = lambda a, c: a[..., None] * c
    inter = E(1.6 * volume, np.array((1, .105, .006))) + E(ghost * field * .035, np.array((1, .1, .006))) + E(.09 * echo, np.array((1, .085, .004))) + E(.7 * rim, np.array((1, .095, .006)))
    emis = aperture * flick * (inter + E(core * 2.8 * seg, CORE) + E(sheath * 1.5 * gas * seg, DISCHARGE) + E(halo * .24 * seg, HALO))
    return emis, base


def render(T, version='v5', digit=4, prev=10, yaw=0., pitch=-8., distance=9., fov=24., size=(640, 480), now=10.,
           change_time=-1000., power_time=-1000., enabled=1., params=None, exposure=1.0):
    P = dict(DEFAULTS, **(params or {}))
    hit, cam = camera(size[0], size[1], yaw, pitch, distance, fov)
    uv = np.stack((hit[..., 0] / 2.4 + .5, .5 - hit[..., 1] / 1.8), -1)
    camvec = cam - hit; camvec /= np.linalg.norm(camvec, axis=-1, keepdims=True)
    face = ((uv >= 0) & (uv <= 1)).all(-1)
    if version == 'v5':
        emis, base, _ = shade_v5(T, P, uv, hit, camvec, digit, prev, now, change_time, power_time, enabled)
    else:
        emis, base = shade_v4(T, P, uv, hit, camvec, digit, now)
    sky = .55 + .45 * np.clip(camvec[..., 1:2] * -1 + .3, 0, 1)
    cosv = np.abs(camvec[..., 2:3]); fcc = (.04 + .96 * (1 - cosv) ** 5) * .2
    lit = base * .55 * (1 - fcc) + fcc * sky * .35 + emis
    receiver = np.array((.035, .037, .036)) * (.6 + .4 * (1 - np.abs(uv[..., 1:2] - .5)))
    img = np.where(face[..., None], lit, receiver)
    x = img * exposure
    aces = np.clip((x * (2.51 * x + .03)) / (x * (2.43 * x + .59) + .14), 0, 1)
    srgb = np.where(aces <= .0031308, aces * 12.92, 1.055 * aces ** (1 / 2.4) - .055)
    return Image.fromarray(np.uint8(np.clip(srgb, 0, 1) * 255 + .5))


def sheet(T, out):
    W, H = 480, 360
    rows = [
        ('было (v4): 3 прямо', dict(version='v4', digit=3)), ('стало (v5): 3 прямо', dict(digit=3)),
        ('было: 4 под углом 30°', dict(version='v4', digit=4, yaw=30)), ('стало: 4 под углом 30°', dict(digit=4, yaw=30)),
        ('стало: 8, взгляд сверху-слева', dict(digit=8, yaw=-28, pitch=-22)), ('стало: питание выключено', dict(digit=8, enabled=0., power_time=0.)),
        ('выстрел 4→3: 15 мс', dict(digit=3, prev=4, change_time=10. - .015)), ('выстрел 4→3: 45 мс', dict(digit=3, prev=4, change_time=10. - .045)),
        ('выстрел 4→3: 90 мс', dict(digit=3, prev=4, change_time=10. - .09)), ('розжиг при доставании: 40 мс (провал после удара)', dict(digit=6, power_time=10. - .04)),
        ('розжиг: 100 мс (повторный поджиг)', dict(digit=6, power_time=10. - .10)), ('розжиг: 200 мс (устоялся с перебросом)', dict(digit=6, power_time=10. - .20)),
    ]
    cols = 2
    img = Image.new('RGB', (W * cols, (H + 24) * ((len(rows) + 1) // cols)), (14, 15, 15))
    d = ImageDraw.Draw(img)
    try: font = ImageFont.truetype('DejaVuSans.ttf', 14)
    except OSError: font = ImageFont.load_default()
    for i, (label, kw) in enumerate(rows):
        tile = render(T, size=(W, H), **kw)
        x, y = (i % cols) * W, (i // cols) * (H + 24)
        img.paste(tile, (x, y + 24)); d.text((x + 8, y + 6), label, fill=(200, 196, 180), font=font)
    img.save(out)
    return out


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--digit', type=float); ap.add_argument('--yaw', type=float, default=0.)
    ap.add_argument('--pitch', type=float, default=-8.); ap.add_argument('--version', default='v5')
    ap.add_argument('--out', default=str(HERE / 'renders/nixie_v5_preview_sheet.png'))
    a = ap.parse_args()
    T = Textures()
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    if a.digit is None:
        print(sheet(T, a.out))
    else:
        render(T, a.version, a.digit, yaw=a.yaw, pitch=a.pitch).save(a.out); print(a.out)
