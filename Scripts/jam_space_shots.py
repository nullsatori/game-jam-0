"""The guns' sounds in space: recorded CC0 donors, heard through the suit.

    python Scripts/jam_space_shots.py <donor dir> <out dir> [<listen dir>]

Downloads the donors (Freesound HQ previews, CC0 - CREDITS.md) into <donor dir>
when missing, cuts them, and renders each sound "as through the suit": no air,
so no crack - what reaches the ear comes through the hands, the helmet and the
body. Writes the game's waves to <out dir> (import: Scripts/jam_import_audio.py)
and, with <listen dir>, the owner's listening files: each sound dry (the donor
cut as recorded) and in space, plus mocks (a shot followed by its cycle, a
pistol burst at its fire rate) and a few alternative donors.

The chain (numbers per sound in SOUNDS):
  1. pitch down by resampling (slower and lower: heavier);
  2. tanh drive - harmonics the low-pass then keeps as body;
  3. high-pass 30 Hz, a 400 Hz dip (no box), low-pass 24 dB/oct at `lpf`;
  4. a sub thump under it (a 0.3 s sine gliding down), felt more than heard;
  5. a big soft stereo reverb: synthetic IR, 20 ms pre-delay, 60 ms fade-in
     (no hard early reflections), RT60 `rt60`, dark (low-passed at 1.2 kHz and
     darker as it decays), its input high-passed at 120 Hz so the sub stays dry;
  6. levelled by the RMS of the first 0.3 s, then soft-clipped (tanh) under -1 dBFS;
  7. trimmed 60 dB under the peak, 22.05 kHz 16-bit stereo (nothing above
     ~3 kHz is left after the low-pass: half the bytes of 44.1 kHz).
Deterministic: the reverb's noise is seeded. Needs numpy and ffmpeg on PATH.
"""
import os
import subprocess
import sys
import urllib.request
import wave

import numpy as np

SR = 48000
OUT_SR = 22050

# id: (preview url, page) - all CC0 1.0, checked on each sound's Freesound page 18.10.2026
DONORS = {
    "427592": ("https://cdn.freesound.org/previews/427/427592_3094998-hq.mp3",
               "https://freesound.org/people/michorvath/sounds/427592/"),  # 9mm pistol shot
    "427595": ("https://cdn.freesound.org/previews/427/427595_3094998-hq.mp3",
               "https://freesound.org/people/michorvath/sounds/427595/"),  # 20 gauge shotgun
    "450853": ("https://cdn.freesound.org/previews/450/450853_612689-hq.mp3",
               "https://freesound.org/people/kyles/sounds/450853/"),  # Lee Enfield .303
    "159710": ("https://cdn.freesound.org/previews/159/159710_2886479-hq.mp3",
               "https://freesound.org/people/AnthonyChan0/sounds/159710/"),  # Mossberg 500A shot and pump
    "370345": ("https://cdn.freesound.org/previews/370/370345_2558531-hq.mp3",
               "https://freesound.org/people/Zott820/sounds/370345/"),  # Mosin-Nagant bolt cycle
    "431834": ("https://cdn.freesound.org/previews/431/431834_6138164-hq.mp3",
               "https://freesound.org/people/moosegravy/sounds/431834/"),  # Sauer 404 (alternative)
    "231248": ("https://cdn.freesound.org/previews/231/231248_1934171-hq.mp3",
               "https://freesound.org/people/AugustSandberg/sounds/231248/"),  # MindKore's S&W 686 (alternative)
}

