"""The flyer's sounds as layered stems, heard through the suit (offline, numpy + ffmpeg).

    python Scripts/jam_flyer_sounds.py <out dir> <listen dir or -> <donor dir> [<donor dir> ...]

Every event is built in layers, each with one role, and written as mono stems
SW_Jam_Flyer_<Event>_<Role>.wav (32 kHz) into <out dir>; AJamFlyer plays them together
(UJamFlyerAudio: per-play pitch, layer gains, low-pass and start offset, 3D, Doppler):
  Attack     the first 50 ms - a beak clack (stork bill-clapping) or the air snap of a coat;
  Body       the donor that says what it is - the hawk's call, the wing beat, the whoosh;
  Character  what makes it this creature - the throat (the call an octave and a half down),
             the breath's rasp, a wet gurgle;
  Sub        a short low sweetener (a gliding sine under the body, the guns' sub);
  Tail       the "felt through the suit" dark three-band reverb of the guns (Scripts/
             jam_space_shots.py) - played only in vacuum; in air the zone's reverb
             (SM_Jam_AirReverb) replaces it.
Per event the stems are compressed together (one gain curve for all), the tail cut
`tail` s after the body with a cos^2 fade (no click), and the sum normalised to the
event's LUFS with the true peak (4x oversampled) at or under -1 dBTP; the stems keep
the sum's proportions, so default layer gains of 0 dB give exactly that mix.
Events: Aggro (the falling call the owner picked, "Screech A"), Dive (the same call,
rising and tight), Death, Flap, Flutter (a 2 s loop of cloth between beats), FlyBy, Hit
(a shot into it), Bite. Unless <listen dir> is "-": each event's mixdown in vacuum and in
air (48 kHz) plus INDEX_flyer.txt, the measured table.

Donors (all CC0, CREDITS.md), found by file name under the donor dirs:
  616926_97550-hq.mp3     TRP, Red Tailed Hawk, eq isolated calls (Freesound 616926)
  774252_16682330-hq.mp3  loganzsound, hawk_cry (Freesound 774252)
  670509_621042-hq.mp3    2create, Huge wing flaps (bed sheets) (Freesound 670509)
  389633_6068748-hq.mp3   _stubb, Wing Flap 2 (a coat) (Freesound 389633)
  705861_9159316-hq.mp3   Breviceps, Storks bill-clapping (Freesound 705861)
  422513_3302499-hq.mp3   Nightflame, Swinging staff whoosh (strong) 04 (Freesound 422513)
  352719_6513636-hq.mp3   Dalesome, Woosh (stick swung in the air) (Freesound 352719)
  554907__nox_sound__male_breath_fast_loop_stereo.wav  Nox_Sound (Freesound 554907) - the rasp
  866651__tile_sfx__gushy-impacts-blood.m4a            TILE_SFX (Freesound 866651) - wet impacts
(the previews: Saved/Donors/<id>/; the last two in MindKore's SFX/ folder).
"""
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jam_space_shots as jss  # noqa: E402

SR = jss.SR
OUT_SR = 32000  # in air the low-pass opens to 12 kHz
ROLES = ("Attack", "Body", "Character", "Sub", "Tail")
FILES = {
    "hawk": "616926_97550-hq.mp3",
    "cry": "774252_16682330-hq.mp3",
    "wings": "670509_621042-hq.mp3",
    "coat": "389633_6068748-hq.mp3",
    "clack": "705861_9159316-hq.mp3",
    "staff": "422513_3302499-hq.mp3",
    "stick": "352719_6513636-hq.mp3",
    "breath": "554907__nox_sound__male_breath_fast_loop_stereo.wav",
    "gushy": "866651__tile_sfx__gushy-impacts-blood.m4a",
}


def find(dirs, name):
    for d in dirs:
        for root, _, files in os.walk(d):
            if name in files:
                return os.path.join(root, name)
    raise FileNotFoundError(name)


def load(path):
    x = np.frombuffer(subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"],
                                     capture_output=True, check=True).stdout, np.float32)
    return x.astype(np.float64) / np.abs(x).max()


