"""Tangent-space normal maps (UE/DirectX: green toward +V, image rows down) for the flesh set.

Run with any Python that has numpy and Pillow, outside the editor:
  python -I Scripts/make_flesh_normals.py <src_dir> <out_dir>
src_dir is the owner's texture set's lowres_256 folder. Writes 1024 px
T_OS_Gritty_<Name>_N.png for every flesh map (owner 18.10: «добавить объёма
через карты нормалей»): the height is the owner's height candidate where there
is one (Muscle, Tendon), else the colour map's luminance band-passed - fine
detail (blur 1 minus blur 4 texels) plus larger forms (blur 4 minus blur 16),
the larger forms stronger. The 256 px height is upscaled x4 bicubic (tiled, so
the edges wrap), softened, and turned into a normal; colour maps stay 256 px
Nearest for the retro look, the normals are smooth so the volume reads.
Non-tiling maps (MuscleEnd, PlanetEye) are padded by mirroring instead.
"""
import os
import sys

import numpy as np
from PIL import Image

SIZE = 1024
SOURCES = {
    # name: (relative path, strength, colour map (band-pass luminance), tiles)
    "Muscle": ("height_candidates/T_OS_Gritty_Muscle_H.png", 4.0, False, True),
    "Tendon": ("height_candidates/T_OS_Gritty_Tendon_H.png", 3.5, False, True),
    "PlanetSkin": ("maps/T_OS_Gritty_PlanetSkin_BC.png", 5.0, True, True),
    "Membrane": ("maps/T_OS_Gritty_Membrane_BC.png", 3.0, True, True),
    "Suckers": ("maps/T_OS_Gritty_Suckers_BC.png", 5.0, True, True),
    "PlanetEye": ("maps/T_OS_Gritty_PlanetEye_BC.png", 3.0, True, False),
    "MuscleEnd": ("maps/T_OS_Gritty_MuscleEnd_BC.png", 4.0, True, False),
}


def gaussian(img, radius):
    """Separable Gaussian blur of a 2-D float array (edges: the caller pads)."""
    r = max(1, int(radius * 3))
    x = np.arange(-r, r + 1, dtype=np.float32)
    k = np.exp(-0.5 * (x / radius) ** 2)
    k /= k.sum()
    img = np.apply_along_axis(lambda row: np.convolve(row, k, mode="same"), 1, img)
    return np.apply_along_axis(lambda col: np.convolve(col, k, mode="same"), 0, img)


def blur(height, radius, tiles):
    """Gaussian blur of a 2-D float array, wrapping (tiles) or mirroring at the edges."""
    h, w = height.shape
    big = np.tile(height, (3, 3)) if tiles else np.pad(height, ((h, h), (w, w)), mode="reflect")
    return gaussian(big, radius)[h:2 * h, w:2 * w]


def band_height(lum, tiles):
    fine = blur(lum, 1, tiles) - blur(lum, 4, tiles)
    large = blur(lum, 4, tiles) - blur(lum, 16, tiles)
    return 0.6 * fine + 1.4 * large


def upscale(height, factor, tiles):
    h, w = height.shape
    big = np.tile(height, (3, 3)) if tiles else np.pad(height, ((h, h), (w, w)), mode="reflect")
    img = np.asarray(Image.fromarray(big.astype(np.float32), mode="F").resize((w * 3 * factor, h * 3 * factor), Image.BICUBIC))
    return gaussian(img, factor * 0.5)[h * factor:2 * h * factor, w * factor:2 * w * factor]


def normal_map(height, strength):
    """height: 2-D float. Central differences with wrap, x right, rows down (+V)."""
    du = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dv = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    n = np.stack([-du * strength, -dv * strength, np.ones_like(height)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return np.clip((n * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8)


def main(src, out):
    os.makedirs(out, exist_ok=True)
    for name, (rel, strength, colour, tiles) in SOURCES.items():
        rgb = np.asarray(Image.open(os.path.join(src, rel)).convert("RGB"), dtype=np.float32) / 255.0
        lum = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
        height = band_height(lum, tiles) if colour else rgb[..., 0]
        height = (height - height.mean()) / max(float(height.std()), 1e-6) * 0.1   # comparable slopes across maps
        factor = SIZE // height.shape[0]
        big = upscale(height, factor, tiles)
        # The strength is per texel at 256: one 256 texel is `factor` texels at 1024.
        Image.fromarray(normal_map(big, strength * height.shape[0] / 64.0 * factor)).save(os.path.join(out, "T_OS_Gritty_%s_N.png" % name))
        print("wrote", name, big.shape)


def _check():
    # A ramp rising to the right tilts the normal left (-x); rising down the rows tilts it toward -V.
    ramp = np.tile(np.linspace(0, 1, 8, dtype=np.float32), (8, 1))
    assert normal_map(ramp, 1.0)[4, 4, 0] < 128
    assert normal_map(ramp.T, 1.0)[4, 4, 1] < 128


if __name__ == "__main__":
    _check()
    main(sys.argv[1], sys.argv[2])
