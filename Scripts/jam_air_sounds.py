"""The guns in air: a crack over the suit's shot, and the cabin's short metal room (Docs/RecoilWeapons-Sound.md, «В воздухе»).

    python Scripts/jam_air_sounds.py <dry donor dir> <out dir> [<listen dir>]

<dry donor dir> holds the shot donors as recorded, Donor_<id>_dry.wav - the files Scripts/jam_space_shots.py
writes into its listen dir (the same Freesound HQ previews, CC0 - CREDITS.md); nothing is downloaded here.
Writes to <out dir>:
  SW_Jam_ShotAir_<gun>.wav  the air layer of each gun's shot (48 kHz, the same in both channels: a mono wave
                            reaches the master 6 dB under a stereo one) for Scripts/jam_import_audio.py;
  IR_Jam_Cabin.wav          the cabin's impulse response (48 kHz stereo) for Scripts/jam_air_audio.py.
With <listen dir> each sound goes there too, with INDEX.txt (the measured table) and mocks of each shot in
vacuum and in air - the suit's shot (re-rendered by jam_space_shots.render from the same dry donors, placed
on their donors' time so the onsets are the game's), the crack, both sent into the cabin, at GAME_DB and through
a -1 dBFS limiter as the master plays them; the table gives what the limiter takes.

In vacuum the shot is only what comes through the suit (jam_space_shots.py), and in the cabin it stays:
the crack is laid over it. It is the donor's own first milliseconds, the highs the suit cuts kept - the
attack held a few ms, then a fast decay, so the donor's room and echo never come (the cabin gives its own);
high-passed (the weight is the suit layer's), the fizz above 10-12 kHz taken off, lightly driven for density.
Sharp and real: no synthesis, no exciter.

The cabin (6 x 5 x 3 m of metal): short and hard, not a hall. Built as the guns' reverb was after the owner
heard a "resonator/chorus" in v1 (jam_space_shots.reverb_ir): per band and channel its own noise (no
shared noise, no repeats, no discrete taps - nothing to comb), static Butterworth bands, each with its own
decay. Metal keeps the mids and the presence ringing (the suit reverb lets everything over 700 Hz die
first); the lows die quickest, the very top is left out. A 2 ms pre-delay and 4 ms rise (small room: the
walls answer at once), the first 30 ms denser (the early reflections, as noise, not taps).
Deterministic: every noise is seeded. Needs numpy and ffmpeg on PATH.
"""
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jam_space_shots as jss  # noqa: E402  (filt, hp, lp, drive, fade, convolve, limit, loudness, write)
from jam_space_shots import SR, hp, lp  # noqa: E402

# gun: (donor id, seconds kept, attack held s, decay time constant s, high-pass Hz, low-pass Hz, drive, peak dBFS)
# The peaks are set so the shot in air (the suit's shot as it is, the crack, the room) costs the master's -1 dBFS
# brickwall at most ~1 dB (the mocks' table; jam.Weapons.AirTake logs what it really took). The SV-98's donor is
# dark (a .303 heard from behind): high-passed higher, held longer and driven harder to give a crack at all.
CRACKS = {
    "Pistol": ("231248", 0.12, 0.003, 0.022, 700, 12000, 1.4, -1.0),
    "Shotgun": ("427595", 0.18, 0.005, 0.035, 1200, 10000, 2.2, -2.0),
    "SV98": ("450853", 0.25, 0.008, 0.06, 1800, 12000, 4.0, -1.0),
}
# The cabin: RT60 s; bands (high-pass, low-pass, share of RT60, gain). The send in game is one level for
# every gun sound (UJamWeaponsComponent::AirReverbSend).
CABIN_RT60 = 0.55
CABIN_BANDS = ((150, 400, 0.55, 0.3), (400, 1500, 1.0, 1.0), (1500, 4000, 0.85, 0.6), (4000, 7000, 0.5, 0.25))
# As the game plays them: the send into the room (UJamWeaponsComponent::AirReverbSend); every 2D gun sound
# reaches the master this much under its file (measured on jam.Weapons.AirTake: a stereo wave -3.1 dB per
# channel, a mono one -9.1).
AIR_SEND = 0.3
GAME_DB = -3.1
# the suit's shot each crack sits on (jam_space_shots.py, the C in game)
SPACE = {"Pistol": "Shot_Pistol_C_revolver", "Shotgun": "Shot_Shotgun_C_mixed", "SV98": "Shot_SV98_C_deep"}
DRY_DIR = "."
_donors = {}


