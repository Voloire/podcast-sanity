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

# WiX UI bitmaps use the Studio palette and the bundled Ubuntu typeface.
from PIL import ImageFont
root = Path(__file__).resolve().parents[1]
def ubuntu(size, weight="Bold"):
    try:
        return ImageFont.truetype(str(root / f"data/fonts/Ubuntu-{weight}.ttf"), size)
    except OSError:
        return ImageFont.load_default()
charcoal, amber, cream = (14, 16, 18), (242, 163, 60), (230, 227, 218)
banner = Image.new("RGB", (493, 58), charcoal)
d = ImageDraw.Draw(banner)
d.rectangle((0, 54, 493, 58), fill=amber)
d.text((16, 14), "PODCAST SANITY", font=ubuntu(22), fill=cream)
d.text((238, 22), "record  -  clean  -  publish", font=ubuntu(13, "Regular"), fill=(164, 167, 158))
banner.save(root / "data/wix-banner.bmp")
dialog = Image.new("RGB", (493, 312), charcoal)
d = ImageDraw.Draw(dialog)
d.rectangle((0, 0, 164, 312), fill=(21, 24, 27))
for x, y, h in ((28, 150, 40), (46, 128, 84), (64, 108, 124), (82, 136, 68), (100, 116, 108), (118, 144, 52), (136, 132, 76)):
    d.rounded_rectangle((x, y, x + 10, y + h), radius=5, fill=amber)
d.text((184, 40), "Podcast Sanity", font=ubuntu(28), fill=cream)
d.text((184, 84), "Record a podcast and master it locally.", font=ubuntu(14, "Regular"), fill=(164, 167, 158))
d.text((184, 106), "No upload, no cloud, no telemetry.", font=ubuntu(14, "Regular"), fill=(164, 167, 158))
dialog.save(root / "data/wix-dialog.bmp")
