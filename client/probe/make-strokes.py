"""Write deterministic scribble strokes for daily-probe: python make-strokes.py OUTDIR
Each file is "x y start" per point, in screen pixels, like a line of handwriting."""
import math
import pathlib
import sys

LINES = {"today-1": (80, 1470, "call mum re: Sunday lunch"), "today-2": (80, 1580, "which day works for the bike repair?"),
         "today-3": (80, 2050, "thanks!"), "garden-1": (80, 500, "3 beds, 1.2 m each"),
         "garden-2": (80, 610, "compost delivery Sat?"), "garden-3": (80, 720, "what soil mix for tomatos?"),
         "club-1": (80, 500, "3 questions for chapter 12?"), "bike-1": (80, 500, "what should I ask the bike shop?")}
out = pathlib.Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for name, (x0, y0, text) in LINES.items():
    rows, x = [], x0
    for i, ch in enumerate(text):
        if ch == " ":
            x += 28
            continue
        for k in range(8):                      # one wavy stroke per letter
            t = k / 7
            rows.append(f"{int(x + 40 * t)} {int(y0 + 18 * math.sin(3 * t + i) - 10 * t)} {1 if k == 0 else 0}")
        x += 44
    (out / f"{name}.txt").write_text("\n".join(rows) + "\n")