def donor(d):
    """A dry slice back on its donor's time: its attack 2 ms after the onset jam_space_shots.DRY gives (the
    slices were cut before jam-sync moved the onsets); silence around it. Peak-normalised, as jss.donor."""
    if d not in _donors:
        x = read(os.path.join(DRY_DIR, f"Donor_{d}_dry.wav")).mean(axis=1)
        onset, length = jss.DRY[d]
        at = int((onset + 0.002) * SR) - attack(x)
        y = np.zeros(at + len(x) + int(0.1 * SR))
        y[at:at + len(x)] = x
        _donors[d] = y / np.abs(y).max()
    return _donors[d]


def suit_shot(gun):
    """The game's suit shot (variant C) as jam_space_shots.main renders it: same spec, same seed."""
    name = SPACE[gun]
    jss.donor = donor
    return jss.render(jss.SOUNDS[name], seed=list(jss.SOUNDS).index(name) + 1)[0]


def read(path):
    with wave.open(path) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float64) / 32768.0
        x = x.reshape(-1, w.getnchannels())
        assert w.getframerate() == SR, path
    return x


def attack(x, db=-20.0):
    """The first sample over db (dBFS): where jam-sync put each shot's onset, 2 ms before it."""
    return int(np.nonzero(np.abs(x) > 10 ** (db / 20))[0][0])


def crack(d, length, hold, tau, hp_hz, lp_hz, amount, peak_db):
    x = read(os.path.join(DRY_DIR, f"Donor_{d}_dry.wav")).mean(axis=1)
    a = max(0, attack(x) - int(0.002 * SR))
    x = x[a:a + int(length * SR)]
    t = np.arange(len(x)) / SR
    env = np.where(t < 0.002 + hold, 1.0, np.exp(-(t - 0.002 - hold) / tau))
    y = jss.drive(x * env, amount)
    y = jss.filt(y, f"{hp(hp_hz, 2)},{lp(lp_hz, 2)}")
    y = jss.fade(y, 0.0005, 0.01)
    return y / np.abs(y).max() * 10 ** (peak_db / 20)


def cabin_ir(seed=7):
    rng = np.random.default_rng(seed)
    n = int((CABIN_RT60 + 0.05) * SR)
    t = np.arange(n) / SR
    onset = np.where(t < 0.002, 0.0, 1 - np.exp(-(t - 0.002) / 0.004))
    early = 1 + 1.5 * np.exp(-t / 0.012)  # the walls' first answers, denser
    ir = np.zeros((n, 2))
    for lo, hi, share, g in CABIN_BANDS:
        band = jss.filt(rng.standard_normal((n, 2)), f"{hp(lo)},{lp(hi, 1)}")
        ir += band * (g * onset * early * np.exp(-6.9 * t / (CABIN_RT60 * share)))[:, None]
    ir = jss.fade(ir, 0, 0.02) if ir.ndim == 1 else np.stack([jss.fade(ir[:, c], 0, 0.02) for c in range(2)], axis=1)
    return ir / np.sqrt((ir ** 2).sum() / 2)  # unit energy per channel: a send of s gives s^2 of the input's energy


def band_share(x, f0=2000.0):
    S = np.abs(np.fft.rfft(x)) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    return 100 * S[f >= f0].sum() / S.sum()


