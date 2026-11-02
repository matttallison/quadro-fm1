#!/usr/bin/env python3
# The README's banner (app/docs/img/hero.png): four of app/tests/screens.txt's screenshots, doubled, side by side.
from pathlib import Path
from PIL import Image
img = Path(__file__).resolve().parent.parent / "app" / "docs" / "img"
names, g, s = ["home", "chord", "edit-bass", "sequencer"], 24, 480
out = Image.new("RGB", (len(names) * (s + g) + g, s + 2 * g), (12, 12, 14))
for i, n in enumerate(names):
    out.paste(Image.open(img / f"{n}.png").convert("RGB").resize((s, s), Image.NEAREST), (g + i * (s + g), g))
out.save(img / "hero.png", optimize=True)
