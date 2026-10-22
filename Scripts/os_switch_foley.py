"""Weapon switch Foley in space: holster and sling handling from recorded CC0 donors, heard through the suit.

    python Scripts/os_switch_foley.py <donor root> <out dir> [<listen dir> [<pistol shot wav>]]

<donor root>/<id>/<preview file> holds each donor (Freesound HQ previews, all CC0 - CREDITS.md);
nothing is downloaded here. The game's variant A of every sound goes to <out dir> as SW_OS_<name>.wav
for Scripts/os_import_audio.py (OS_AUDIO_DEST=/Game/OSArt/Audio/Weapons); with <listen dir> every
variant is written there too, with a mock of a switch round (pistol -> shotgun -> SV-98 -> pistol) in
game timing and INDEX.txt, the measured table. With <pistol shot wav> (the game's SW_OS_Shot_Pistol,
exported) the table gives each sound's level against it and a mock plays the shot before the round.

Same space as the gun mechanisms in os_space_shots.py (its helpers are reused): each recorded piece
pitched down and muffled (low-pass 1.8-2.2 kHz), a copy of the knocks an octave lower under it
(the thunk, driven, low-passed 400 Hz), a short sub under the heavy strokes, the mechanisms' small
dark reverb, compressor and -1 dBFS limiter; levels well under the shots. Weight by gun: the pistol
3 semitones down, a light thunk and no sub, the shotgun 5 down with a short sub, the SV-98 7 down
with a longer, lower, louder sub and a pack strap under its knock.

  pistol stow  a leather slide into the holster, the seat (a snap, then the thump on the hip);
  pistol draw  the hand slapping the grip, the leather pull;
  long stow    webbing rustle, the sling swivel clinking, the stock knocking on the back;
  long draw    the sling whipping off with the swivel, the gun caught in the hands (metal + wood knock).

Timing (OSWeaponParts.cpp SetupWeaponParts): each sound starts at its StowSoundSeconds /
DrawSoundSeconds. The stows lead into their knock, so their events moved earlier: the pistol's seat
lands at 0.30 s (event 0.14 + 0.16), the shotgun's knock at 0.40 (0.20 + 0.20), the SV-98's at 0.45
(0.20 + 0.25) - where the old events were. The draws keep their events (0.02 / 0.05 / 0.06): the
slap or whip at once, the catch on the landing kick (pistol none, shotgun +0.40 = 0.45, SV-98
+0.54 = 0.60). TARGETS lists them; align() shifts each render so its loudest knock lands there
(the table gives the shift).
Deterministic: every noise is seeded. Needs numpy and ffmpeg on PATH.
"""
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os_space_shots as jss  # noqa: E402  (the helpers: rec, sub, render, loudness, limit, write)
from os_space_shots import SR, hp, lp, rec, sub  # noqa: E402

# id: preview file, under <donor root>/<id>/. Page: freesound.org/s/<id>/. All CC0 1.0, checked 19.10.2026.
DONORS = {
    "767308": "767308_15758192-hq.mp3",  # Hano_van_Dalen, leather revolver holster equip
    "767312": "767312_15758192-hq.mp3",  # Hano_van_Dalen, revolver removal from leather holster
    "755986": "755986_7157894-hq.mp3",  # serøutōnin--deprivəd, a pistol holstered/unholstered
    "418330": "418330_6511463-hq.mp3",  # cambrianman, dropping a submachine gun on a sling
    "620935": "620935_13743365-hq.mp3",  # moneybags-anon, SVT40 (canvas backpack)
    "725403": "725403_7157894-hq.mp3",  # serøutōnin--deprivəd, a rifle moved around and handled
    "744123": "744123_59530-hq.mp3",  # ChemiCatz, bag strap buckle
    "19291": "19291_84709-hq.mp3",  # martian, foley cloth rustle
}
DONOR_ROOT = "."
_donors = {}


