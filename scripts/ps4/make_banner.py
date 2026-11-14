#!/usr/bin/env python3
"""The README's banner (docs/images/banner.png, 1600x480): a chrome pinball between two neon flippers
in a starfield, and the title in Orbitron. Original art: nothing from the game.

    python scripts/ps4/make_banner.py
"""
import math
import random
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "Platform" / "PS4" / "fonts"
OUT = ROOT / "docs" / "images" / "banner.png"
W, H = 1600, 480
K = 2  # drawn at 2x, shrunk for smooth edges
CYAN = (64, 196, 255)
VIOLET = (190, 110, 255)


def background():
    img = Image.new("RGBA", (W, H), (5, 8, 22, 255))
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(glow)
    d.ellipse((120, -120, 760, 600), fill=(40, 30, 110, 255))
    d.ellipse((900, 180, 1700, 700), fill=(10, 60, 110, 255))
    img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(150)))
    stars = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(stars)
    rnd = random.Random(1995)
    for _ in range(420):
        x, y = rnd.uniform(0, W), rnd.uniform(0, H)
        r = rnd.choice((0.6, 0.8, 1.0, 1.4, 2.0))
        a = rnd.randint(90, 255)
        d.ellipse((x - r, y - r, x + r, y + r), fill=(220, 230, 255, a))
    return Image.alpha_composite(img, stars)


def flipper(d, pivot, angle, length, width, color):
    """A tapered flipper bat: a circle at the pivot, a smaller one at the tip, joined."""
    px, py = pivot
    tx, ty = px + math.cos(angle) * length, py + math.sin(angle) * length
    r1, r2 = width, width * 0.45
    nx, ny = -math.sin(angle), math.cos(angle)
    poly = [(px + nx * r1, py + ny * r1), (tx + nx * r2, ty + ny * r2), (tx - nx * r2, ty - ny * r2),
            (px - nx * r1, py - ny * r1)]
    d.polygon(poly, fill=color)
    d.ellipse((px - r1, py - r1, px + r1, py + r1), fill=color)
    d.ellipse((tx - r2, ty - r2, tx + r2, ty + r2), fill=color)


def art():
    """The ball and the flippers, with their glow."""
    layer = Image.new("RGBA", (W * K, H * K), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy, r = 430 * K, 210 * K, 120 * K
    # flippers below the ball
    flipper(d, (250 * K, 410 * K), math.radians(-18), 170 * K, 26 * K, CYAN + (255,))
    flipper(d, (610 * K, 410 * K), math.radians(198), 170 * K, 26 * K, VIOLET + (255,))
    # the chrome ball: rings from dark edge to bright centre-top
    for i in range(r, 0, -2):
        t = i / r
        shade = int(40 + 200 * (1 - t) ** 0.7)
        off = int((1 - t) * 0.35 * r)
        d.ellipse((cx - i - off, cy - i - off, cx + i - off, cy + i - off),
                  fill=(shade, min(255, shade + 12), min(255, shade + 30), 255))
    # the room reflected in its lower half, and a hot highlight
    d.chord((cx - r + 10 * K, cy - 10 * K, cx + r - 10 * K, cy + r - 6 * K), 10, 170, fill=(30, 60, 120, 55))
    d.ellipse((cx - 70 * K, cy - 80 * K, cx - 20 * K, cy - 40 * K), fill=(255, 255, 255, 230))
    layer = layer.resize((W, H), Image.LANCZOS)
    glow = layer.filter(ImageFilter.GaussianBlur(28))
    return glow, layer


def gradient_text(text, font, colors):
    """Text filled with a left-to-right gradient, as an RGBA image cropped to the text."""
    box = font.getbbox(text)
    w, h = box[2] - box[0] + 8, box[3] - box[1] + 8
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).text((-box[0] + 4, -box[1] + 4), text, font=font, fill=255)
    grad = Image.new("RGBA", (w, h))
    gd = ImageDraw.Draw(grad)
    for x in range(w):
        t = x / max(1, w - 1)
        c = tuple(int(colors[0][i] * (1 - t) + colors[1][i] * t) for i in range(3))
        gd.line((x, 0, x, h), fill=c + (255,))
    grad.putalpha(mask)
    return grad


def main():
    img = background()
    glow, art_layer = art()
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, art_layer)

    title_font = ImageFont.truetype(str(FONTS / "Orbitron-Bold.ttf"), 92)
    sub_font = ImageFont.truetype(str(FONTS / "Orbitron-Bold.ttf"), 40)
    body_font = ImageFont.truetype(str(FONTS / "Exo2-SemiBold.ttf"), 30)
    x = 770
    lines = [("SPACE CADET", 120), ("PINBALL", 222)]
    for text, y in lines:
        t = gradient_text(text, title_font, (CYAN, VIOLET))
        shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
        shadow.paste(t, (x, y), t)
        shadow = shadow.filter(ImageFilter.GaussianBlur(16))
        img = Image.alpha_composite(img, shadow)
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        layer.paste(t, (x, y), t)
        img = Image.alpha_composite(img, layer)
    d = ImageDraw.Draw(img)
    d.text((x + 4, 334), "FOR  PS4", font=sub_font, fill=(236, 240, 250, 255))
    d.text((x + 4, 392), "Native port  ·  DualShock 4  ·  1080p / 720p", font=body_font, fill=(150, 160, 185, 255))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    img.convert("RGB").save(OUT)
    print(OUT)


if __name__ == "__main__":
    main()
