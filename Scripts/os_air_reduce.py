"""Reads a take of os.Weapons.AirTake: the guns in air against the same in vacuum (Docs/RecoilWeapons-Sound.md, «В воздухе»).

    python Scripts/os_air_reduce.py <AirTake_X.wav> <log>

With a sync take (os.Weapons.AirTake sync) it gives the onset of every single shot per medium instead.

The log's lines «Air take: [air|vacuum] <event> at +<s> s» place each event in the wav (seconds from the
recording's start; the recording begins on the next mix buffer, so every time carries the same small offset).
Per event and medium, in the event's window: the peak (dBFS), the loudest 400 ms (LUFS-M max, BS.1770), the
share of energy above 2 kHz, the onset after the event (the 5 ms envelope 10 dB over what came before; the
offset is the same for both media, so air minus vacuum is what the air path adds), and for «room alone» how
long the sound rings (10 ms envelope from its peak to 60 dB under it). Then the whole take: peak, samples over
-1 dBFS, RMS. Needs numpy.
"""
import os
import re
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os_space_shots as jss  # noqa: E402  (loudness: BS.1770 at 48 kHz)

# event -> window, s (each to before the next event)
WINDOWS = {"pistol shot": 1.4, "pistol burst": 1.6, "switch to 1": 1.45, "shotgun shot": 2.45, "switch to 2": 1.45,
           "sv98 shot": 4.95, "switch to 0": 1.45, "pistol last round": 0.95, "pistol empty reload": 1.9, "room alone": 1.6}


def main():
    path, log = sys.argv[1], sys.argv[2]
    with wave.open(path) as w:
        rate, ch = w.getframerate(), w.getnchannels()
        x = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float64).reshape(-1, ch) / 32768.0
    if rate != jss.SR:
        raise SystemExit(f"{path}: {rate} Hz, the loudness needs {jss.SR}")
    events, sync = {}, {"air": [], "vacuum": []}
    for line in open(log, encoding="utf-8", errors="replace"):
        m = re.search(r"Air take: \[(air|vacuum)\] (.+?) (?:fired |REFUSED )?at \+([\d.]+) s", line)
        if m and m.group(2) == "sync shot":
            sync[m.group(1)].append(float(m.group(3)))
        elif m and m.group(2) in WINDOWS and (m.group(1), m.group(2)) not in events:
            events[(m.group(1), m.group(2))] = float(m.group(3))
    m = x.mean(axis=1)
    env5 = np.sqrt(np.convolve(m ** 2, np.ones(240) / 240, "same"))
    env10 = np.sqrt(np.convolve(m ** 2, np.ones(480) / 480, "same"))
    rows = []

    def onset(t, window):
        a, b = int(t * rate), int((t + window) * rate)
        before = env5[max(0, a - int(0.05 * rate)):a].max() + 1e-6
        on = np.nonzero(env5[a:b] > before * 10 ** (10 / 20))[0]
        return on[0] / rate * 1000 if len(on) else float("nan")
    for medium, times in sync.items():
        if times:
            ms = np.array([onset(t, 1.0) for t in times])
            rows.append(f"sync shots in {medium}: {len(ms)}, onset {np.nanmean(ms):.1f} ms mean, {np.nanmin(ms):.1f}-{np.nanmax(ms):.1f} "
                        f"(each: {', '.join(f'{v:.1f}' for v in ms)})")
    for name in WINDOWS:
        cells = {}
        for medium in ("air", "vacuum"):
            t = events.get((medium, name))
            if t is None:
                continue
            a, b = int(t * rate), int((t + WINDOWS[name]) * rate)
            seg = x[a:b]
            S = np.abs(np.fft.rfft(seg.mean(axis=1))) ** 2
            f = np.fft.rfftfreq(len(seg), 1 / rate)
            c = dict(peak=20 * np.log10(np.abs(seg).max() + 1e-9), mom=jss.loudness(seg)[1],
                     hf=100 * S[f >= 2000].sum() / (S.sum() + 1e-20), onset=onset(t, WINDOWS[name]))
            if name == "room alone":
                e = env10[a:b]
                pk = e.argmax()
                under = np.nonzero(e[pk:] < e[pk] * 10 ** (-60 / 20))[0]
                c["ring"] = under[0] / rate * 1000 if len(under) else float("nan")
            cells[medium] = c
        if len(cells) < 2:
            if not any(sync.values()):
                    rows.append(f"{name}: missing in the log ({', '.join(cells) or 'both'})")
            continue
        A, V = cells["air"], cells["vacuum"]
        line = (f"{name}: peak {A['peak']:.1f} / {V['peak']:.1f} dBFS, LUFS-M max {A['mom']:.1f} / {V['mom']:.1f} "
                f"({A['mom'] - V['mom']:+.1f} LU), over 2 kHz {A['hf']:.1f} / {V['hf']:.2f} % "
                f"({10 * np.log10((A['hf'] + 1e-6) / (V['hf'] + 1e-6)):+.1f} dB), onset {A['onset']:.1f} / {V['onset']:.1f} ms")
        if "ring" in A:
            line += f", rings {A['ring']:.0f} / {V['ring']:.0f} ms to -60 dB"
        rows.append(line)
    over = int((np.abs(x) > 10 ** (-1 / 20) + 1e-4).any(axis=1).sum())
    rows.append(f"whole take: {len(x) / rate:.1f} s, peak {20 * np.log10(np.abs(x).max() + 1e-9):.2f} dBFS, {over} frame(s) over -1 dBFS, "
                f"RMS {10 * np.log10((m ** 2).mean() + 1e-20):.1f} dBFS (air / vacuum in each row)")
    print("\n".join(rows))


if __name__ == "__main__":
    main()
