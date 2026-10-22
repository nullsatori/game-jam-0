"""The guns' sounds in space: recorded CC0 donors, layered and heard through the suit.

    python Scripts/os_space_shots.py <donor dir> <out dir> [<listen dir>]

Downloads the donors (Freesound HQ previews, all CC0 - CREDITS.md) into <donor dir>
when missing and renders every sound in SOUNDS. The game's sounds (game=True, or game="<asset name>")
go to <out dir> as SW_OS_<name>.wav for Scripts/os_import_audio.py; with <listen dir>
every variant is written there too (<name>_space.wav, the shot donors dry as
Donor_<id>_dry.wav, mocks of a shot with its cycle and of a pistol burst) with
INDEX.txt, the measured table.

No air in space: what reaches the ear comes through the hands, the helmet and
the body - muffled, low, big. A shot is built like a modern explosion, in layers:
  transient  the donor's first 0.1 s, a little lower, low-passed at 1.6-2.5 kHz;
  mid        the whole donor a few semitones down, low-passed ~1 kHz (v1's sound);
  body       the donor an octave+ down, band-passed 70-260 Hz - the weight;
  sub        a sine dropping to 28-38 Hz over 0.15-0.7 s, lightly saturated;
  rumble     (heavy variants) the donor's tail an octave down under 300 Hz;
  tail       a dark reverb (reverb_ir) fed by the layers' sends.
Glue on the bus: parallel saturation of 100-500 Hz, a compressor (4:1, 8 ms
attack lets the first click through, 150 ms release, threshold set for the
wanted gain reduction), lows under 150 Hz in mono, makeup to a loudness target
(LUFS, BS.1770 K-weighting) and a brickwall limiter at -1 dBFS.
Mechanisms (pump, bolt) and dry clicks get the same space, smaller: the click
pitched down and muffled, a thunk of the same click an octave lower under it, a
short sub under the heavy strokes.

The reverb (rebuilt 18.10 after the owner heard a "resonator/chorus" in v1): three
bands of independent noise per channel (no shared noise, no repeats, no early
taps), each through static Butterworth filters with its own decay - lows longest.
v1 cross-faded two low-passed copies of ONE noise over the tail: their phases
differ, so the sum had a notch that moved as the mix moved - a phaser. Send:
high-pass 120 Hz (the sub stays dry) and low-pass 2.5 kHz (mechanisms 1 kHz: their metal rings
kept ringing in the tail); return low-pass 900 Hz.
Deterministic: every noise is seeded. Needs numpy and ffmpeg on PATH.
"""
import os
import subprocess
import sys
import urllib.request
import wave

import numpy as np
from numpy.lib.stride_tricks import sliding_window_view

SR = 48000
OUT_SR = 22050  # nothing above ~3 kHz survives the chain

# id: preview url (page: freesound.org/s/<id>/). All CC0 1.0, checked on each sound's page 18.10.2026.
DONORS = {
    "427592": "https://cdn.freesound.org/previews/427/427592_3094998-hq.mp3",  # michorvath, 9mm pistol shot
    "427595": "https://cdn.freesound.org/previews/427/427595_3094998-hq.mp3",  # michorvath, 20 gauge shotgun
    "450853": "https://cdn.freesound.org/previews/450/450853_612689-hq.mp3",  # kyles, Lee Enfield .303 shot
    "231248": "https://cdn.freesound.org/previews/231/231248_1934171-hq.mp3",  # AugustSandberg, S&W 686 (MindKore's shot)
    "431834": "https://cdn.freesound.org/previews/431/431834_6138164-hq.mp3",  # moosegravy, Sauer 404 close shot
    "159710": "https://cdn.freesound.org/previews/159/159710_2886479-hq.mp3",  # AnthonyChan0, Mossberg 500A shot and pump
    "613801": "https://cdn.freesound.org/previews/613/613801_2338259-hq.mp3",  # Krovi, Shotgun Rack Sample
    "370345": "https://cdn.freesound.org/previews/370/370345_2558531-hq.mp3",  # Zott820, Mosin Nagant bolt cycle
    "508747": "https://cdn.freesound.org/previews/508/508747_1934171-hq.mp3",  # AugustSandberg, Mauser K98 reload
    "674568": "https://cdn.freesound.org/previews/674/674568_7157894-hq.mp3",  # serøutōnin--deprivəd, 9mm dry fired
    "508243": "https://cdn.freesound.org/previews/508/508243_8644110-hq.mp3",  # shelbyshark, 1911 .45 hammer cocking
    "854460": "https://cdn.freesound.org/previews/854/854460_7157894-hq.mp3",  # serøutōnin--deprivəd, Glock magazine out and in
    "725397": "https://cdn.freesound.org/previews/725/725397_7157894-hq.mp3",  # serøutōnin--deprivəd, M16 empty reload
    "108793": "https://cdn.freesound.org/previews/108/108793_1553758-hq.mp3",  # CeebFrack, shell load
}
DONOR_DIR = "."