def glide(d, a, b, st0, st1, af, amount=0.0, gain=0.0, send=None, at=0.0, length=None, release=0.08):
    """Donor slice a..b s, played as tape whose pitch glides from st0 to st1 semitones, driven,
    filtered; peak-normalised to gain dB. length: cut to this many seconds, fading over the last `release` s."""
    def layer():
        src = jss.fade(jss.donor(d)[int(a * SR):int(b * SR)], 0.004, 0.04)
        n_out = int(len(src) / 2 ** ((st0 + st1) / 24))
        pos = np.cumsum(2 ** (np.linspace(st0, st1, n_out) / 12))
        pos = pos[pos < len(src) - 1]
        x = jss.drive(np.interp(pos, np.arange(len(src)), src), amount)
        if length:
            x = jss.fade(x[:int(length * SR)], 0.0, release)
        if af:
            x = jss.filt(x, af)
        x = jss.fade(x, 0.002, 0.05)
        return x / (np.abs(x).max() + 1e-12) * 10 ** (gain / 20), at, send
    return layer


def sub(f0, f1, length, gain, at=0.0):
    return lambda: jss.sub(f0, f1, length, gain, at=at, amount=1.5, sweep=min(0.08, length / 2))


HP, LP = jss.hp, jss.lp


def call_len(a, b, st0, st1):
    return (b - a) * 2 ** (-(st0 + st1) / 24)


# ---- the events ------------------------------------------------------------------------------
# The hawk's calls (s): 4.55-5.0 (the owner's "A"), 6.65-7.15; the close cry 0.03-0.72.
# The big wings' beats: 2.45-3.15, 3.75-4.2; the coat flap 0.12-0.55. Stork clacks: 0.32-0.40,
# 0.46-0.54, 0.60-0.68. The blood impacts: the big splat 2.50-2.80, smaller 1.75-1.95, 4.15-4.35.
CLACK = (0.32, 0.40)

