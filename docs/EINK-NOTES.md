# Drawing to the Scribe's e-ink panel from X11

Short notes from getting pen styles to show up. They explain a few choices in the client.

## Grey disappears on fast refreshes

The app draws into an X pixmap, and `fbink_xdamage` pushes the changed regions to the panel
through FBInk. While you write, regions are refreshed with the A2 waveform, which is
black and white only. On Kindle MTK hardware (the Scribe is one), FBInk sets
`EPDC_FLAG_FORCE_MONOCHROME` for A2 updates, and the controller turns that into a 50%
threshold on the whole region. `gray50` sits exactly on that threshold and comes out white.

That's why a grey pencil looked fine in a screenshot of the X framebuffer and vanished on
the panel. The drawing code was correct. The refresh path was the problem.

## What the client does about it

- The pencil is drawn as black dots in a fixed pattern instead of grey. One-bit pixels
  survive every waveform. The pattern comes from a hash of the absolute coordinates, so it
  looks the same on every repaint.
- Pen width is stored per point, not pressure. The style is only a mapping at capture
  time, and the renderer doesn't care which pen made the stroke.
- Test thin and thick strokes on the device itself. A harness under Xvfb models the
  drawing but not the display pipeline, and a bug that was invisible at medium width was
  total at thin.

## Refresh timing

FBInk's own figures for the waveforms: A2 about 120 ms, DU about 260 ms, GC16 or REAGL
about 450 ms. You pick the mode on purpose, because no amount of fast code beats it.
KOReader switches off the driver's automatic promotion of large partial updates to REAGL
on the Scribe (`fbink_mtk_toggle_auto_reagl()`) for the same reason.

## Reference

- FBInk: https://github.com/NiLuJe/FBInk
- `fbink_xdamage`: https://github.com/schuhumi/fbink-xdamage
