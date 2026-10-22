"""Reduce a os.Sync.Probe take: per shot, when the sound starts (the master submix
recording), when the flash shows (the recorded video) and when the gun's kick peaks
(Saved/Clips/SyncFrames.csv), all against the game-thread moment Fire() ran.

    python Scripts/os_sync_reduce.py <Saved/Clips dir> <the probe's log>

Sound: the recording starts with OSClipRecorder::Start (logged as "recording started at
platform"), so wav time + that platform time is when a sample was mixed (to within one
audio callback); the device plays it later (its own buffers, not in the recording).
Onsets are taken on 5 ms RMS envelopes: the first moment 10 dB over the level at Fire,
full band (the sub starts it) and above 300 Hz (the crack the ear times by).
Flash: the first video frame whose lower-right two thirds are over 2 grey levels brighter
than the three frames before Fire's frame (the pistol's flash is small). Kick: the frames of
the largest gun displacement in view (position, cm; rotation, degrees) and of the largest
view pitch change within 0.6 s after Fire.
"""
import csv
import os
import re
import subprocess
import sys
import wave

import numpy as np

clips, log_path = sys.argv[1], sys.argv[2]
log = open(log_path, encoding="utf-8", errors="ignore").read()
t0 = float(re.search(r"recording started at platform ([\d.]+)", log).group(1))
fires = [(float(m.group(1)), int(m.group(2)), int(m.group(3))) for m in
         re.finditer(r"Sync: fire ok at platform ([\d.]+) s .*? video frame (\d+), gun (\d+)", log)]

w = wave.open(os.path.join(clips, "SyncProbe.wav"))
sr = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(float).reshape(-1, w.getnchannels()).mean(axis=1) / 32768
spec = np.fft.rfft(x)
freqs = np.fft.rfftfreq(len(x), 1 / sr)
high = np.fft.irfft(np.where(freqs >= 300, spec, 0), len(x))
win = int(0.005 * sr)


def envelope(s):
    return np.sqrt(np.convolve(s ** 2, np.ones(win) / win, "same"))


env_all, env_high = envelope(x), envelope(high)


def onset(env, at):
    """First rise 10 dB over the level at Fire (a tail of an earlier shot included)."""
    i = int(at * sr)
    base = env[max(0, i - int(0.01 * sr)):i + 1].max()
    after = np.nonzero(env[i:i + int(0.6 * sr)] > max(base * 3.16, 1e-3))[0]
    return after[0] / sr if len(after) else float("nan")


W, H = 160, 90
raw = subprocess.run(["ffmpeg", "-v", "error", "-i", os.path.join(clips, "SyncProbe.mp4"), "-vf", f"scale={W}:{H}",
                      "-f", "rawvideo", "-pix_fmt", "gray", "-"], capture_output=True, check=True).stdout
frames = np.frombuffer(raw, np.uint8).reshape(-1, H, W).astype(float)
lower_right = frames[:, H // 3:, W // 3:].mean(axis=(1, 2))
rows = list(csv.DictReader(open(os.path.join(clips, "SyncFrames.csv"))))
frame_time = {int(r["video_frame"]): float(r["platform_s"]) for r in rows}
pose = {int(r["video_frame"]): np.array([float(r[k]) for k in ("gun_x", "gun_y", "gun_z", "gun_pitch", "gun_yaw", "gun_roll", "view_pitch")])
        for r in rows}

names = {0: "pistol", 1: "shotgun", 2: "SV-98"}
print("gun      shot | sound mixed: full band, >300 Hz | flash: frame, ms | kick peak: frame, ms (gun cm / gun deg / view deg)")
for platform, frame, gun in fires:
    at = platform - t0
    a_all, a_high = onset(env_all, at), onset(env_high, at)
    base = lower_right[frame - 3:frame].mean()
    lit = [f for f in range(frame - 1, frame + 10) if lower_right[f] - base > 2]
    flash = lit[0] if lit else None
    flash_ms = (frame_time.get(flash, float("nan")) - platform) * 1000 if flash is not None else float("nan")
    # The row of video frame f carries the pose the frame before rendered (the probe ticks first).
    rest = pose[frame]
    later = [f for f in range(frame + 1, frame + 80) if f in pose and frame_time[f] - platform < 0.6]
    move = {f: np.concatenate([pose[f][:3] - rest[:3], (pose[f][3:] - rest[3:] + 180.0) % 360.0 - 180.0]) for f in later}
    cm = max(later, key=lambda f: np.linalg.norm(move[f][:3]))
    deg = max(later, key=lambda f: np.abs(move[f][3:6]).max())
    view = max(later, key=lambda f: abs(move[f][6]))
    ms = lambda f: (frame_time[f] - platform) * 1000 - 0.0  # noqa: E731
    print(f"{names[gun]:8s} {frame:5d} | {a_all * 1000:6.0f} ms, {a_high * 1000:6.0f} ms | {flash}, {flash_ms:5.1f} ms | "
          f"{ms(cm - 1):5.0f} / {ms(deg - 1):5.0f} / {ms(view - 1):5.0f} ms ({np.linalg.norm(move[cm][:3]):.1f} cm, "
          f"{np.abs(move[deg][3:6]).max():.1f} deg, {abs(move[view][6]):.1f} deg)")
