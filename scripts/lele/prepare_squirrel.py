#!/usr/bin/env python3
"""Prepare native-size squirrel PNGs from the checked-in imagegen originals.

Requires Pillow; this is offline asset preparation, not a firmware build.
"""
import argparse
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageOps


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "main/boards/lckfb/szpi-esp32s3/assets/squirrel"
EXPRESSIONS = ("neutral", "happy", "listening", "speaking", "thinking", "sad", "sleepy")
SIZE = 259  # Match the former 144px * 461/256 display extent, without upscaling.


def prepare(check=False):
    images = {}
    for name in EXPRESSIONS:
        with Image.open(ASSETS / "originals" / (name + ".png")) as source:
            image = source.convert("RGBA")
        if image.getchannel("A").getextrema() != (0, 255):
            raise ValueError(f"{name}: expected transparent RGBA artwork")
        if min(image.size) < SIZE:
            raise ValueError(f"{name}: use the high-resolution original, not a small runtime PNG")
        images[name] = image
    if len({image.size for image in images.values()}) != 1:
        raise ValueError("Originals must share one canvas size for consistent framing")
    bounds = [image.getchannel("A").getbbox() for image in images.values()]
    # Keep the same joint crop and padding as the original 144px preparation.
    # Individual crops would make the character shift when expressions change.
    crop = (min(b[0] for b in bounds) - 12, min(b[1] for b in bounds) - 12,
            max(b[2] for b in bounds) + 12, max(b[3] for b in bounds) + 12)
    for name, image in images.items():
        small = ImageOps.contain(image.crop(crop), (SIZE, SIZE), Image.Resampling.LANCZOS)
        canvas = Image.new("RGBA", (SIZE, SIZE))
        canvas.paste(small, ((SIZE - small.width) // 2, (SIZE - small.height) // 2))
        encoded = BytesIO()
        canvas.save(encoded, format="PNG", optimize=True)
        target = ASSETS / ("squirrel_" + name + ".png")
        if check:
            if not target.is_file() or target.read_bytes() != encoded.getvalue():
                raise ValueError(f"Out-of-date prepared asset: {target.relative_to(ROOT)}")
        else:
            target.write_bytes(encoded.getvalue())
        print(f"{'Verified' if check else 'Prepared'} {target.name}: {SIZE}x{SIZE}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify without writing assets")
    prepare(parser.parse_args().check)
