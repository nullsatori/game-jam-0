"""The flyer's voice and wings, heard through the suit (offline, numpy + ffmpeg).

    python Scripts/jam_flyer_sounds.py <out dir> <listen dir or -> <donor dir> [<donor dir> ...]

The donors are found by file name anywhere under the donor dirs (all CC0, CREDITS.md):
  616926_97550-hq.mp3     TRP, Red Tailed Hawk, eq isolated calls (Freesound 616926), HQ preview
  774252_16682330-hq.mp3  loganzsound, hawk_cry (Freesound 774252), HQ preview
  670509_621042-hq.mp3    2create, Huge wing flaps for bird, dragon, dinosaur (Freesound 670509), HQ preview
  389633_6068748-hq.mp3   _stubb, Wing Flap 2 (Freesound 389633), HQ preview
  554907__nox_sound__male_breath_fast_loop_stereo.wav  Nox_Sound (Freesound 554907) - the rasp
  866651__tile_sfx__gushy-impacts-blood.m4a            TILE_SFX (Freesound 866651) - the gurgle
(the four previews: Saved/Donors/<id>/ after the 19.10 download; the last two in
MindKore's SFX/ folder). Writes SW_Jam_Flyer_{Screech_A,B,C, Death, Flap_A,B,C}.wav
into <out dir> for
  JAM_WAV_DIR=<out dir> JAM_AUDIO_DEST=/Game/JamArt/Audio/Flyer UnrealEditor-Cmd ... Scripts/jam_import_audio.py
and, unless <listen dir> is "-", the same at 48 kHz plus INDEX_flyer.txt (the measured table).

Built with the guns' chain (Scripts/jam_space_shots.py: the layers, the dark
three-band reverb, the glue, the loudness target):
  screech  a red-tailed hawk's call 4-7 semitones down, falling a little (a bigger
           bird), muffled at 4 kHz and driven; under it the same call an octave and
           a half down under 900 Hz (the throat), a raspy breath and a short sub;
  death    the close hawk cry far down and gliding further, the rasp, a wet gurgle
           (the blood impact an octave down) at its end;
  flap     one beat of big cloth wings (bed sheets) 3 semitones down, a coat flap
           under it for the snap, a short sub; little reverb, quiet - one per
           downstroke in game.
"""
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jam_space_shots as jss  # noqa: E402

SR = jss.SR
FILES = {
    "hawk": "616926_97550-hq.mp3",
    "cry": "774252_16682330-hq.mp3",
    "wings": "670509_621042-hq.mp3",
    "coat": "389633_6068748-hq.mp3",
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


def glide(d, a, b, st0, st1, af, amount=0.0, gain=0.0, send=None, at=0.0):
    """Donor slice a..b s, played as tape whose pitch glides from st0 to st1 semitones, driven,
    filtered; peak-normalised to gain dB."""
    def layer():
        src = jss.fade(jss.donor(d)[int(a * SR):int(b * SR)], 0.004, 0.04)
        n_out = int(len(src) / 2 ** ((st0 + st1) / 24))
        pos = np.cumsum(2 ** (np.linspace(st0, st1, n_out) / 12))
        pos = pos[pos < len(src) - 1]
        x = jss.drive(np.interp(pos, np.arange(len(src)), src), amount)
        if af:
            x = jss.filt(x, af)
        x = jss.fade(x, 0.003, 0.05)
        return x / (np.abs(x).max() + 1e-12) * 10 ** (gain / 20), at, send
    return layer


def screech(d, a, b, st0, st1, lufs=-16.0, gurgle=None, reverb=(1.6, -4)):
    layers = [
        glide(d, a, b, st0, st1, f"{jss.hp(350)},{jss.lp(4000)}", 1.5, 0, send=-3),
        glide(d, a, b, st0 - 18, st1 - 18, f"{jss.hp(60)},{jss.lp(900)}", 2.5, -9, send=-8),
        glide("breath", 0.58, 1.02, st0 + 6, st1 + 4, f"{jss.hp(800)},{jss.lp(3000)}", 2.5, -14, send=-6),
        lambda: jss.sub(90, 45, 0.2, -16, amount=1.5, sweep=0.08),
    ]
    if gurgle:
        g_a, g_b, at = gurgle
        layers.append(glide("gushy", g_a, g_b, -12, -14, f"{jss.hp(80)},{jss.lp(900)}", 2.0, -4, send=-6, at=at))
    return dict(layers=layers, reverb=reverb, gr=5.0, lufs=lufs, psat=(3.0, -8), send_lp=2500)


def flap(w_a, w_b, c_a, c_b, pitch=-3, lufs=-24.0):
    return dict(layers=[
        glide("wings", w_a, w_b, pitch, pitch - 1, f"{jss.hp(70)},{jss.lp(2500)}", 1.2, 0, send=-10),
        glide("coat", c_a, c_b, pitch - 2, pitch - 3, f"{jss.hp(80)},{jss.lp(3000)}", 1.0, -8, send=-12),
        lambda: jss.sub(70, 40, 0.08, -18, amount=1.5, sweep=0.03),
    ], reverb=(0.6, -12), gr=3.0, lufs=lufs, psat=(2.0, -12), send_lp=1000)


# The hawk's calls (s): 4.55-5.0, 6.65-7.15, 9.2-9.55; the close cry 0.03-0.72.
# The big wings' beats: 2.45-3.15, 3.75-4.2, 5.1-5.5, 6.5-6.85; the coat flap 0.12-0.55.
SOUNDS = {
    "Screech_A": screech("hawk", 4.55, 5.0, -5, -7),
    "Screech_B": screech("hawk", 6.65, 7.15, -7, -9),
    "Screech_C": screech("cry", 0.03, 0.72, -4, -6),
    "Death": screech("cry", 0.03, 0.72, -9, -16, lufs=-15.0, gurgle=(2.50, 2.80, 0.55), reverb=(2.2, -3)),
    "Flap_A": flap(2.45, 3.15, 0.12, 0.55),
    "Flap_B": flap(3.75, 4.2, 0.12, 0.55, pitch=-4),
    "Flap_C": flap(5.1, 5.5, 0.12, 0.55, pitch=-2),
}


def main():
    out_dir, listen, donor_dirs = sys.argv[1], sys.argv[2], sys.argv[3:]
    listen = None if listen == "-" else listen
    for d in [out_dir] + ([listen] if listen else []):
        os.makedirs(d, exist_ok=True)
    # Into the guns' donor cache, so the layers read them like any other donor.
    for key, name in FILES.items():
        jss._donors[key] = load(find(donor_dirs, name))
    rows = []
    for i, (name, spec) in enumerate(SOUNDS.items()):
        y, _ = jss.render(spec, seed=100 + i)
        m = jss.measure(y)
        rows.append(f"{name} | {m['len']:.2f} s | peak {m['peak']:.1f} dBFS | LUFS-I {m['lufs']:.1f} | rise {m['rise']:.1f} ms | "
                    f"<80/80-250/250-1k/1k-3k/>3k % {'/'.join(f'{v:.0f}' for v in m['b_all'])}")
        print(rows[-1])
        jss.write(os.path.join(out_dir, f"SW_Jam_Flyer_{name}.wav"), y, jss.OUT_SR)
        if listen:
            jss.write(os.path.join(listen, f"Flyer_{name}.wav"), y, SR)
    if listen:
        with open(os.path.join(listen, "INDEX_flyer.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(rows) + "\n")


if __name__ == "__main__":
    main()