def donor(i):
    """Each donor from its own folder (jss.donor would download into one); peak-normalised mono."""
    if i not in _donors:
        path = os.path.join(DONOR_ROOT, i, DONORS[i])
        x = np.frombuffer(jss.run_ffmpeg(["-i", path, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"]), np.float32)
        _donors[i] = x.astype(np.float64) / np.abs(x).max()
    return _donors[i]


jss.donor = donor  # rec() reads donors through this name

LEAD = 0.012  # each piece starts this long (donor time) before its attack


def at_for(target, pitch, lead=LEAD):
    """Start of a piece whose attack (lead into it, donor time) must land at target s, pitched as given."""
    return max(0.0, target - lead * 2 ** (-pitch / 12))


def slide(d, a, b, target, pitch, gain, lp_hz=1800):
    """Friction (leather, webbing): muffled, lightly driven, from a to b in the donor, starting at target."""
    return lambda: rec(d, [(a, b, 0.0)], pitch, f"{hp(110)},{lp(lp_hz)}", 1.0, gain, at=target, send=-8, fout=0.05)


def knock(d, attack, length, target, pitch, gain, thunk=-4.0):
    """A knock or click: the piece muffled + the thunk an octave under it, both attacks on target."""
    a, b = attack - LEAD, attack - LEAD + length
    return [
        lambda: rec(d, [(a, b, 0.0)], pitch, f"{hp(90)},{lp(2200)}", 1.5, gain, at=at_for(target, pitch), send=-4),
        lambda: rec(d, [(a, b, 0.0)], pitch - 12, f"{hp(50)},{lp(400)}", 3.0, gain + thunk,
                    at=at_for(target, pitch - 12), send=-10),
    ]


def thump(target, f0, f1, length, gain):
    return lambda: sub(f0, f1, length, gain, at=max(0.0, target - 0.004), amount=2.0, sweep=0.03)


def sound(layers, lufs, main):
    """main: s of the main knock in the render (where the table checks it landed)."""
    return dict(layers=layers, reverb=(1.1, -7), gr=3.0, lufs=lufs, psat=(3.0, -10), send_lp=1000, main=main)


P_PISTOL, P_SHOTGUN, P_SV98 = -3, -5, -7


def pistol_stow(slide_at, seat_at):
    """Leather slide (0-0.2 s) into the seat at 0.16 s: the clasp snap and the hip thump ~75 ms later."""
    return sound([slide("767308", slide_at, slide_at + 0.17, 0.0, P_PISTOL, -7)]
                 + knock("767308", seat_at, 0.24, 0.16, P_PISTOL, 0.0, thunk=-10), lufs=-29, main=0.16)


def pistol_draw(pull_at):
    """The grip slapped (0.005 s), the leather pull right after it (its attack at 0.035 s)."""
    return sound(knock("755986", 0.315, 0.12, 0.005, P_PISTOL, -3, thunk=-8)
                 + knock("767312", pull_at, 0.22, 0.035, P_PISTOL, 0.0, thunk=-10), lufs=-28, main=0.035)


def long_stow(pitch, knock_t, rustle_at, clink, stock, sub_args, strap=False, lufs=-27):
    """Webbing rustle from 0, the swivel clink 30 ms before the knock, the stock on the back at knock_t."""
    cd, ca = clink
    sd, sa = stock
    layers = [slide("19291", rustle_at, rustle_at + 0.2, 0.0, pitch, -6),
              *knock(cd, ca, 0.12, knock_t - 0.03, pitch, -7, thunk=-10),
              *knock(sd, sa, 0.25, knock_t, pitch, 0.0),
              thump(knock_t, *sub_args)]
    if strap:  # the pack strap taking the weight
        layers += [slide("744123", 2.06, 2.30, knock_t + 0.01, pitch, -6)]
    return sound(layers, lufs=lufs, main=knock_t)


def long_draw(pitch, catch_t, whip_at, catch, sub_args, lufs=-26):
    """The sling whipping off (cloth swish + swivel rattle), the strap sliding, the catch at catch_t."""
    cd, ca = catch
    return sound([slide("19291", whip_at, whip_at + 0.22, 0.0, pitch, -3),
                  *knock("418330", 1.970, 0.10, 0.03, pitch, -9, thunk=-12),
                  slide("744123", 0.52, 0.85, 0.08, pitch, -10),
                  *knock(cd, ca, 0.15, catch_t, pitch, -1),
                  *knock("620935", 6.695, 0.25, catch_t + 0.004, pitch, -2),
                  thump(catch_t, *sub_args)], lufs=lufs, main=catch_t)


# Event times in game and where each knock lands after it (the C++ side: SetupWeaponParts).
TARGETS = {"Pistol": dict(stow=(0.14, 0.16), draw=(0.02, 0.035), stow_end=0.32),
           "Shotgun": dict(stow=(0.20, 0.20), draw=(0.05, 0.40), stow_end=0.50),
           "SV98": dict(stow=(0.20, 0.25), draw=(0.06, 0.54), stow_end=0.60)}
SUB_SHOTGUN, SUB_SV98 = (95, 50, 0.08, -7), (75, 36, 0.16, -2)
# Variant A in game; B and C other takes of the same donors, for listening (per-play pitch/volume
# variation in game comes from UOSWeaponsComponent's switch sounds).
SOUNDS = {
    "Stow_Pistol": pistol_stow(7.22, 2.270),
    "Stow_Pistol_B": pistol_stow(8.50, 4.370),
    "Stow_Pistol_C": pistol_stow(13.28, 12.075),
    "Draw_Pistol": pistol_draw(1.020),
    "Draw_Pistol_B": pistol_draw(4.055),
    "Draw_Pistol_C": pistol_draw(5.400),
    "Stow_Shotgun": long_stow(P_SHOTGUN, 0.20, 0.53, ("418330", 1.970), ("620935", 6.695), SUB_SHOTGUN),
    "Stow_Shotgun_B": long_stow(P_SHOTGUN, 0.20, 1.10, ("725403", 4.980), ("418330", 0.025), SUB_SHOTGUN),
    "Stow_Shotgun_C": long_stow(P_SHOTGUN, 0.20, 1.18, ("620935", 4.170), ("620935", 6.695), SUB_SHOTGUN),
    "Draw_Shotgun": long_draw(P_SHOTGUN, 0.40, 1.18, ("725403", 1.790), SUB_SHOTGUN),
    "Draw_Shotgun_B": long_draw(P_SHOTGUN, 0.40, 0.53, ("725403", 23.555), SUB_SHOTGUN),
    "Draw_Shotgun_C": long_draw(P_SHOTGUN, 0.40, 1.10, ("725403", 20.710), SUB_SHOTGUN),
    "Stow_SV98": long_stow(P_SV98, 0.25, 0.53, ("418330", 1.970), ("620935", 6.695), SUB_SV98, strap=True, lufs=-26),
    "Stow_SV98_B": long_stow(P_SV98, 0.25, 1.10, ("725403", 4.980), ("418330", 0.025), SUB_SV98, strap=True, lufs=-26),
    "Stow_SV98_C": long_stow(P_SV98, 0.25, 1.18, ("620935", 4.170), ("620935", 6.695), SUB_SV98, strap=True, lufs=-26),
    "Draw_SV98": long_draw(P_SV98, 0.54, 1.18, ("725403", 1.790), SUB_SV98, lufs=-25),
    "Draw_SV98_B": long_draw(P_SV98, 0.54, 0.53, ("725403", 23.555), SUB_SV98, lufs=-25),
    "Draw_SV98_C": long_draw(P_SV98, 0.54, 1.10, ("725403", 20.710), SUB_SV98, lufs=-25),
}
# A switch round as the game plays it: the stow starts at the key, its sound at the stow event, the
# next gun's draw when the stow path ends (+ one 16 ms frame), its sound at the draw event.
ROUND = [("Pistol", "Shotgun", 0.0), ("Shotgun", "SV98", 1.5), ("SV98", "Pistol", 3.2)]
FRAME = 1 / 60


def round_parts(start=0.0):
    parts = []
    for gun, nxt, t in ROUND:
        t += start
        stow_ev, draw_ev = TARGETS[gun]["stow"][0], TARGETS[nxt]["draw"][0]
        parts += [(f"Stow_{gun}", t + stow_ev), (f"Draw_{nxt}", t + TARGETS[gun]["stow_end"] + FRAME + draw_ev)]
    return parts


def ebur128(path):
    """ffmpeg's own meter (independent of ours): integrated LUFS and true peak dBTP."""
    err = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", path, "-af", "ebur128=peak=true", "-f", "null", "-"],
                         capture_output=True, text=True).stderr
    tail = err[err.rfind("Summary:"):]
    i = float(tail.split("I:")[1].split("LUFS")[0])
    tp = float(tail.split("Peak:")[1].split("dBFS")[0])
    return i, tp


def main_knock(y, near, window=0.06):
    """The loudest 2 ms (above 150 Hz) within +-window of near: where the main knock landed, s."""
    m = jss.filt(y.mean(axis=1), hp(150, 2))
    env = np.convolve(np.abs(m), np.ones(96) / 96, "same")
    a, b = int(max(0.0, near - window) * SR), int((near + window) * SR)
    return (a + int(env[a:b].argmax())) / SR


def align(y, target):
    """Shift the render so its main knock lands on target: a donor's knock is a snap and a thump some
    ms apart, and which of them is the loudest differs by take - measured, not assumed. Trims the head
    (3 ms fade-in) or pads silence; returns the shift, s (+: trimmed)."""
    shift = main_knock(y, target, window=0.1) - target
    n = int(round(abs(shift) * SR))
    if shift > 0:
        y = y[n:].copy()
        y[:144] *= np.linspace(0, 1, 144)[:, None]
    else:
        y = np.concatenate([np.zeros((n, 2)), y])
    return y, shift


def main():
    global DONOR_ROOT
    DONOR_ROOT, out_dir = sys.argv[1], sys.argv[2]
    listen = sys.argv[3] if len(sys.argv) > 3 else None
    shot_path = sys.argv[4] if len(sys.argv) > 4 else None
    for d in [out_dir] + ([listen] if listen else []):
        os.makedirs(d, exist_ok=True)
    shot = None
    if shot_path:
        shot = np.frombuffer(jss.run_ffmpeg(["-i", shot_path, "-ac", "2", "-ar", str(SR), "-f", "f32le", "-"]),
                             np.float32).astype(np.float64).reshape(-1, 2)
    shot_lufs = jss.loudness(shot)[0] if shot is not None else None
    rows, rendered = [], {}
    hdr = ["sound", "len s", "LUFS-I", "LUFS-M max", "sample peak dBFS", "ffmpeg LUFS-I", "true peak dBTP",
           "vs pistol shot LU", "main knock s (target)", "aligned by s", "lim dB"]
    for i, (name, spec) in enumerate(SOUNDS.items()):
        y, info = jss.render(spec, seed=100 + i)
        y, shift = align(y, spec["main"])
        rendered[name] = y
        game = "_" not in name.split("_", 1)[1]
        path = os.path.join(out_dir if game else listen or out_dir, f"SW_OS_{name}.wav" if game else f"{name}.wav")
        if game or listen:
            jss.write(path, y, jss.OUT_SR if game else SR)
        if listen and game:
            jss.write(os.path.join(listen, f"{name}.wav"), y, SR)
        li, lm = jss.loudness(y)
        fi, tp = ebur128(path) if (game or listen) else (float("nan"), float("nan"))
        hit = main_knock(y, spec["main"])
        rows.append([name, f"{len(y) / SR:.2f}", f"{li:.1f}", f"{lm:.1f}", f"{20 * np.log10(np.abs(y).max()):.1f}",
                     f"{fi:.1f}", f"{tp:.1f}", f"{li - shot_lufs:+.1f}" if shot_lufs is not None else "-",
                     f"{hit:.3f} ({spec['main']:.3f})", f"{shift:+.3f}", f"{info['lim']:.1f}"])
        print(" | ".join(rows[-1]))
    if not listen:
        return
    lines = []

    def mock(name, parts):
        n = max(int(t * SR) + len(y) for y, t in parts)
        x = np.zeros((n, 2))
        for y, t in parts:
            x[int(t * SR):int(t * SR) + len(y)] += y
        x, lim = jss.limit(x)  # as the master plays it (SMX_OS_MasterBrickwall, -1 dBFS)
        p = os.path.join(listen, name)
        jss.write(p, x, SR)
        li, tp = ebur128(p)
        lines.append(f"{name}: {', '.join(f'{t:.2f}' for _, t in parts)} s; ffmpeg LUFS-I {li:.1f}, true peak {tp:.1f} dBTP, "
                     f"limiter {lim:.1f} dB")
        print(lines[-1])

    parts = round_parts()
    mock("Mock_switch_round.wav", [(rendered[n], t) for n, t in parts])
    if shot is not None:  # the level against the pistol shot: a shot, then the same round 0.6 s later
        mock("Mock_shot_then_switch_round.wav", [(shot, 0.0)] + [(rendered[n], t) for n, t in round_parts(0.6)])
    with open(os.path.join(listen, "INDEX.txt"), "w", encoding="utf-8") as f:
        f.write(" | ".join(hdr) + "\n" + "\n".join(" | ".join(r) for r in rows) + "\n"
                + (f"pistol shot (game asset): LUFS-I {shot_lufs:.1f}\n" if shot_lufs is not None else "")
                + "\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
