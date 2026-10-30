# Why the pen styles do not show, and what to do about it

> **Historical log.** These are the author's notes from debugging the pen styles on his own
> Scribe. The probe programs and helper sources they mention (`style-probe.c`, `fbink.c`,
> `vendor/fbink-xdamage/main.c`) are not part of this repository.

Written 2026-09-12 after a failed pair of fixes to the drawing code. Read this
before touching `draw_segment()` again — the drawing code was never the bug.

## What is actually proven

**1. The drawing code is correct on the device.** `style-probe.c` cross-compiled
for ARM and run on the Scribe itself, against the real X server, reading the
Pixmap directly, passes all 24 assertions:

```
BALLPOINT   2    4    6    9   13     (exact table widths)
FOUNTAIN    3    6    8   13   18     (velocity-modulated)
FELT        6    9   12   16   22     (marker, clearly wider)
PENCIL      4    4    5    6    8     (grey, 57-164 texture gaps)
```

Run it on the device, not under Xvfb:

```sh
DISPLAY=:0 /mnt/us/hermes-native-live/diag/style-probe-arm
```

**2. The X framebuffer contains the grey.** Captured with `xwd -id`, the window
holds ~0.5% pixels at colormap index 127 = `gray50`. The pixels are drawn.

**3. The refresh bridge destroys them.** `vendor/fbink-xdamage/main.c`:

```c
94:  case RM_QUICKLY: fbink_cfg.wfm_mode = WFM_A2;   // monochrome-only waveform
137: if(areaState == AS_DRAWN_QUICKLY || (tdiffms<500) && msElapsedSince(creation)>200) {
         refresh(RM_QUICKLY, ...);        // everything drawn while writing
     } else { refresh(RM_PRETTY, ...); }  // WFM_AUTO, grayscale-capable
```

`WFM_A2` is a black-and-white-only waveform. On Kindle MTK hardware — which
explicitly includes the **Scribe** (`refresh_kindle_mtk()` in FBInk's `fbink.c`
names it) — FBInk sets `EPDC_FLAG_FORCE_MONOCHROME` on every A2 update, and the
EPDC implements that as **50%-threshold posterization of the whole region**.
`gray50` is 128/255 = exactly 50%, so it sits on the threshold and collapses to
white. That is the disappearing pencil.

This also explains the user's report that "pencil works for about 0.5 second":
that `500` in line 137 is the idle threshold. While drawing, every region is A2
(no grey); when the pen pauses past 500 ms, housekeeping issues a
`PRETTY_CLEANUP` with `WFM_AUTO`, the greys come back briefly, and the next
stroke puts the area back into A2.

**4. The bridge has three further defects**, any of which alone would misbehave:

```c
38:  int rect1InsideRect2(...) { if(...){ ...return true; } }   // no return on the
                                                              // fall-through path:
                                                              // undefined behaviour, so
                                                              // damage areas associate
                                                              // and merge wrongly
190: if(tdiffms>500){ ... if(previousItem) previousItem->next = currentItem->next;
         else areaList = NULL;      // drops the ENTIRE remainder of the list
         free(rememberToFree); }
206: currentItem = currentItem->next;   // read AFTER free: use-after-free
```

The list truncation plus the bounding-box merge in `rectsMerge()` is why the log
shows one rect refreshed forever (`DOING fbink_refresh: 350 1351 226 289`).
Regions get swallowed, so large parts of the canvas never receive a proper
grayscale refresh. This is also the laggy menu: a large UI change cannot get a
fast, correct update.

## What the prior art says (sources in docs/)

- **Do not ask a fast waveform to render grey.** A2 and DU are B&W by hardware
  definition; `FORCE_MONOCHROME` is documented 50%-threshold posterization
  (NXP i.MX RM00293; JavaFX Monocle `EPDSettings`). Fake grey with ordered
  dithering in the app instead — 50% checkerboard is pure black and white pixels,
  which every waveform reproduces faithfully, and the eye integrates it back to
  grey. `rmkit/harmony` does exactly this on reMarkable (per-segment dither
  probability at constant width, lighter for pencil).
- **Grey on white needs an intermediate black anyway.** KOReader's guide: a
  white→grey transition must go white→black→grey, because the panel cannot go
  white→grey directly. Another reason not to build the pencil on grey.
- **KOReader explicitly disables the driver's silent waveform promotion on
  Scribe.** Kindle MTK auto-promotes large partial updates to REAGL (~450 ms).
  `fbink_mtk_toggle_auto_reagl()` / `_MTK_ToggleFastMode(true)` is the switch;
  KOReader calls it at init for Scribe. This matters for our menu latency.
