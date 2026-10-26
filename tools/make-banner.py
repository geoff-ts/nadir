"""Draw the 720 x 320 appstore banner from the screenshots and Bruno Ace.

Run from the repo root: python tools/make-banner.py
Writes docs/banner.png.
"""
import math
import random

from PIL import Image, ImageDraw, ImageFont

W, H = 720, 320
FONT = 'resources/fonts/BrunoAce-Regular.ttf'
SHOTS = ['docs/screenshots/emery-day.png', 'docs/screenshots/emery-night.png']

img = Image.new('RGB', (W, H))
px = img.load()
for y in range(H):
    for x in range(W):
        # Deep blue at the top left fading to black.
        t = min(1.0, math.hypot(x / W, y / H) / 1.2)
        px[x, y] = (int(12 * (1 - t)), int(22 * (1 - t)), int(48 * (1 - t)) + 4)

draw = ImageDraw.Draw(img)
rng = random.Random(7)
for _ in range(140):
    x, y = rng.randrange(W), rng.randrange(H)
    v = rng.randrange(70, 200)
    draw.point((x, y), fill=(v, v, min(255, v + 30)))

# A faint ecliptic arc sweeping behind the text.
draw.arc((-260, 60, 620, 700), 200, 330, fill=(60, 80, 130), width=2)
for deg, colour in ((250, (255, 170, 0)), (258, (255, 255, 255)), (268, (255, 85, 0))):
    a = math.radians(deg)
    cx, cy = 180 + 440 * math.cos(a), 380 + 320 * math.sin(a)
    draw.ellipse((cx - 4, cy - 4, cx + 4, cy + 4), fill=colour)

title = ImageFont.truetype(FONT, 52)
sub = ImageFont.truetype(FONT, 17)
draw.text((36, 112), 'NADIR', font=title, fill=(255, 255, 255))
draw.text((40, 184), 'THE SKY RIGHT NOW,', font=sub, fill=(85, 255, 255))
draw.text((40, 208), 'AROUND THE TIME', font=sub, fill=(85, 255, 255))


def watch(shot, left, top):
    """Paste a screenshot inside a plain dark bezel."""
    pad = 8
    draw.rounded_rectangle((left - pad, top - pad, left + 200 + pad, top + 228 + pad),
                           radius=18, fill=(28, 28, 32), outline=(70, 70, 80), width=2)
    img.paste(Image.open(shot).convert('RGB'), (left, top))


watch(SHOTS[0], 290, 46)
watch(SHOTS[1], 508, 46)

img.save('docs/banner.png')
