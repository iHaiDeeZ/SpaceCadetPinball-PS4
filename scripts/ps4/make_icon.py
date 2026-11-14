#!/usr/bin/env python3
"""The package's icon0.png (512x512): the game's own 192x192 icon, scaled up on a dark field.

    python3 scripts/ps4/make_icon.py
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "SpaceCadetPinball" / "Icon_192x192.png"
OUT = ROOT / "Platform" / "PS4" / "sce_sys" / "icon0.png"

icon = Image.open(SRC).convert("RGBA").resize((448, 448), Image.LANCZOS)
canvas = Image.new("RGBA", (512, 512), (12, 16, 40, 255))
canvas.alpha_composite(icon, (32, 32))
OUT.parent.mkdir(parents=True, exist_ok=True)
canvas.convert("RGB").save(OUT)
print(OUT)