# name: donor, [(start s, end s, place at s)] pieces, pitch semitones, drive, lpf Hz,
#       sub (Hz from, Hz to, decay s, dB) or None, rt60 s, wet dB, level dB (RMS of the first 0.3 s), in game
SOUNDS = {
    "Shot_Pistol": dict(donor="427592", pieces=[(0.105, 1.10, 0.0)], pitch=-3, drive=2.0, lpf=1500,
                        sub=(75, 40, 0.07, -9), rt60=1.6, wet=-4, level=-18, game=True),
    "Shot_Shotgun": dict(donor="427595", pieces=[(0.030, 1.40, 0.0)], pitch=-4, drive=2.5, lpf=1100,
                         sub=(62, 34, 0.14, -5), rt60=2.6, wet=-2, level=-14, game=True),
    "Shot_SV98": dict(donor="450853", pieces=[(0.010, 1.40, 0.0)], pitch=-5, drive=3.0, lpf=850,
                      sub=(55, 30, 0.22, -3), rt60=3.4, wet=-1, level=-12, game=True),
    # the pump: back (cha) and forward (chock) of one Mossberg rack, 0.30 s apart
    "Pump_Shotgun": dict(donor="159710", pieces=[(1.20, 1.42, 0.0), (1.58, 1.80, 0.30)], pitch=-5, drive=2.0,
                         lpf=1800, sub=None, rt60=1.2, wet=-6, level=-22, game=True),
    # the bolt: up, back, forward, down - one Mosin cycle, tightened to 0.52 s
    "Bolt_SV98": dict(donor="370345", pieces=[(0.05, 0.28, 0.0), (0.38, 0.60, 0.17), (0.77, 1.00, 0.36),
                                               (1.10, 1.38, 0.52)], pitch=-5, drive=2.0, lpf=1800, sub=None,
                      rt60=1.2, wet=-6, level=-22, game=True),
    # alternatives for the owner's ear only
    "Alt_Shot_Pistol_SW686": dict(donor="231248", pieces=[(2.000, 3.00, 0.0)], pitch=-3, drive=2.0, lpf=1500,
                                  sub=(75, 40, 0.07, -9), rt60=1.6, wet=-4, level=-17, game=False),
    "Alt_Shot_SV98_Sauer404": dict(donor="431834", pieces=[(0.500, 2.00, 0.0)], pitch=-5, drive=3.0, lpf=850,
                                   sub=(55, 30, 0.22, -3), rt60=3.4, wet=-1, level=-12, game=False),
}
# in-game delay of each cycle after its shot (JamWeapons.cpp CycleDelaySeconds)
CYCLE_MOCKS = {"Shotgun": ("Shot_Shotgun", "Pump_Shotgun", 0.20), "SV98": ("Shot_SV98", "Bolt_SV98", 0.25)}


def ffmpeg_f32(args_in, af=None):
    cmd = ["ffmpeg", "-v", "error"] + args_in + (["-af", af] if af else []) + ["-ac", "1", "-ar", str(SR), "-f", "f32le", "-"]
    return np.frombuffer(subprocess.run(cmd, capture_output=True, check=True).stdout, np.float32).astype(np.float64)


def filt(x, af, channels=1):
    """Run float samples through an ffmpeg filter chain (causal IIR, as a desk would)."""
    cmd = ["ffmpeg", "-v", "error", "-f", "f32le", "-ar", str(SR), "-ac", str(channels), "-i", "-", "-af", af,
           "-f", "f32le", "-"]
    data = np.ascontiguousarray(x, np.float32).tobytes()
    y = np.frombuffer(subprocess.run(cmd, input=data, capture_output=True, check=True).stdout, np.float32)
    return y.astype(np.float64).reshape(-1, channels) if channels > 1 else y.astype(np.float64)


def fade(x, fin, fout):
    x = x.copy()
    a, b = int(fin * SR), int(fout * SR)
    if a:
        x[:a] *= np.linspace(0, 1, a)
    if b:
        x[-b:] *= np.linspace(1, 0, b)
    return x


def donor_cut(path, pieces):
    src = ffmpeg_f32(["-i", path])
    end = max(at + (b - a) for a, b, at in pieces)
    out = np.zeros(int(end * SR) + 1)
    for a, b, at in pieces:
        seg = fade(src[int(a * SR):int(b * SR)], 0.003, min(0.05, (b - a) / 4))
        i = int(at * SR)
        out[i:i + len(seg)] += seg
    return out


def sub_thump(f0, f1, decay, n):
    t = np.arange(n) / SR
    freq = f1 + (f0 - f1) * np.exp(-t / 0.08)
    env = (1 - np.exp(-t / 0.004)) * np.exp(-t / decay)
    return np.sin(2 * np.pi * np.cumsum(freq) / SR) * env


def reverb_ir(rt60, seed):
    rng = np.random.default_rng(seed)
    n = int((rt60 + 0.1) * SR)
    t = np.arange(n) / SR
    pre = 0.02
    shape = np.where(t < pre, 0.0, 1 - np.exp(-(t - pre) / 0.06)) * np.exp(-6.9 * t / rt60)
    noise = rng.standard_normal((n, 2))
    bright = filt(noise, "lowpass=f=1200:p=2,lowpass=f=1200:p=2", 2)
    dark = filt(noise, "lowpass=f=400:p=2,lowpass=f=400:p=2", 2)
    mix = np.clip(t / rt60, 0, 1)[:, None]  # darker as it decays
    ir = (bright * (1 - mix) + dark * 2.0 * mix) * shape[:, None]
    return ir / np.sqrt((ir ** 2).sum() / 2)


def convolve(x, ir):
    n = len(x) + len(ir) - 1
    size = 1 << (n - 1).bit_length()
    X = np.fft.rfft(x, size)
    return np.stack([np.fft.irfft(X * np.fft.rfft(ir[:, c], size), size)[:n] for c in range(2)], axis=1)


