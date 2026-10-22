"""A low-resolution, gritty star map for the sky dome (any Python with numpy and Pillow).

  python -I Scripts/make_lowres_starmap.py <source.jpg> <out.png> [width]

The source is NASA SVS «Deep Star Maps 2020» (galactic projection, 3840 x 1920,
CREDITS.md; not in the repository). width defaults to 1024 (height = width / 2).
Plain averaging would erase the stars, so every output texel is a blend of its
block's mean and its block's brightest source pixel (STAR_KEEP): a star survives
as one chunky texel, the Milky Way keeps its band. Then a subtle grade toward the
gritty palette - shadows a little violet, mids a little warm dust - and a
posterize to LEVELS steps per channel. Imported with filter Nearest (os_sky.py).
"""
import sys

import numpy as np
from PIL import Image

STAR_KEEP = 0.7       # 0: block mean, 1: the block's brightest pixel
LEVELS = 40           # posterize steps per channel (sRGB)
VIOLET = np.array([0.55, 0.35, 1.0])   # shadow tint direction
DUST = np.array([1.0, 0.8, 0.55])      # mid tint direction
GRADE = 0.12          # strength of the split tone


def lowres(rgb, width):
    """rgb: H x W x 3 float 0..1 (sRGB). Block mean blended with the block's brightest pixel."""
    height = width // 2
    k = 4
    big = np.asarray(Image.fromarray((rgb * 255).astype(np.uint8)).resize((width * k, height * k), Image.BILINEAR), dtype=np.float32) / 255
    blocks = big.reshape(height, k, width, k, 3).transpose(0, 2, 1, 3, 4).reshape(height, width, k * k, 3)
    mean = blocks.mean(axis=2)
    lum = blocks @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    brightest = np.take_along_axis(blocks, lum.argmax(axis=2)[..., None, None], axis=2)[:, :, 0]
    return mean + (brightest - mean) * STAR_KEEP


def grade(rgb):
    lum = (rgb @ np.array([0.2126, 0.7152, 0.0722]))[..., None]
    shadow = np.clip(1 - lum * 4, 0, 1)
    mid = np.clip(1 - np.abs(lum - 0.35) * 3, 0, 1)
    tinted = rgb * (1 + GRADE * (shadow * (VIOLET - 1) + mid * (DUST - 1)))
    return np.round(np.clip(tinted, 0, 1) * (LEVELS - 1)) / (LEVELS - 1)


def main(src, out, width):
    rgb = np.asarray(Image.open(src).convert("RGB"), dtype=np.float32) / 255
    result = grade(lowres(rgb, width))
    Image.fromarray((result * 255 + 0.5).astype(np.uint8)).save(out)
    print("wrote %s %dx%d" % (out, width, width // 2))


def _check():
    # One bright pixel in a dark 64 x 32 image survives a 16 x 8 downscale as a bright texel.
    img = np.zeros((32, 64, 3), dtype=np.float32)
    img[10, 20] = 1.0
    small = lowres(img, 16)
    assert small.max() > 0.5, small.max()


if __name__ == "__main__":
    _check()
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 1024)