- **Waveform cost ladder is the real latency budget** (FBInk's own figures):
  A2 ~120 ms, DU ~260 ms, GC16/REAGL ~450 ms. No amount of fast code beats it;
  you choose the mode deliberately.
- **Closest architecture: `rmkit/harmony`** (reMarkable, C-like, raw framebuffer).
  Its palette is a bottom `HorizontalLayout` of widgets; its brush/style picker is
  a `TextDropdown` that builds an overlay scene of option buttons. It tracks
  damage inside the framebuffer (`FBRect dirty_area` + `dirty` flag, unioned by
  every draw primitive), sends **one** EPDC update per frame for exactly that
  rect, and draws strokes into an in-memory `VirtualFB` layer then composites
  only the dirty rect into the real framebuffer with a per-row `memcpy`.
  **It has no licence file — reimplement, do not copy code.**
- **Nobody uses "tap the active tool again".** Xournal++ uses an always-visible
  chevron menu button beside the tool; Rnote uses a persistent sidebar page;
  Saber opens a second panel next to the active tool button. The hidden gesture
  in our palette is not a known-good pattern — it is the thing to remove.
- **Variable width must be a filled outline polygon, not a stroked line.** X11
  `XDrawLine` cannot vary width along a path. Xournal++ (`StrokeContour`) and
  Rnote (`compose_lines_variable_width`) both build the offset outline and fill
  it. Our per-segment stepping approximates this and is acceptable for e-ink.
- **Store per-point WIDTH, not raw pressure** (Xournal++ `point.z`, reMarkable
  `uint16` width). The style then exists only as the capture-time mapping and the
  renderer stays style-agnostic. Our `.ink` format already stores width.
- **A seeded texture must be reproducible** or it shimmers on every repaint.
  Rnote stores a `u64` seed advanced per segment. We hash absolute coordinates,
  which achieves the same thing.

## Plan

Ordered by impact per unit of risk.

### 1. Pencil: black dither, not grey (small, high confidence)

Change the pencil to draw **pure black** pixels in an ordered/50% pattern rather
than `gray50`. This is 1-bit safe under every waveform, so the pencil appears
while drawing, on every refresh mode, with no flash. Keep the grid-cell stipple
already implemented; change the colour and raise the density.

Grey can be offered later as a *committed-stroke* refinement via a GC16 refresh,
never on the live path.

### 2. Fix the refresh bridge (medium, high impact)

Both symptoms live here.

- Fix `rect1InsideRect2()` to return a correct result on all paths (it currently
  returns garbage).
- Fix `areaListHousekeeping()`: do not null the list head when siblings remain,
  and do not touch `currentItem` after `free()`.
- `RM_PRETTY`: use explicit `WFM_GC16` + `is_flashing=false` instead of
  `WFM_AUTO`, so greys are guaranteed on committed strokes without a flash.
- Trigger the grayscale cleanup **deterministically on stroke end / tool change**
  rather than only on the time heuristic, so committed ink is always correct.
- Bracket any remaining A2 run with a non-flashing GC16 on entry and exit, as
  FBInk's own docs require.

### 3. Kill the menu lag (small, high impact)

- Call `fbink_mtk_toggle_auto_reagl()` / MTK fast mode on init, as KOReader does
  for Scribe, so the driver stops silently promoting partials to REAGL.
- Give tap feedback on touch-down: redraw only the pressed item's rect with a
  fast waveform, then let the action render (KOReader's fast + forceRePaint).
- Merge intersecting damage rects and refresh only the palette rect; never the
  window. Keep the palette geometry stable so repeat opens damage the same area.
- Draw the palette in pure B/W. No gradients, no alpha over the canvas.

### 4. Palette UX: stop hiding the gesture (medium)

Replace "tap the active tool again" with an always-visible affordance, following
every surveyed app: a chevron/second button beside the active tool that opens the
style and width list, plus a live width preview (Rnote renders each preset as a
dab of the actual brush). Consider making PENCIL and HIGHLIGHTER siblings of PEN
rather than styles under it (Saber does this) so only interchangeable pens share
a submenu.

## Things not to do again

- **Do not verify rendering only under Xvfb.** It models the drawing, not the
  display pipeline. Both failed attempts passed a local harness and failed in the
  hand. Run the probe on the device.
- **Do not test only at a convenient base width.** Test THIN and THICK; the
  pencil bug was invisible at MEDIUM and total at THIN.
- **Do not trust a speed ramp that exceeds the real sample distribution.** Use
  the measured p10..p90 (1px..9px) from `workspace-v2/*.ink`.
- **Do not fix the same symptom twice without questioning the layer.** Two fixes
  to `draw_segment()` changed nothing because the bug was never there.
