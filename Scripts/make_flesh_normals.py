"""Tangent-space normal maps (UE/DirectX: green toward +V, image rows down) from height or luminance PNGs.

Run with any Python that has numpy and Pillow, outside the editor:
  python -I Scripts/make_flesh_normals.py <src_dir> <out_dir>
src_dir is the owner's texture set's lowres_256 folder. Writes 256 px
T_Jam_Gritty_{Muscle,Tendon,PlanetSkin}_N.png: Muscle and Tendon from their
height candidates (light = raised), PlanetSkin from the colour map's luminance.
The maps tile (wrapped differences), so the normals tile too.
"""
import os
import sys

import numpy as np
from PIL import Image

SOURCES = {
    # name: (relative path, strength, is colour)
    "Muscle": ("height_candidates/T_Jam_Gritty_Muscle_H.png", 3.0, False),
    "Tendon": ("height_candidates/T_Jam_Gritty_Tendon_H.png", 2.5, False),
    "PlanetSkin": ("maps/T_Jam_Gritty_PlanetSkin_BC.png", 2.0, True),
}


def normal_map(height, strength):
    """height: 2-D float 0..1. Central differences with wrap, x right, rows down (+V)."""
    du = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dv = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    n = np.stack([-du * strength, -dv * strength, np.ones_like(height)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return np.clip((n * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8)


def main(src, out):
    os.makedirs(out, exist_ok=True)
    for name, (rel, strength, colour) in SOURCES.items():
        img = Image.open(os.path.join(src, rel)).convert("RGB")
        rgb = np.asarray(img, dtype=np.float32) / 255.0
        height = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32) if colour else rgb[..., 0]
        # 256 px: the strength is per texel, the slope of one texel step.
        Image.fromarray(normal_map(height, strength * height.shape[0] / 64.0)).save(os.path.join(out, "T_Jam_Gritty_%s_N.png" % name))
        print("wrote", name, img.size)


def _check():
    # A ramp rising to the right tilts the normal left (-x); rising down the rows tilts it toward -V.
    ramp = np.tile(np.linspace(0, 1, 8, dtype=np.float32), (8, 1))
    assert normal_map(ramp, 1.0)[4, 4, 0] < 128
    assert normal_map(ramp.T, 1.0)[4, 4, 1] < 128


if __name__ == "__main__":
    _check()
    main(sys.argv[1], sys.argv[2])