def tail_ms(y, below_db=-60.0):
    """From the peak to where the 10 ms envelope stays under the peak by below_db, ms."""
    env = np.convolve(np.abs(y.mean(axis=1)) if y.ndim > 1 else np.abs(y), np.ones(480) / 480, "same")
    pk = env.argmax()
    over = np.nonzero(env > env.max() * 10 ** (below_db / 20))[0]
    return (over[-1] - pk) / SR * 1000


def main():
    global DRY_DIR
    DRY_DIR, out_dir = sys.argv[1], sys.argv[2]
    listen = sys.argv[3] if len(sys.argv) > 3 else None
    for d in [out_dir] + ([listen] if listen else []):
        os.makedirs(d, exist_ok=True)
    rows = []
    ir = cabin_ir()
    jss.write(os.path.join(out_dir, "IR_Jam_Cabin.wav"), ir * 0.5, SR)  # x0.5: room in 16 bits; the editor script restores it
    rows.append(f"IR_Jam_Cabin: {len(ir) / SR:.2f} s, RT60 {CABIN_RT60} s, energy over 2 kHz {band_share(ir.mean(axis=1)):.1f} %, "
                f"over 700 Hz {band_share(ir.mean(axis=1), 700):.1f} %")
    if listen:
        jss.write(os.path.join(listen, "IR_Jam_Cabin.wav"), ir * 0.5, SR)
    for gun, spec in CRACKS.items():
        y = crack(*spec)
        jss.write(os.path.join(out_dir, f"SW_Jam_ShotAir_{gun}.wav"), y[:, None].repeat(2, axis=1), SR)
        li, lm = jss.loudness(y[:, None].repeat(2, axis=1))
        rows.append(f"ShotAir_{gun}: {len(y) / SR:.3f} s, peak {20 * np.log10(np.abs(y).max()):.1f} dBFS, LUFS-M max {lm:.1f}, "
                    f"energy over 2 kHz {band_share(y):.1f} %, 60 dB down after {tail_ms(y):.0f} ms")
        if listen:
            jss.write(os.path.join(listen, f"ShotAir_{gun}.wav"), y, SR)
        if not listen:
            continue
        game = 10 ** (GAME_DB / 20)
        vac = suit_shot(gun) * game
        dry = vac.copy()
        dry[:len(y)] += y[:, None] * game
        wet = jss.convolve(dry.mean(axis=1) * AIR_SEND, ir)
        air = np.zeros((max(len(dry), len(wet)), 2))
        air[:len(dry)] += dry
        air[:len(wet)] += wet
        for name, z in (("vacuum", vac), ("air", air)):
            lim, gr = jss.limit(z)
            li, lm = jss.loudness(lim)
            rows.append(f"  Mock {gun} {name}: peak before the limiter {20 * np.log10(np.abs(z).max()):+.1f} dBFS, limiter {gr:.1f} dB, "
                        f"LUFS-I {li:.1f}, LUFS-M max {lm:.1f}, energy over 2 kHz {band_share(lim.mean(axis=1)):.1f} %")
            jss.write(os.path.join(listen, f"Mock_{gun}_{name}.wav"), lim, SR)
        rows.append(f"  Mock {gun} the room alone (the crack sent): 60 dB down after {tail_ms(jss.convolve(y * AIR_SEND, ir)):.0f} ms")
        if gun == "SV98":  # the room's own decay (Schroeder backward integration of the IR), 500 Hz-4 kHz
            e = np.cumsum((jss.filt(ir.mean(axis=1), f"{hp(500)},{lp(4000)}") ** 2)[::-1])[::-1]
            d = 10 * np.log10(e / e[0] + 1e-30)
            t5, t25 = np.argmax(d < -5) / SR, np.argmax(d < -25) / SR
            rows.append(f"IR_Jam_Cabin decay: T20 x3 = {3 * (t25 - t5):.2f} s (500 Hz-4 kHz)")
    print("\n".join(rows))
    if listen:
        with open(os.path.join(listen, "INDEX.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(rows) + "\n")


if __name__ == "__main__":
    main()