EVENTS = {
    # The aggro call is the owner's Screech A (19.10, 1.5 s, the throat ringing 0.3 s past the call),
    # now with a beak clack in front.
    "Aggro": dict(lufs=-16.0, reverb=(1.5, -5), tail=0.7, gr=5.0, send_lp=2500, stems=dict(
        Attack=[glide("clack", *CLACK, -5, -5, f"{HP(300)},{LP(6000)}", 1.5, -8, send=-12)],
        Body=[glide("hawk", 4.55, 5.0, -5, -7, f"{HP(350)},{LP(4000)}", 1.5, 0, send=-3, at=0.01)],
        Character=[glide("hawk", 4.55, 5.0, -23, -25, f"{HP(60)},{LP(900)}", 2.5, -9, send=-8, at=0.01,
                         length=call_len(4.55, 5.0, -5, -7) + 0.3, release=0.38),
                   glide("breath", 0.58, 1.02, 1, -3, f"{HP(800)},{LP(3000)}", 2.5, -14, send=-6, at=0.01)],
        Sub=[sub(90, 45, 0.2, -16, at=0.01)])),
    # The dive: the same call rising (a shriek that tightens as it comes), driven harder, more rasp,
    # the air snap with the clack, a shorter tail.
    "Dive": dict(lufs=-15.0, reverb=(0.9, -6), tail=0.45, gr=6.0, send_lp=2500, stems=dict(
        Attack=[glide("clack", *CLACK, -3, -3, f"{HP(300)},{LP(6000)}", 1.5, -8, send=-12),
                glide("coat", 0.12, 0.30, -3, -3, f"{HP(200)},{LP(5000)}", 1.0, -10, send=-12)],
        Body=[glide("hawk", 4.55, 4.95, -9, -3, f"{HP(350)},{LP(4500)}", 2.2, 0, send=-4, at=0.01)],
        Character=[glide("hawk", 4.55, 4.95, -21, -15, f"{HP(60)},{LP(900)}", 2.5, -10, send=-8, at=0.01,
                         length=call_len(4.55, 4.95, -9, -3) + 0.15, release=0.2),
                   glide("breath", 0.58, 1.02, 0, 4, f"{HP(800)},{LP(3500)}", 3.0, -10, send=-6, at=0.01)],
        Sub=[sub(100, 50, 0.15, -14, at=0.01)])),
    # The death cry as cut on 19.10 (645dc6d), a broken clack in front.
    "Death": dict(lufs=-15.0, reverb=(0.7, -5), tail=0.35, gr=5.0, send_lp=2500, stems=dict(
        Attack=[glide("clack", *CLACK, -9, -10, f"{HP(200)},{LP(4000)}", 1.5, -12, send=-12)],
        Body=[glide("cry", 0.03, 0.72, -9, -16, f"{HP(350)},{LP(4000)}", 1.5, 0, send=-3, at=0.01)],
        Character=[glide("cry", 0.03, 0.72, -27, -34, f"{HP(60)},{LP(900)}", 2.5, -9, send=-8, at=0.01,
                         length=call_len(0.03, 0.72, -9, -16)),
                   glide("breath", 0.58, 1.02, -3, -12, f"{HP(800)},{LP(3000)}", 2.5, -14, send=-6, at=0.01),
                   glide("gushy", 2.50, 2.80, -12, -14, f"{HP(80)},{LP(900)}", 2.0, -4, send=-6, at=0.55)],
        Sub=[sub(90, 45, 0.2, -16, at=0.01)])),
    # The wing beat as cut on 19.10: the bed sheets down 3 semitones, the coat's snap on its front.
    "Flap": dict(lufs=-24.0, reverb=(0.2, -14), tail=0.17, gr=3.0, send_lp=1000, stems=dict(
        Attack=[glide("coat", 0.12, 0.55, -5, -6, f"{HP(80)},{LP(3000)}", 1.0, -8, send=-12)],
        Body=[glide("wings", 2.45, 3.15, -3, -4, f"{HP(70)},{LP(2500)}", 1.2, 0, send=-10)],
        Sub=[sub(70, 40, 0.08, -18)])),
    # A close pass: a heavy staff and a long stick through the air, far down, a sub swell under them.
    "FlyBy": dict(lufs=-20.0, reverb=(0.4, -10), tail=0.25, gr=4.0, send_lp=1500, stems=dict(
        Body=[glide("staff", 0.0, 0.31, -7, -9, f"{HP(60)},{LP(3500)}", 1.3, 0, send=-8)],
        Character=[glide("stick", 0.05, 0.50, -9, -11, f"{HP(80)},{LP(3000)}", 1.2, -4, send=-8, at=0.04)],
        Sub=[sub(55, 35, 0.35, -14, at=0.05)])),
    # A round into it: a wet splat on the front, the membrane's slap (the coat far down) and a chunk of
    # wing beat, a thump.
    "Hit": dict(lufs=-18.0, reverb=(0.3, -12), tail=0.2, gr=5.0, send_lp=1500, stems=dict(
        Attack=[glide("gushy", 2.50, 2.62, -3, -4, f"{HP(150)},{LP(5000)}", 2.0, 0, send=-12)],
        Body=[glide("coat", 0.12, 0.40, -9, -10, f"{HP(60)},{LP(2500)}", 1.5, -4, send=-10),
              glide("wings", 3.75, 3.95, -8, -9, f"{HP(60)},{LP(2000)}", 1.5, -6, send=-10)],
        Sub=[sub(85, 40, 0.12, -8)])),
    # The bite: the beak snapping shut twice, a wet tear, a thump (MindKore's gristle crunch is
    # played with it in game).
    "Bite": dict(lufs=-15.0, reverb=(0.4, -10), tail=0.25, gr=5.0, send_lp=1500, stems=dict(
        Attack=[glide("clack", 0.32, 0.40, -4, -4, f"{HP(250)},{LP(6000)}", 1.8, 0, send=-12),
                glide("clack", 0.46, 0.54, -5, -5, f"{HP(250)},{LP(6000)}", 1.8, -3, send=-12, at=0.06)],
        Body=[glide("gushy", 2.50, 2.75, -6, -7, f"{HP(80)},{LP(3000)}", 2.5, -2, send=-10, at=0.02)],
        Character=[glide("gushy", 4.15, 4.35, -10, -11, f"{HP(80)},{LP(2000)}", 2.0, -8, send=-10, at=0.08)],
        Sub=[sub(90, 40, 0.15, -8, at=0.02)])),
}


