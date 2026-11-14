#!/usr/bin/env python3
"""The PS4 menus' fonts: static instances cut from the variable Google Fonts (SIL OFL 1.1).

stb_truetype (ImGui) only draws a variable font's default instance, so the weights the menus use are
made here, once, from Platform/PS4/fonts/{Exo2,Orbitron}.ttf:

    Exo2-Regular.ttf     body text            Exo2-SemiBold.ttf   menu items
    Orbitron-Bold.ttf    titles

    python scripts/ps4/make_fonts.py
"""
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

DIR = Path(__file__).resolve().parents[2] / "Platform" / "PS4" / "fonts"
CUTS = [
    ("Exo2.ttf", "Exo2-Regular.ttf", 400),
    ("Exo2.ttf", "Exo2-SemiBold.ttf", 600),
    ("Orbitron.ttf", "Orbitron-Bold.ttf", 800),
]
for src, out, weight in CUTS:
    font = instancer.instantiateVariableFont(TTFont(DIR / src), {"wght": weight})
    font.save(DIR / out)
    print(out, (DIR / out).stat().st_size)
