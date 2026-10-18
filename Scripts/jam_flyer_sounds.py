"""The flyer's voice and wings, heard through the suit (offline, numpy + ffmpeg).

    python Scripts/jam_flyer_sounds.py <donor dir> <out dir> [<listen dir>]

<donor dir> holds the recorded donors (CC0, already in CREDITS.md; MindKore keeps
the Freesound originals in its SFX/ folder):
  554907__nox_sound__male_breath_fast_loop_stereo.wav  Nox_Sound, Male_Breath_Fast_Loop (Freesound 554907)
  866651__tile_sfx__gushy-impacts-blood.m4a            TILE_SFX, Gushy Impacts (Blood) (Freesound 866651)
Writes SW_Jam_Flyer_{Screech_A,B,C, Death, Flap_A,B,C}.wav into <out dir> for
  JAM_WAV_DIR=<out dir> JAM_AUDIO_DEST=/Game/JamArt/Audio/Flyer UnrealEditor-Cmd ... Scripts/jam_import_audio.py
and, with <listen dir>, the same at 48 kHz plus INDEX.txt (the measured table).

Built with the guns' chain (Scripts/jam_space_shots.py: the layers, the dark
three-band reverb, the glue, the loudness target), no synthesis of the voice:
  screech  a fast breath taken in, pitched up and gliding down (a falling
           "kreee"), its formants pushed (1.2 and 2.6 kHz), driven to a rasp and
           fluttered at ~32 Hz like a bird's call; under it the same breath two
           octaves down (the throat) and a short sub; muffled at 3 kHz;
  death    the same, longer, gliding far down, a wet gurgle (the blood impact an
           octave down) at its end;
  flap     a breath out an octave+ down and low-passed (the air the wing
           pushes) and a wet slap two octaves down under 1.5 kHz (the leather);
           short, little reverb, quiet - one per downstroke in game.
Stand-ins until the owner approves real bird/wing donors (Docs/Flyers.md).
"""
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jam_space_shots as jss  # noqa: E402

SR = jss.SR
BREATH = "554907__nox_sound__male_breath_fast_loop_stereo.wav"
GUSHY = "866651__tile_sfx__gushy-impacts-blood.m4a"


def load(donor_dir, name):
    x = np.frombuffer(subprocess.run(["ffmpeg", "-v", "error", "-i", os.path.join(donor_dir, name), "-ac", "1", "-ar", str(SR),
                                      "-f", "f32le", "-"], capture_output=True, check=True).stdout, np.float32)
    return x.astype(np.float64) / np.abs(x).max()


def glide(d, a, b, st0, st1, af, amount=0.0, gain=0.0, send=None, flutter=0.0, at=0.0):
    """Donor slice a..b s, played as tape whose pitch glides from st0 to st1 semitones, fluttered
    (amplitude, flutter Hz), driven, filtered; peak-normalised to gain dB."""
    def layer():
        src = jss.fade(jss.donor(d)[int(a * SR):int(b * SR)], 0.004, 0.04)
        n_out = int(len(src) / 2 ** ((st0 + st1) / 24))
        rate = 2 ** (np.linspace(st0, st1, n_out) / 12)
        pos = np.cumsum(rate)
        pos = pos[pos < len(src) - 1]
        x = np.interp(pos, np.arange(len(src)), src)
        if flutter:
            t = np.arange(len(x)) / SR
            x = x * (0.55 + 0.45 * np.sin(2 * np.pi * flutter * t))
        x = jss.drive(x, amount)
        if af:
            x = jss.filt(x, af)
        x = jss.fade(x, 0.003, 0.05)
        return x / (np.abs(x).max() + 1e-12) * 10 ** (gain / 20), at, send
    return layer


FORMANTS = "equalizer=f=1200:t=q:w=2:g=8,equalizer=f=2600:t=q:w=3:g=6"


def screech(a, b, st0, st1, flutter, lufs=-16.0, gurgle=None, reverb=(1.6, -4)):
    layers = [
        glide("breath", a, b, st0, st1, f"{jss.hp(500)},{FORMANTS},{jss.lp(3000)}", 2.5, 0, send=-3, flutter=flutter),
        glide("breath", a, b, st0 - 24, st1 - 24, f"{jss.hp(60)},{jss.lp(500)}", 3.0, -16, send=-14),
        lambda: jss.sub(90, 45, 0.2, -18, amount=1.5, sweep=0.08),
    ]
    if gurgle:
        g_a, g_b, at = gurgle
        layers.append(glide("gushy", g_a, g_b, -12, -14, f"{jss.hp(80)},{jss.lp(900)}", 2.0, -4, send=-6, at=at))
    return dict(layers=layers, reverb=reverb, gr=5.0, lufs=lufs, psat=(3.0, -8), send_lp=2500)


def flap(b_a, b_b, g_a, g_b, pitch=-10, lufs=-24.0):
    return dict(layers=[
        glide("breath", b_a, b_b, pitch, pitch - 2, f"{jss.hp(50)},{jss.lp(900)}", 1.5, 0, send=-10),
        glide("gushy", g_a, g_b, -22, -24, f"{jss.hp(60)},{jss.lp(1500)}", 2.0, -6, send=-12, at=0.03),
        lambda: jss.sub(70, 40, 0.08, -8, amount=1.5, sweep=0.03),
    ], reverb=(0.6, -12), gr=3.0, lufs=lufs, psat=(2.0, -12), send_lp=1000)


# The breath's events (s): taken in at 0.55-1.0 (the loudest), 1.55-2.0, 2.55-2.95; out at 0.05-0.5, 1.05-1.5.
# The blood impacts: the big splat at 2.50-2.75, smaller ones at 1.75-1.95 and 4.15-4.35.
SOUNDS = {
    "Screech_A": screech(0.58, 1.02, 7, 2, 32),
    "Screech_B": screech(1.57, 2.0, 9, 3, 28),
    "Screech_C": screech(2.57, 2.95, 5, 0, 36),
    "Death": screech(0.58, 1.02, 4, -8, 22, lufs=-15.0, gurgle=(2.50, 2.80, 0.5), reverb=(2.2, -3)),
    "Flap_A": flap(0.10, 0.34, 2.50, 2.66),
    "Flap_B": flap(1.10, 1.32, 1.75, 1.92, pitch=-11),
    "Flap_C": flap(2.10, 2.32, 4.15, 4.32, pitch=-9),
}


def main():
    donor_dir, out_dir = sys.argv[1], sys.argv[2]
    listen = sys.argv[3] if len(sys.argv) > 3 else None
    for d in [out_dir] + ([listen] if listen else []):
        os.makedirs(d, exist_ok=True)
    # Local donors in the guns' donor cache, so their layers read them like any other.
    jss._donors["breath"] = load(donor_dir, BREATH)
    jss._donors["gushy"] = load(donor_dir, GUSHY)
    rows = []
    for i, (name, spec) in enumerate(SOUNDS.items()):
        y, info = jss.render(spec, seed=100 + i)
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
