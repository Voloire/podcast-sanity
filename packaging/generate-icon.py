"""Regenerate the Windows ICO from the shapes in data/podcast-sanity.svg.

Build-time artwork tool only; requires Pillow. The ICO is checked in so neither
building nor running the app requires Python.
"""
from pathlib import Path
from PIL import Image, ImageDraw

image = Image.new("RGBA", (256, 256))
mask = Image.new("L", image.size)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, 255, 255), radius=56, fill=255)
draw = ImageDraw.Draw(image)
for y in range(256):
    color = tuple(round(a + (b - a) * y / 255) for a, b in zip((143, 167, 149), (95, 122, 102)))
    draw.line((0, y, 255, y), fill=(*color, 255))
image.putalpha(mask)
for x, y, height in ((52, 112, 32), (74, 92, 72), (96, 72, 112),
                     (118, 100, 56), (140, 80, 96), (162, 108, 40), (184, 96, 64)):
    draw.rounded_rectangle((x, y, x + 12, y + height), radius=6, fill="#F4F1E8")
draw.ellipse((182, 50, 210, 78), fill="#F4F1E8")
draw.ellipse((189, 57, 203, 71), fill="#5F7A66")
image.save(Path(__file__).resolve().parents[1] / "data/podcast-sanity.ico",
           sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
