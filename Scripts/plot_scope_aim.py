"""Plot the SV-98 scope's aim springs from a jam.Scope.Trace log (jam.Scope.AimFrames): python -I Scripts/plot_scope_aim.py <log> <out.png>."""
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

rows = []
pat = re.compile(r"Scope trace: t=([\d.]+) aim=(\d) place=([-\d.]+),([-\d.]+),([-\d.]+) turn=([-\d.]+),([-\d.]+),([-\d.]+) alpha=([-\d.]+)")
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = pat.search(line)
    if m:
        rows.append([float(v) for v in m.groups()])
assert rows, "no trace"
# Split into the raise (aim=1) and the lowering (aim=0), each from its own start.
runs = {}
for r in rows:
    runs.setdefault(int(r[1]), []).append(r)
fig, axes = plt.subplots(2, 2, figsize=(13, 8))
for col, (aim, title) in enumerate(((1, "raise (AimIn)"), (0, "lower (AimOut)"))):
    seg = runs.get(aim, [])
    if not seg:
        continue
    t0 = seg[0][0]
    t = [(r[0] - t0) * 1000.0 for r in seg]
    end = {1: (18.1, 0.0, -11.5), 0: (22.0, 13.0, -16.0)}[aim]
    start = {1: (22.0, 13.0, -16.0), 0: (18.1, 0.0, -11.5)}[aim]
    ax = axes[0][col]
    for i, name in enumerate(("X (toward eye)", "Y (across)", "Z (up)")):
        span = end[i] - start[i]
        ax.plot(t, [(r[2 + i] - start[i]) / span for r in seg], label=name)
    ax.plot(t, [r[8] for r in seg], "k--", lw=1, label="alpha")
    ax.axhline(1.0, color="grey", lw=0.5)
    ax.set_title("%s: place, share of the way" % title)
    ax.set_xlabel("ms after the press" if aim else "ms after the release")
    ax.set_xlim(0, 1200 if aim else 800)
    ax.legend(loc="lower right")
    ax = axes[1][col]
    for i, name in enumerate(("pitch", "yaw", "roll")):
        ax.plot(t, [r[5 + i] for r in seg], label=name)
    ax.axhline(0.0, color="grey", lw=0.5)
    ax.set_title("%s: turn, degrees" % title)
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
ax2.set_title("raise: the gun's path seen from behind")
ax2.invert_xaxis()
fig2.tight_layout()
fig2.savefig(sys.argv[2].replace(".png", "_path.png"), dpi=90)
for aim, seg in runs.items():
    t0 = seg[0][0]
    print("aim=%d: %d samples over %.0f ms" % (aim, len(seg), (seg[-1][0] - t0) * 1000))
