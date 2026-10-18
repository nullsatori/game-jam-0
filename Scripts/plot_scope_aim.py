"""Plot a gun's aim springs from a jam.Scope.Trace log (jam.Scope.AimFrames).

  python -I Scripts/plot_scope_aim.py <log> <out.png> [gun]   (gun: 0 pistol, 1 pump gun, 2 SV-98; default 2)
"""
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

GUN = int(sys.argv[3]) if len(sys.argv) > 3 else 2
NAMES = {0: "pistol", 1: "pump gun", 2: "SV-98"}
rows = []
pat = re.compile(r"Scope trace: gun=(-?\d+) t=([\d.]+) aim=(\d) place=([-\d.]+),([-\d.]+),([-\d.]+) turn=([-\d.]+),([-\d.]+),([-\d.]+) alpha=([-\d.]+)")
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = pat.search(line)
    if m and int(m.group(1)) == GUN:
        rows.append([float(v) for v in m.groups()[1:]])
assert rows, "no trace"
# Contiguous runs of the same target; the first raise (cut before the shot at 1.3 s) and the lowering after it.
segments = []
for r in rows:
    if not segments or int(r[1]) != int(segments[-1][-1][1]) or r[0] - segments[-1][-1][0] > 0.2:
        segments.append([])
    segments[-1].append(r)
runs = {}
for i, seg in enumerate(segments):
    if int(seg[0][1]) == 1 and 1 not in runs:
        runs[1] = [r for r in seg if r[0] - seg[0][0] < 1.25]
        after = [s2 for s2 in segments[i + 1:] if int(s2[0][1]) == 0]
        if after:
            runs[0] = [r for r in after[0] if r[0] - after[0][0][0] < 0.8]
fig, axes = plt.subplots(2, 2, figsize=(13, 8))
for col, (aim, title) in enumerate(((1, "raise (AimIn)"), (0, "lower (AimOut)"))):
    seg = runs.get(aim, [])
    if not seg:
        continue
    t0 = seg[0][0]
    t = [(r[0] - t0) * 1000.0 for r in seg]
    start = seg[0][2:5]
    end = seg[-1][2:5]
    ax = axes[0][col]
    for i, name in enumerate(("X (toward eye)", "Y (across)", "Z (up)")):
        span = end[i] - start[i]
        if abs(span) > 1e-3:
            ax.plot(t, [(r[2 + i] - start[i]) / span for r in seg], label="%s %+.1f cm" % (name, span))
    ax.plot(t, [r[8] for r in seg], "k--", lw=1, label="alpha")
    ax.axhline(1.0, color="grey", lw=0.5)
    ax.set_title("%s %s: place, share of the way" % (NAMES[GUN], title))
    ax.set_xlabel("ms after the press" if aim else "ms after the release")
    ax.set_xlim(0, 1200 if aim else 800)
    ax.legend(loc="lower right")
    ax = axes[1][col]
    for i, name in enumerate(("pitch", "yaw", "roll")):
        ax.plot(t, [r[5 + i] for r in seg], label=name)
    ax.axhline(0.0, color="grey", lw=0.5)
    ax.set_title("%s %s: turn, degrees" % (NAMES[GUN], title))
    ax.set_xlabel("ms")
    ax.set_xlim(0, 1200 if aim else 800)
    ax.legend(loc="upper right")
fig.tight_layout()
fig.savefig(sys.argv[2], dpi=90)
# The path's arc: Y-Z in the camera's frame during the raise.
seg = runs[1]
fig2, ax2 = plt.subplots(figsize=(6, 5))
ax2.plot([r[3] for r in seg], [r[4] for r in seg], "-o", ms=2)
ax2.set_xlabel("Y cm (right)")
ax2.set_ylabel("Z cm (up)")
ax2.set_title("%s raise: the gun's path seen from behind" % NAMES[GUN])
ax2.invert_xaxis()
fig2.tight_layout()
fig2.savefig(sys.argv[2].replace(".png", "_path.png"), dpi=90)
for aim, seg in runs.items():
    t0 = seg[0][0]
    print("aim=%d: %d samples over %.0f ms" % (aim, len(seg), (seg[-1][0] - t0) * 1000))
# Aimed idle (the breath): the longest aimed run over 3 s, its turn once the raise and the look turn are over.
idle = [seg for seg in segments if int(seg[0][1]) == 1 and seg[-1][0] - seg[0][0] > 3.0]
if idle:
    seg = max(idle, key=lambda s2: s2[-1][0] - s2[0][0])
    seg = [r for r in seg if r[0] - seg[0][0] > 2.6]  # after the raise and the look turn (jam.Scope.AimFrames)
    fig3, ax3 = plt.subplots(figsize=(9, 4))
    for i, name in enumerate(("pitch", "yaw", "roll")):
        ax3.plot([r[0] - seg[0][0] for r in seg], [r[5 + i] for r in seg], label=name)
    ax3.set_title("%s aimed idle: the breath, degrees" % NAMES[GUN])
    ax3.set_xlabel("s (game time)")
    ax3.legend(loc="upper right")
    fig3.tight_layout()
    fig3.savefig(sys.argv[2].replace(".png", "_idle.png"), dpi=90)
    print("idle: %d samples over %.1f s" % (len(seg), seg[-1][0] - seg[0][0]))