def rms_db(x, seconds=0.3):
    seg = x[:int(seconds * SR)]
    return 10 * np.log10((seg ** 2).mean() + 1e-20)


def space(dry, p, seed):
    r = 2 ** (p["pitch"] / 12)
    x = np.interp(np.arange(0, len(dry) - 1, r), np.arange(len(dry)), dry)  # resample: lower and slower
    x = x / (np.abs(x).max() + 1e-12)
    x = np.tanh(p["drive"] * x) / np.tanh(p["drive"])
    x = filt(x, f"highpass=f=30:p=2,equalizer=f=400:t=q:w=1:g=-4,"
                f"lowpass=f={p['lpf']}:p=2,lowpass=f={p['lpf']}:p=2")
    if p["sub"]:
        f0, f1, decay, db = p["sub"]
        x = x + sub_thump(f0, f1, decay, len(x)) * 10 ** (db / 20) * np.abs(x).max()
    ir = reverb_ir(p["rt60"], seed)
    wet = convolve(filt(x, "highpass=f=120:p=2"), ir) * 10 ** (p["wet"] / 20)
    out = wet.copy()
    out[:len(x)] += x[:, None]
    out = out * 10 ** ((p["level"] - rms_db(out.mean(axis=1))) / 20)
    ceiling = 10 ** (-1 / 20)
    out = ceiling * np.tanh(out / ceiling)  # soft clip: the peaks saturate, the body keeps its level
    env = np.convolve(np.abs(out.mean(axis=1)), np.ones(480) / 480, "same")
    last = np.nonzero(env > env.max() * 10 ** (-60 / 20))[0][-1]
    return np.stack([fade(out[:last + 1, c], 0, 0.05) for c in range(2)], axis=1)


def write(path, x, rate):
    x = np.atleast_2d(x.T).T
    if rate != SR:
        x = np.stack([np.interp(np.arange(0, len(x) - 1, SR / rate), np.arange(len(x)), x[:, c])
                      for c in range(x.shape[1])], axis=1)  # content is under 3 kHz here: no alias to fear
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(pcm.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def main():
    donor_dir, out_dir = sys.argv[1], sys.argv[2]
    listen = sys.argv[3] if len(sys.argv) > 3 else None
    for d in [donor_dir, out_dir] + ([listen] if listen else []):
        os.makedirs(d, exist_ok=True)
    rendered = {}
    for i, (name, p) in enumerate(SOUNDS.items()):
        url = DONORS[p["donor"]][0]
        path = os.path.join(donor_dir, os.path.basename(url))
        if not os.path.exists(path):
            urllib.request.urlretrieve(url, path)
        dry = donor_cut(path, p["pieces"])
        wet = space(dry, p, seed=i + 1)
        rendered[name] = wet
        S = np.abs(np.fft.rfft(wet.mean(axis=1))) ** 2
        f = np.fft.rfftfreq(len(wet), 1 / SR)
        print(f"{name}: {len(wet) / SR:.2f} s, peak {20 * np.log10(np.abs(wet).max()):.1f} dBFS, "
              f"RMS 0.3 s {rms_db(wet.mean(axis=1)):.1f} dB, centroid {(S * f).sum() / S.sum():.0f} Hz, "
              f"under 200 Hz {100 * S[f < 200].sum() / S.sum():.0f}%")
        if p["game"]:
            write(os.path.join(out_dir, f"SW_Jam_{name}.wav"), wet, OUT_SR)
        if listen:
            write(os.path.join(listen, f"{name}_dry.wav"), dry / np.abs(dry).max() * 10 ** (-1 / 20), SR)
            write(os.path.join(listen, f"{name}_space.wav"), wet, SR)
    if listen:
        for gun, (shot, cycle, delay) in CYCLE_MOCKS.items():
            a, b = rendered[shot], rendered[cycle]
            i = int(delay * SR)
            mock = np.zeros((max(len(a), i + len(b)), 2))
            mock[:len(a)] += a
            mock[i:i + len(b)] += b
            write(os.path.join(listen, f"Mock_{gun}_shot_then_cycle_space.wav"), mock, SR)
        a = rendered["Shot_Pistol"]  # eight shots at the pistol's FireInterval 0.15 s
        burst = np.zeros((int(7 * 0.15 * SR) + len(a), 2))
        for k in range(8):
            burst[int(k * 0.15 * SR):int(k * 0.15 * SR) + len(a)] += a
        print(f"Mock pistol burst: peak {20 * np.log10(np.abs(burst).max()):.1f} dBFS")
        write(os.path.join(listen, "Mock_Pistol_burst8_space.wav"), burst / max(1.0, np.abs(burst).max() / 0.99), SR)


if __name__ == "__main__":
    main()
