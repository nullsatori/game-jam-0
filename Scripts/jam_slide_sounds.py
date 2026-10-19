"""The USP's slide stop: its own recorded sounds (lever click, slide home), heard through the suit.

    python Scripts/jam_slide_sounds.py <donor root> <out dir> [<listen dir> [<reference dir>]]

<donor root>/<id>/<preview file> holds each donor (Freesound HQ previews, CC0 - CREDITS.md); nothing is
downloaded here. The game's sounds (variant A) go to <out dir> as SW_Jam_<name>.wav for
Scripts/jam_import_audio.py; with <listen dir> every variant is written there too (<name>_space.wav)
with INDEX.txt. <reference dir> is a listen dir of Scripts/jam_space_shots.py (MagOut_Pistol_space.wav,
MagIn_Pistol_space.wav): the empty reload is mocked with them in game timing, through a -1 dBFS limiter.

Donor: a Glock 17's slide released by its slide stop (Stoniac, 842731, take at 26.3 s): the lever's
click, 44 ms of the slide running, the slam into battery with its ring. Cut in two at the run - the
click with the run's first 20 ms, and the slam - each placed by the game on its own key of
SlideReleaseTravel (0.975 s released, 1.0 s home: the run is 25 ms there). Same treatment as the
other mechanism sounds (jam_space_shots.mech: the click pitched down and muffled, a thunk an octave
under it, a short sub under the slam, the mechanisms' reverb). Variant B: the same cuts 7 semitones down
instead of 4 (bigger, darker). Each sound's click is measured on the render (loudest 2 ms above
800 Hz); the game starts it that long before its key.
Deterministic: every noise is seeded. Needs numpy and ffmpeg on PATH.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jam_space_shots as jss  # noqa: E402  (mech, render, loudness, limit, write)
from jam_space_shots import SR, hp  # noqa: E402

# id: preview file, under <donor root>/<id>/. Page: freesound.org/s/<id>/. CC0 1.0, checked 20.10.2026.
DONORS = {"842731": "842731_15508415-hq.mp3"}  # Stoniac, Glock17_Slide (racking and releasing a Glock 17's slide)
DONOR_ROOT = "."
_donors = {}


def donor(i):
    if i not in _donors:
        x = np.frombuffer(jss.run_ffmpeg(["-i", os.path.join(DONOR_ROOT, i, DONORS[i]), "-ac", "1", "-ar", str(SR),
                                          "-f", "f32le", "-"]), np.float32)
        _donors[i] = x.astype(np.float64) / np.abs(x).max()
    return _donors[i]


jss.donor = donor  # rec() reads donors through this name

# (donor, [(start, end, place)], [click from the piece's start]), donor time. The lever clicks at 26.3266
# (a second tick at 26.337 as the slide leaves the notch), the slide runs and slams home at 26.371.
LEVER = ("842731", [(26.3245, 26.352, 0.0)], [0.002])
HOME = ("842731", [(26.3685, 26.62, 0.0)], [0.0025])
# The lever under the slam by 8 dB (the donor's 1 ms peaks: -12 / -3 dB); the slam as loud as the magazine seat at its loudest (LUFS-M max -17).
SOUNDS = {
    "SlideRelease_Pistol": dict(game=True, **jss.mech(*LEVER, heavy=[], lufs=-29)),
    "SlideHome_Pistol": dict(game=True, **jss.mech(*HOME, heavy=[0], lufs=-22)),
    "SlideRelease_Pistol_B_deep": jss.mech(*LEVER, heavy=[], pitch=-7, lufs=-29),
    "SlideHome_Pistol_B_deep": jss.mech(*HOME, heavy=[0], pitch=-7, lufs=-22),
}
KEYS = {"SlideRelease": 0.975, "SlideHome": 1.0}  # JamWeaponParts.cpp: SlideReleaseSeconds, the slam's key


def click(y):
    """Where the loudest 2 ms above 800 Hz sits in the first 50 ms, s."""
    m = jss.filt(y.mean(axis=1), hp(800, 2))[:int(0.05 * SR)]
    return int(np.convolve(np.abs(m), np.ones(96) / 96, "same").argmax()) / SR


def main():
    global DONOR_ROOT
    DONOR_ROOT, out_dir = sys.argv[1], sys.argv[2]
    listen = sys.argv[3] if len(sys.argv) > 3 else None
    ref = sys.argv[4] if len(sys.argv) > 4 else None
    for d in [out_dir] + ([listen] if listen else []):
        os.makedirs(d, exist_ok=True)
    rows, rendered = [], {}
    for i, (name, spec) in enumerate(SOUNDS.items()):
        y, info = jss.render(spec, seed=200 + i)
        rendered[name] = y
        li, lm = jss.loudness(y)
        c = click(y)
        start = KEYS[name.split("_")[0]] - c
        rows.append(f"{name}: {len(y) / SR:.2f} s, LUFS-I {li:.1f}, LUFS-M max {lm:.1f}, peak {20 * np.log10(np.abs(y).max()):.1f} dBFS, "
                    f"limiter {info['lim']:.1f} dB, click at {c * 1000:.1f} ms -> start at {start:.4f} s")
        print(rows[-1])
        if spec.get("game"):
            jss.write(os.path.join(out_dir, f"SW_Jam_{name}.wav"), y, jss.OUT_SR)
        if listen:
            jss.write(os.path.join(listen, f"{name}_space.wav"), y, SR)
    if listen and ref:
        def wav(path):
            return np.frombuffer(jss.run_ffmpeg(["-i", path, "-ac", "2", "-ar", str(SR), "-f", "f32le", "-"]),
                                 np.float32).astype(np.float64).reshape(-1, 2)
        base = [(wav(os.path.join(ref, "MagOut_Pistol_space.wav")), 0.36), (wav(os.path.join(ref, "MagIn_Pistol_space.wav")), 0.86)]
        for tag, suffix in (("A", ""), ("B", "_B_deep")):
            parts = list(base)
            for key in KEYS:
                y = rendered[f"{key}_Pistol{suffix}"]
                parts.append((y, KEYS[key] - click(y)))
            mock = np.zeros((max(int(t * SR) + len(y) for y, t in parts), 2))
            for y, t in parts:
                mock[int(t * SR):int(t * SR) + len(y)] += y
            mock, lim = jss.limit(mock)
            jss.write(os.path.join(listen, f"Mock_Pistol_empty_reload_{tag}_space.wav"), mock, SR)
            rows.append(f"Mock_Pistol_empty_reload_{tag}: LUFS-I {jss.loudness(mock)[0]:.1f}, limiter {lim:.1f} dB")
            print(rows[-1])
    if listen:
        with open(os.path.join(listen, "INDEX.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(rows) + "\n")


if __name__ == "__main__":
    main()