# ---- rendering ------------------------------------------------------------------------------------

def bus_gain(x, gr_db, ratio=4.0, attack=0.008, release=0.15):
    """The guns' compressor (jam_space_shots.compress) as a gain curve on a mono sum."""
    if gr_db <= 0:
        return np.ones(len(x))
    y, _ = jss.compress(np.stack([x, x], axis=1), gr_db, ratio, attack, release)
    with np.errstate(divide="ignore", invalid="ignore"):
        g = np.where(np.abs(x) > 1e-6, y[:, 0] / x, np.nan)
    # Between zero crossings the ratio is noise: hold the last good value.
    idx = np.where(~np.isnan(g), np.arange(len(g)), 0)
    np.maximum.accumulate(idx, out=idx)
    g = g[idx]
    return np.nan_to_num(g, nan=1.0)


def true_peak_db(x):
    n = len(x)
    up = np.fft.irfft(np.fft.rfft(x), 4 * n) * 4
    return 20 * np.log10(np.abs(up).max() + 1e-12)


def body_end(x):
    env = np.convolve(np.abs(x), np.ones(480) / 480, "same")
    return np.nonzero(env > env.max() * 0.1)[0][-1] / SR


def render_stems(ev, seed):
    parts = {role: [layer() for layer in layers] for role, layers in ev["stems"].items()}
    n = max(int(at * SR) + len(x) for role in parts.values() for x, at, _ in role)
    rt, wet_db = ev["reverb"]
    n += int((rt + 0.2) * SR)
    stems, send = {}, np.zeros(n)
    for role, layers in parts.items():
        dry = np.zeros(n)
        for x, at, s in layers:
            i = int(at * SR)
            dry[i:i + len(x)] += x
            if s is not None:
                send[i:i + len(x)] += x * 10 ** (s / 20)
        stems[role] = dry
    wet = jss.filt(jss.convolve(jss.filt(send, f"{HP(120)},{LP(ev['send_lp'])}"), jss.reverb_ir(rt, seed)), LP(900))
    stems["Tail"] = wet.mean(axis=1)[:n] * 10 ** (wet_db / 20)
    total = sum(stems.values())
    g = bus_gain(total, ev["gr"])
    stems = {r: x * g for r, x in stems.items()}
    total = sum(stems.values())
    # The tail: cos^2 from the body's end over `tail` s, then cut.
    start = int(body_end(total) * SR)
    end = min(n, start + int(ev["tail"] * SR))
    fade = np.ones(end)
    fade[start:end] = np.cos(np.linspace(0, np.pi / 2, end - start)) ** 2
    stems = {r: x[:end] * fade for r, x in stems.items()}
    # Loudness, with the guns' brickwall where the transients would pass -1 dBTP (one gain curve
    # for every stem, so they still sum to the mix); 0.5 dB under the ceiling for the inter-sample peaks.
    for _ in range(4):
        total = sum(stems.values())
        k = 10 ** ((ev["lufs"] - jss.loudness(total)[0]) / 20)
        stems = {r: x * k for r, x in stems.items()}
        total = sum(stems.values())
        if true_peak_db(total) <= -1.0:
            break
        limited, _ = jss.limit(np.stack([total, total], axis=1), ceiling_db=-1.5)
        with np.errstate(divide="ignore", invalid="ignore"):
            g = np.where(np.abs(total) > 1e-6, limited[:, 0] / total, np.nan)
        idx = np.where(~np.isnan(g), np.arange(len(g)), 0)
        np.maximum.accumulate(idx, out=idx)
        g = np.clip(np.nan_to_num(g[idx], nan=1.0), 0.0, 1.0)
        stems = {r: x * g for r, x in stems.items()}
    total = sum(stems.values())
    excess = true_peak_db(total) + 1.0
    if excess > 0:  # whatever is still over: the loudness gives way
        stems = {r: x * 10 ** (-excess / 20) for r, x in stems.items()}
    return stems, sum(stems.values())