def run_ffmpeg(cmd, data=None):
    return subprocess.run(["ffmpeg", "-v", "error"] + cmd, input=data, capture_output=True, check=True).stdout


_donors = {}


def donor(i):
    if i not in _donors:
        path = os.path.join(DONOR_DIR, os.path.basename(DONORS[i]))
        if not os.path.exists(path):
            urllib.request.urlretrieve(DONORS[i], path)
        x = np.frombuffer(run_ffmpeg(["-i", path, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"]), np.float32)
        _donors[i] = x.astype(np.float64) / np.abs(x).max()
    return _donors[i]


def filt(x, af):
    """float samples through an ffmpeg filter chain (causal biquads, Q 0.707: Butterworth, no peak)."""
    ch = 1 if x.ndim == 1 else x.shape[1]
    y = np.frombuffer(run_ffmpeg(["-f", "f32le", "-ar", str(SR), "-ac", str(ch), "-i", "-", "-af", af, "-f", "f32le", "-"],
                                 np.ascontiguousarray(x, np.float32).tobytes()), np.float32).astype(np.float64)
    return y if ch == 1 else y.reshape(-1, ch)


def lp(f, n=2):
    return ",".join([f"lowpass=f={f}:p=2"] * n)


def hp(f, n=1):
    return ",".join([f"highpass=f={f}:p=2"] * n)


def fade(x, fin, fout):
    x = x.copy()
    a, b = int(fin * SR), int(fout * SR)
    if a:
        x[:a] *= np.linspace(0, 1, a)
    if b:
        x[-b:] *= np.linspace(1, 0, b)
    return x


def resample(x, semitones):
    """Lower and slower by the ratio, as tape: the pitch-down the owner liked in MindKore."""
    r = 2 ** (semitones / 12)
    return np.interp(np.arange(0, len(x) - 1, r), np.arange(len(x)), x) if semitones else x


def drive(x, amount, bias=0.0):
    """tanh saturation; bias adds even harmonics. Peak-normalised in and out."""
    if not amount:
        return x
    x = x / (np.abs(x).max() + 1e-12)
    y = np.tanh(amount * (x + bias)) - np.tanh(amount * bias)
    return y / (np.abs(y).max() + 1e-12)


# ---- layers: each returns (mono samples, start s, send dB or None) ----

def rec(d, pieces, pitch=0, af="", amount=0.0, gain=0.0, at=0.0, send=None, fout=0.03, time_pitch=None):
    """Recorded slice(s) of donor d: [(start s, end s, place s)], each pitched on its own and placed at
    its time scaled as by time_pitch (default: pitch), then driven and filtered. A copy of a sequence
    far lower (the thunk under a click) keeps the clicks' timing this way instead of stretching it."""
    src = donor(d)
    tr = 2 ** (-(pitch if time_pitch is None else time_pitch) / 12)
    segs = [(int(p * tr * SR), resample(fade(src[int(a * SR):int(b * SR)], 0.002, min(fout, (b - a) / 3)), pitch))
            for a, b, p in pieces]
    x = np.zeros(max(i + len(seg) for i, seg in segs))
    for i, seg in segs:
        x[i:i + len(seg)] += seg
    x = drive(x, amount)
    if af:
        x = filt(x, af)
    return x / (np.abs(x).max() + 1e-12) * 10 ** (gain / 20), at, send


def sub(f0, f1, length, gain=0.0, at=0.0, amount=1.5, sweep=0.06):
    """A sine dropping from f0 to f1 (exponential glide), 3 ms rise, gone (-26 dB) at `length`."""
    t = np.arange(int(length * 1.6 * SR)) / SR
    f = f1 + (f0 - f1) * np.exp(-t / sweep)
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * (1 - np.exp(-t / 0.003)) * np.exp(-3 * (t / length) ** 2)
    x = drive(x, amount, bias=0.2)  # 2nd/3rd harmonics: heard on small speakers too
    return fade(x, 0, 0.01) * 10 ** (gain / 20), at, None


# ---- the space ----

REVERB_BANDS = ((None, 250, 1.0, 1.0), (250, 700, 0.7, 0.6), (700, 1600, 0.4, 0.3))  # hp, lp, share of rt60, gain


def reverb_ir(rt60, seed):
    """Stereo IR: per band and channel its own Gaussian noise (no correlation, no periodicity),
    static Butterworth band edges, exponential decay per band, 20 ms pre-delay, 60 ms soft onset."""
    rng = np.random.default_rng(seed)
    n = int((rt60 + 0.15) * SR)
    t = np.arange(n) / SR
    onset = np.where(t < 0.02, 0.0, 1 - np.exp(-(t - 0.02) / 0.06))
    ir = np.zeros((n, 2))
    for lo, hi, share, g in REVERB_BANDS:
        chain = ",".join(([hp(lo)] if lo else []) + [lp(hi, 1)])
        band = filt(rng.standard_normal((n, 2)), chain)
        ir += band * (g * onset * np.exp(-6.9 * t / (rt60 * share)))[:, None]
    return ir / np.sqrt((ir ** 2).sum() / 2)


def convolve(x, ir):
    n = len(x) + len(ir) - 1
    size = 1 << (n - 1).bit_length()
    X = np.fft.rfft(x, size)
    return np.stack([np.fft.irfft(X * np.fft.rfft(ir[:, c], size), size)[:n] for c in range(2)], axis=1)


# ---- the bus ----

def compress(x, gr_db, ratio=4.0, attack=0.008, release=0.15):
    """Feed-forward RMS compressor on 1 ms blocks; threshold set so the loudest block asks for gr_db.
    Returns the signal and the largest gain reduction reached."""
    if gr_db <= 0:
        return x, 0.0
    blk = SR // 1000
    m = x.mean(axis=1)
    nb = len(m) // blk
    env = 10 * np.log10((m[:nb * blk].reshape(nb, blk) ** 2).mean(axis=1) + 1e-20)
    thr = env.max() - gr_db / (1 - 1 / ratio)
    target = np.maximum(env - thr, 0) * (1 - 1 / ratio)
    a_att, a_rel = np.exp(-0.001 / attack), np.exp(-0.001 / release)
    g, out = 0.0, np.empty(nb)
    for i, tg in enumerate(target):
        a = a_att if tg > g else a_rel
        g = a * g + (1 - a) * tg
        out[i] = g
    gain = np.interp(np.arange(len(m)), np.arange(nb) * blk + blk / 2, out)
    return x * 10 ** (-gain / 20)[:, None], float(out.max())


def limit(x, ceiling_db=-1.0, window_ms=10.0):
    """Brickwall, offline (zero latency): a min-filter of the needed gain over +-W, then a box
    average over W - never above what any sample needs, and smooth over W."""
    c = 10 ** (ceiling_db / 20)
    need = np.minimum(1.0, c / (np.abs(x).max(axis=1) + 1e-12))
    w = int(window_ms * SR / 1000)
    mins = sliding_window_view(np.pad(need, w, constant_values=1.0), 2 * w + 1).min(axis=1)
    h = w // 2
    g = sliding_window_view(np.pad(mins, h, mode="edge"), 2 * h + 1).mean(axis=1)
    return np.clip(x * g[:, None], -c, c), float(-20 * np.log10(g.min()))


def mono_lows(x, f=150):
    mid, side = (x[:, 0] + x[:, 1]) / 2, (x[:, 0] - x[:, 1]) / 2
    side = filt(side, hp(f, 2))
    return np.stack([mid + side, mid - side], axis=1)


def k_weighted(x):
    """BS.1770 K-weighting (48 kHz coefficients) applied as its magnitude response."""
    f = np.fft.rfftfreq(len(x), 1 / SR)
    z = np.exp(-2j * np.pi * f / SR)

    def h(b, a):
        return np.polyval(b[::-1], z) / np.polyval(a[::-1], z)
    k = np.abs(h([1.53512485958697, -2.69169618940638, 1.19839281085285], [1, -1.69065929318241, 0.73248077421585]) *
               h([1.0, -2.0, 1.0], [1, -1.99004745483398, 0.99007225036621]))
    return np.stack([np.fft.irfft(np.fft.rfft(x[:, c]) * k, len(x)) for c in range(x.shape[1])], axis=1)


def loudness(x):
    """(integrated LUFS with the -70/-10 gates, max momentary LUFS)."""
    x = np.atleast_2d(x.T).T
    if len(x) < int(0.4 * SR):
        x = np.pad(x, ((0, int(0.4 * SR) - len(x)), (0, 0)))
    y = k_weighted(x) ** 2
    blk, hop = int(0.4 * SR), int(0.1 * SR)
    z = np.array([y[i:i + blk].mean(axis=0).sum() for i in range(0, len(y) - blk + 1, hop)])
    lk = -0.691 + 10 * np.log10(z + 1e-20)
    g = z[lk > -70]
    rel = -0.691 + 10 * np.log10(g.mean()) - 10
    g = z[(lk > -70) & (lk > rel)]
    return -0.691 + 10 * np.log10(g.mean()), lk.max()


def render(spec, seed):
    layers = [layer() for layer in spec["layers"]]
    n = max(int(at * SR) + len(x) for x, at, _ in layers)
    dry, send = np.zeros(n), np.zeros(n)
    for x, at, s in layers:
        i = int(at * SR)
        dry[i:i + len(x)] += x
        if s is not None:
            send[i:i + len(x)] += x * 10 ** (s / 20)
    sat = spec.get("psat")
    if sat:  # parallel saturation of the low mids: density without touching the click
        band = filt(dry, f"{hp(100)},{lp(500)}")
        dry = dry + drive(band, sat[0]) * np.abs(band).max() * 10 ** (sat[1] / 20)
    rt60, wet_db = spec["reverb"]
    wet = filt(convolve(filt(send, f"{hp(120)},{lp(spec.get('send_lp', 2500))}"), reverb_ir(rt60, seed)), lp(900))
    wet *= 10 ** (wet_db / 20)
    out = wet.copy()
    out[:n] += dry[:, None]
    out = mono_lows(out)
    out, gr = compress(out, spec.get("gr", 4.0))
    gain = 0.0
    for _ in range(4):  # makeup to the loudness target through the limiter
        y, lim = limit(out * 10 ** (gain / 20))
        gain += spec["lufs"] - loudness(y)[0]
    y, lim = limit(out * 10 ** (gain / 20))
    env = np.convolve(np.abs(y.mean(axis=1)), np.ones(480) / 480, "same")
    last = np.nonzero(env > env.max() * 10 ** (-60 / 20))[0][-1]
    y = np.stack([fade(y[:last + 1, c], 0, 0.05) for c in range(2)], axis=1)
    return y, dict(gr=gr, lim=lim, wet=wet)


# ---- the sounds ----

def shot(d, onset, length, t_pitch, t_lpf, m_pitch, m_lpf, b_pitch, sub_args, rumble=None, body_from=None, b_len=0.45):
    """The shot template: transient, mid, body, sub (+ rumble). body_from = (donor, onset) to borrow a body."""
    bd, bo = body_from or (d, onset)
    layers = [
        lambda: rec(d, [(onset, onset + 0.1, 0.0)], t_pitch, f"{hp(150)},{lp(t_lpf)}", 1.5, -4, send=-6, fout=0.04),
        lambda: rec(d, [(onset, onset + length, 0.0)], m_pitch,
                    f"{hp(120)},equalizer=f=400:t=q:w=1:g=-4,{lp(m_lpf)}", 2.0, -3, send=0),
        lambda: rec(bd, [(bo, bo + b_len, 0.0)], b_pitch, f"{hp(70)},{lp(260)}", 2.5, -1, send=-10),
        lambda: sub(*sub_args),
    ]
    if rumble:
        r_pitch, r_gain = rumble
        layers.append(lambda: rec(d, [(onset + 0.08, onset + length, 0.0)], r_pitch, lp(300), 2.0, r_gain,
                                  at=0.02, send=-4))
    return layers


def mech(d, pieces, clicks, heavy, pitch=-4, lufs=-20):
    """A mechanism: click (muffled), thunk (an octave under it, driven), a short sub under each heavy
    stroke. clicks: each piece's click, s from its start (donor time); heavy: piece indices."""
    r = 2 ** (-pitch / 12)
    layers = [
        lambda: rec(d, pieces, pitch, f"{hp(90)},{lp(2200)}", 1.5, 0, send=-4),
        lambda: rec(d, pieces, pitch - 12, f"{hp(50)},{lp(400)}", 3.0, -4, send=-10, time_pitch=pitch),
    ]
    for k in heavy:
        at = (pieces[k][2] + clicks[k]) * r
        layers.append(lambda at=at: sub(110, 55, 0.07, -5, at=max(0.0, at - 0.004), amount=2.0, sweep=0.02))
    return dict(layers=layers, reverb=(1.1, -7), gr=3.0, lufs=lufs, psat=(3.0, -10), send_lp=1000)


def click_times(d, pieces, clicks, pitch=-4):
    """Where each piece's click lands in the rendered mechanism, s."""
    r = 2 ** (-pitch / 12)
    return [(p + c) * r for (_, _, p), c in zip(pieces, clicks)]


# Each shot's onset sits 2 ms before its donor's attack (the first sample over -20 dBFS), measured
# 19.10 (jam-sync): the earlier onsets kept 14-34 ms of room before it, so the crack came that late.
S686 = ("231248", 2.017)
# Clicks placed on the animation (jam-weapons-anim, 18.10), from the shot: pump back 0.26, forward 0.40
# with CycleDelaySeconds 0.15 (owner: a snappy rack, no pause mid-stroke); bolt up 0.35, back 0.63,
# forward 0.87, down 1.09 with 0.25. Pieces: (donor start, end, place before the pitch); clicks: each
# piece's click from its start, as measured on the render (the loudest 2 ms above 800 Hz).
PUMP_A = ("613801", [(0.097, 0.22, 0.0), (0.36, 0.62, 0.122)], [0.087, 0.076])
PUMP_B = ("159710", [(1.222, 1.36, 0.0), (1.62, 1.80, 0.105)], [0.087, 0.0935])
BOLT_A = ("508747", [(3.06, 3.30, 0.030), (3.40, 3.65, 0.147), (20.06, 20.30, 0.343), (20.43, 20.68, 0.577)],
          [0.049, 0.155, 0.149, 0.090])
BOLT_B = ("370345", [(0.05, 0.28, 0.006), (0.38, 0.60, 0.192), (0.77, 1.00, 0.382), (1.10, 1.38, 0.550)],
          [0.073, 0.110, 0.110, 0.117])
DRY_FIRE = ("674568", [(0.0, 0.25, 0.0)], [0.075])
# Reload and hammer (jam-anim2's hooks, 18.10): each cut starts 20-30 ms before its click, so the
# click lands on the hook's moment (+25-40 ms after the pitch).
HAMMER = ("508243", [(5.47, 5.68, 0.0)], [0.02])  # the 1911's hammer: two ticks 70 ms apart
MAG_OUT_PISTOL = ("854460", [(0.27, 0.60, 0.0)], [0.02])  # the Glock's magazine let go, sliding out
MAG_IN_PISTOL = ("854460", [(1.845, 1.98, 0.0)], [0.015])  # ... and slapped home
MAG_OUT_RIFLE = ("725397", [(0.40, 0.66, 0.0)], [0.06])  # the M16's magazine leaving the well
MAG_IN_RIFLE = ("725397", [(3.10, 3.45, 0.0)], [0.03])  # ... seated
SHELL = ("108793", [(0.20, 0.556, 0.0)], [0.03])  # a shell thumbed into a tube
SOUNDS = {
    # In game since the owner's pick on 18.10 ("все выстрелы берем С"): the C of each gun, under the
    # default asset names; A and B are rendered for listening only. Behind the master limiter
    # (SM_OS_Master) the pistol sits at -16.5 LUFS: its burst may sum over 0 dBFS, the limiter holds it.
    # pistol: A the 9mm; B heavier and longer; C MindKore's revolver in the same build
    "Shot_Pistol_A_9mm": dict(lufs=-19.5, gr=6, psat=(3, -8), reverb=(1.5, -3),
                        layers=shot("427592", 0.123, 1.0, -2, 2500, -3, 1500, -12, (90, 38, 0.15, -1), b_len=0.2)),
    "Shot_Pistol_B_heavy": dict(lufs=-19.5, gr=7, psat=(4, -6), reverb=(2.0, -2),
                                layers=shot("427592", 0.123, 1.0, -2, 2200, -4, 1300, -14, (85, 34, 0.25, 0),
                                            rumble=(-12, -6), b_len=0.25)),
    "Shot_Pistol_C_revolver": dict(game="Shot_Pistol", lufs=-16.5, gr=6, psat=(3, -8), reverb=(1.5, -3),
                                   layers=shot(*S686, 1.0, -2, 2500, -3, 1500, -12, (90, 38, 0.15, -1), b_len=0.2)),
    # shotgun: A the 20 gauge; B an explosion (rumble, longer sub, longer tail); C the 686's body under it
    "Shot_Shotgun_A_20ga": dict(lufs=-13, gr=7, psat=(3, -6), reverb=(2.6, -2),
                         layers=shot("427595", 0.057, 1.37, -3, 2200, -4, 1100, -12, (80, 34, 0.3, 0))),
    "Shot_Shotgun_B_blast": dict(lufs=-12.5, gr=8, psat=(4, -4), reverb=(3.2, -1),
                                 layers=shot("427595", 0.057, 1.37, -3, 2000, -5, 1000, -14, (75, 30, 0.4, 1),
                                             rumble=(-12, -4))),
    "Shot_Shotgun_C_mixed": dict(game="Shot_Shotgun", lufs=-13.5, gr=7, psat=(3, -6), reverb=(2.6, -2),
                                 layers=shot("427595", 0.057, 1.37, -3, 2200, -4, 1100, -10, (80, 34, 0.3, 0),
                                             body_from=S686)),
    # SV-98: A the Lee Enfield; B the Sauer 404; C deeper and longer still
    "Shot_SV98_A_enfield": dict(lufs=-13.5, gr=7, psat=(3, -6), reverb=(3.4, -1),
                      layers=shot("450853", 0.025, 1.39, -3, 2000, -5, 850, -12, (70, 30, 0.5, -1), rumble=(-12, -5))),
    "Shot_SV98_B_sauer": dict(lufs=-13, gr=7, psat=(3, -6), reverb=(3.4, -1),
                              layers=shot("431834", 0.505, 1.5, -3, 2000, -5, 850, -12, (70, 30, 0.5, 1),
                                          rumble=(-12, -5))),
    "Shot_SV98_C_deep": dict(game="Shot_SV98", lufs=-12.5, gr=8, psat=(4, -4), reverb=(4.2, 0),
                             layers=shot("450853", 0.025, 1.39, -4, 1600, -6, 750, -15, (60, 28, 0.7, 0),
                                         rumble=(-14, -3))),
    # pump: back then forward (heavy). A the clean rack sample (default), B v1's Mossberg
    "Pump_Shotgun": dict(game=True, **mech(*PUMP_A, heavy=[1])),
    "Pump_Shotgun_B_mossberg": dict(**mech(*PUMP_B, heavy=[1])),
    # bolt: up, back, forward (heavy), down (heavy). A the Mauser K98 (default), B v1's Mosin
    "Bolt_SV98": dict(game=True, **mech(*BOLT_A, heavy=[2, 3])),
    "Bolt_SV98_B_mosin": dict(**mech(*BOLT_B, heavy=[2, 3])),
    # dry fire on an empty magazine: the 9mm's hammer, lower for the bigger guns
    "Empty_Pistol": dict(game=True, **mech(*DRY_FIRE, heavy=[0], pitch=-3, lufs=-23)),
    "Empty_Shotgun": dict(game=True, **mech(*DRY_FIRE, heavy=[0], pitch=-6, lufs=-22)),
    "Empty_SV98": dict(game=True, **mech(*DRY_FIRE, heavy=[0], pitch=-8, lufs=-22)),
    # the hammer under the shot (masked by it, felt as a tick), the magazines, the shells
    "Hammer_Pistol": dict(game=True, **mech(*HAMMER, heavy=[], lufs=-30)),
    "MagOut_Pistol": dict(game=True, **mech(*MAG_OUT_PISTOL, heavy=[], lufs=-24)),
    "MagIn_Pistol": dict(game=True, **mech(*MAG_IN_PISTOL, heavy=[0], lufs=-21)),
    "MagOut_SV98": dict(game=True, **mech(*MAG_OUT_RIFLE, heavy=[], pitch=-5, lufs=-23)),
    "MagIn_SV98": dict(game=True, **mech(*MAG_IN_RIFLE, heavy=[0], pitch=-5, lufs=-20)),
    "Shell_Shotgun": dict(game=True, **mech(*SHELL, heavy=[0], lufs=-22)),
}
# Sequences in game timing (OSWeaponParts.cpp): (sound, seconds) from the shot or the reload's start.
SEQUENCES = {
    "Pistol_shot_hammer": [("Shot_Pistol_C_revolver", 0.0), ("Hammer_Pistol", 0.085)],
    "Pistol_reload": [("MagOut_Pistol", 0.36), ("MagIn_Pistol", 0.86)],
    "SV98_reload": [("MagOut_SV98", 0.5), ("MagIn_SV98", 1.25)],
    "Shotgun_load3": [("Shell_Shotgun", 0.25 + n * 0.42 + 0.30) for n in range(3)],
}
# in-game delay of each cycle after its shot (OSWeapons.cpp CycleDelaySeconds)
CYCLE_MOCKS = {"Shotgun": ("Shot_Shotgun_C_mixed", "Pump_Shotgun", 0.15), "SV98": ("Shot_SV98_C_deep", "Bolt_SV98", 0.25)}
DRY = {"427592": (0.123, 1.0), "427595": (0.057, 1.37), "450853": (0.025, 1.39), "231248": (2.017, 1.0),
       "431834": (0.505, 1.5)}


# ---- measures ----

def bands(x, cuts=(80, 250, 1000, 3000)):
    S = np.abs(np.fft.rfft(x)) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    edges = (0,) + cuts + (SR,)
    return [100 * S[(f >= a) & (f < b)].sum() / S.sum() for a, b in zip(edges, edges[1:])]


def measure(y):
    m = y.mean(axis=1)
    li, lm = loudness(y)
    env = np.convolve(np.abs(m), np.ones(48) / 48, "same")
    pk = env.argmax()
    below = np.nonzero(env[:pk + 1] < 0.1 * env[pk])[0]
    rms03 = 10 * np.log10((m[:int(0.3 * SR)] ** 2).mean() + 1e-20)
    return dict(len=len(y) / SR, peak=20 * np.log10(np.abs(y).max()), lufs=li, mom=lm, rms03=rms03,
                crest=20 * np.log10(np.abs(m).max()) - rms03, rise=(pk - below[-1]) / SR * 1000 if len(below) else 0.0,
                b_all=bands(m), b_att=bands(m[:int(0.05 * SR)]), b_body=bands(m[int(0.05 * SR):int(0.3 * SR)]),
                b_tail=bands(m[int(0.3 * SR):]))


def write(path, x, rate):
    x = np.atleast_2d(x.T).T
    if rate != SR:
        x = np.stack([np.interp(np.arange(0, len(x) - 1, SR / rate), np.arange(len(x)), x[:, c])
                      for c in range(x.shape[1])], axis=1)
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(pcm.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def main():
    global DONOR_DIR
    DONOR_DIR, out_dir = sys.argv[1], sys.argv[2]
    listen = sys.argv[3] if len(sys.argv) > 3 else None
    for d in [DONOR_DIR, out_dir] + ([listen] if listen else []):
        os.makedirs(d, exist_ok=True)
    rows, rendered = [], {}
    hdr = ("sound", "len s", "peak dBFS", "LUFS-I", "LUFS-M max", "RMS 0.3 s", "crest dB", "rise ms", "comp GR dB",
           "lim dB", "<80/80-250/250-1k/1k-3k/>3k % all", "same 0-50 ms", "same 50-300 ms", "same tail")
    for i, (name, spec) in enumerate(SOUNDS.items()):
        y, info = render(spec, seed=i + 1)
        rendered[name] = y
        m = measure(y)
        rows.append((name, f"{m['len']:.2f}", f"{m['peak']:.1f}", f"{m['lufs']:.1f}", f"{m['mom']:.1f}",
                     f"{m['rms03']:.1f}", f"{m['crest']:.1f}", f"{m['rise']:.1f}", f"{info['gr']:.1f}",
                     f"{info['lim']:.1f}") + tuple("/".join(f"{v:.0f}" for v in m[k])
                                                    for k in ("b_all", "b_att", "b_body", "b_tail")))
        print(" | ".join(rows[-1]))
        if spec.get("game"):
            asset = spec["game"] if isinstance(spec["game"], str) else name
            write(os.path.join(out_dir, f"SW_OS_{asset}.wav"), y, OUT_SR)
        if listen:
            write(os.path.join(listen, f"{name}_space.wav"), y, SR)
    for name, m in (("Pump_Shotgun", PUMP_A), ("Pump_Shotgun_B_mossberg", PUMP_B), ("Bolt_SV98", BOLT_A),
                    ("Bolt_SV98_B_mosin", BOLT_B)):
        line = f"Clicks {name}: " + ", ".join(f"{t:.3f}" for t in click_times(*m)) + " s from the sound's start"
        print(line)
        rows.append((line,))
    if not listen:
        return
    for d, (onset, length) in DRY.items():
        x = fade(donor(d)[int(onset * SR):int((onset + length) * SR)], 0.002, 0.05)
        write(os.path.join(listen, f"Donor_{d}_dry.wav"), x * 10 ** (-1 / 20), SR)
    for gun, (s, c, delay) in CYCLE_MOCKS.items():
        a, b = rendered[s], rendered[c]
        mock = np.zeros((max(len(a), int(delay * SR) + len(b)), 2))
        mock[:len(a)] += a
        mock[int(delay * SR):int(delay * SR) + len(b)] += b
        write(os.path.join(listen, f"Mock_{gun}_shot_then_cycle_space.wav"), mock, SR)
    for seq, parts in SEQUENCES.items():  # through a -1 dBFS limiter, as the master plays them
        mock = np.zeros((max(int(t * SR) + len(rendered[n]) for n, t in parts), 2))
        for n, t in parts:
            mock[int(t * SR):int(t * SR) + len(rendered[n])] += rendered[n]
        write(os.path.join(listen, f"Mock_{seq}_space.wav"), limit(mock)[0], SR)
    for name in ("Shot_Pistol_C_revolver",):  # eight shots at the pistol's FireInterval 0.15 s
        a = rendered[name]
        burst = np.zeros((int(7 * 0.15 * SR) + len(a), 2))
        for k in range(8):
            burst[int(k * 0.15 * SR):int(k * 0.15 * SR) + len(a)] += a
        li, _ = loudness(burst)
        line = (f"Mock burst {name}: peak {20 * np.log10(np.abs(burst).max()):+.1f} dBFS, LUFS-I {li:.1f}, "
                f"<80/80-250/250-1k/1k-3k/>3k {'/'.join(f'{v:.0f}' for v in bands(burst.mean(axis=1)))} %")
        print(line)
        rows.append((line,))
        # as the game plays it: through a -1 dBFS limiter (ours; the game's is SMX_OS_MasterLimiter)
        limited, lim = limit(burst)
        print(f"  through a -1 dBFS limiter: {lim:.1f} dB of limiting, LUFS-I {loudness(limited)[0]:.1f}")
        write(os.path.join(listen, f"Mock_{name}_burst8_limited_space.wav"), limited, SR)
    with open(os.path.join(listen, "INDEX.txt"), "w", encoding="utf-8") as f:
        f.write(" | ".join(hdr) + "\n" + "\n".join(" | ".join(r) for r in rows) + "\n")


if __name__ == "__main__":
    main()