def flutter_loop(seconds=2.0, pitch=-4, lufs=-30.0, seed=7):
    """Cloth flutter between beats: short grains of the bed-sheet recording's rustle, overlapped at
    random, pitched down, band-limited; the ends cross-faded so it loops without a seam."""
    rng = np.random.default_rng(seed)
    src = jss.filt(jss.resample(jss.donor("wings"), pitch), f"{HP(300)},{LP(3500)}")
    grain, hop = int(0.12 * SR), int(0.045 * SR)
    xf = int(0.25 * SR)
    n = int(seconds * SR) + xf
    out = np.zeros(n + grain)
    win = np.hanning(grain)
    for i in range(0, n, hop):
        j = rng.integers(0, len(src) - grain)
        g = src[j:j + grain]
        g = g / (np.sqrt((g ** 2).mean()) + 1e-9)
        out[i:i + grain] += g * win * 10 ** (rng.uniform(-9, 0) / 20)
    out = out[:n]
    # Loop: the extra end fades into the start (equal power).
    body = out[:int(seconds * SR)].copy()
    t = np.linspace(0, np.pi / 2, xf)
    body[:xf] = body[:xf] * np.sin(t) + out[-xf:] * np.cos(t)
    return body * 10 ** ((lufs - jss.loudness(body)[0]) / 20)


def air_mix(stems):
    """How it sounds in air (an approximation of SM_Jam_AirReverb for listening only): no suit tail,
    the low-pass open, a brighter reverb."""
    dry = sum(x for r, x in stems.items() if r != "Tail")
    wet = jss.convolve(jss.filt(dry, HP(150)), jss.reverb_ir(1.2, 99)).mean(axis=1)[:len(dry) + int(0.6 * SR)]
    out = np.zeros(len(wet))
    out[:len(dry)] = dry
    return out + wet * 10 ** (-14 / 20)


def main():
    out_dir, listen, donor_dirs = sys.argv[1], sys.argv[2], sys.argv[3:]
    listen = None if listen == "-" else listen
    for d in [out_dir] + ([listen] if listen else []):
        os.makedirs(d, exist_ok=True)
    for key, name in FILES.items():
        jss._donors[key] = load(find(donor_dirs, name))
    rows = []
    for i, (name, ev) in enumerate(EVENTS.items()):
        stems, total = render_stems(ev, seed=200 + i)
        for role in ROLES:
            if role in stems and np.abs(stems[role]).max() > 1e-5:
                jss.write(os.path.join(out_dir, f"SW_Jam_Flyer_{name}_{role}.wav"), stems[role], OUT_SR)
        li, _ = jss.loudness(total)
        tail = len(total) / SR - body_end(total)
        shares = {r: 10 * np.log10((x ** 2).sum() / (total ** 2).sum() + 1e-12) for r, x in stems.items()}
        rows.append(f"{name} | {len(total) / SR:.2f} s, tail {tail:.2f} s | LUFS-I {li:.1f} | true peak {true_peak_db(total):.1f} dBTP | "
                    + ", ".join(f"{r} {v:+.1f} dB" for r, v in shares.items())
                    + f" | <80/80-250/250-1k/1k-3k/>3k % {'/'.join(f'{v:.0f}' for v in jss.bands(total))}")
        print(rows[-1])
        if listen:
            jss.write(os.path.join(listen, f"Flyer_{name}_vacuum.wav"), jss.filt(total, LP(4500)), SR)
            jss.write(os.path.join(listen, f"Flyer_{name}_air.wav"), air_mix(stems), SR)
    loop = flutter_loop()
    jss.write(os.path.join(out_dir, "SW_Jam_Flyer_Flutter_Loop.wav"), loop, OUT_SR)
    rows.append(f"Flutter_Loop | {len(loop) / SR:.2f} s loop | LUFS-I {jss.loudness(loop)[0]:.1f} | true peak {true_peak_db(loop):.1f} dBTP")
    print(rows[-1])
    if listen:
        jss.write(os.path.join(listen, "Flyer_Flutter_loop_x3.wav"), np.tile(loop, 3), SR)
        with open(os.path.join(listen, "INDEX_flyer.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(rows) + "\n")


if __name__ == "__main__":
    main()
