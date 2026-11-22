# Picco roadmap

What is open, then ideas for later. `CHANGELOG.md` lists what is done.

## Status (2026-11-21)

The Picco build is complete and in daily use: built and flashed to the owner's Picco four times on
2026-11-21 (app slot only), and the owner's device test passed.

Checks on every build: a clean `pio run -e picco` with no warnings (RAM 28.5%, flash 38.4%), 180/180
host tests (`cmake -S test -B build/test`, `cmake --build build/test`, `ctest --test-dir build/test`),
and `pio check -e picco` with no high or medium defects. The X4 `default` build also succeeds; it
refuses a project path with a space, so build it from a copy at a space-free path.

Checked and closed earlier: sleep screen and 4-level images (CrossPoint's versions), AO3 indexing
(matches the original device firmware), SecureHttpClient (all differences come from the SDK), the
settings loader (same keys and messages), and the crash record (fixed by dd6581cd).

## Worth watching in daily use

- Memory with a very large AO3 library (the 2500-book limit; run Library Cleanup once first).
- Tight line spacing (0.8): tall and low letters may touch on some fonts.
- Long chapters: page turns while the rest of the chapter is laid out in the background.
- Raincloud Extended's progress bar on the first cover (a new design, not from CrossPoint).
- The keyboard's 150 ms delay before a held key lights up.

## Next

- Page-turn timings: read the reader's own timing log (`Page render ...` lines, book names masked)
  on the Picco and speed up the slowest step.
- A final sweep of AvesO3, CrossPoint and freeink-sdk for useful updates.
- Release builds with less serial logging, if wanted (`[env:picco]` logs at `LOG_LEVEL=2`).

## Later

- AvesO3 1.6.5 merge once upstream finishes it, and the AO3 Bookmarks feature.
- After a low-memory skip, restart silently and resume indexing (AvesO3 1.6.5's restart), so
  fewer books are skipped on long runs.
- TrueType fonts (CrossPoint aaf68379 and its fixes; freeink-sdk already has FreeInkFont), then
  plugins, highlight/clip text, Indic scripts and OPDS 2.0.
- CrossPoint's medium ports (paragraph indent, GIF images, line spacing in percent) if AvesO3 adopts
  them, and the Image Grayscale on/off option.
- Bluetooth page turner (CrossPoint fecee610, 0727f9f0). CrossPoint leaves Bluetooth off on the
  Picco, because its USB Drive mode build uses the prebuilt core without the Bluetooth settings.
- Ideas from the stock Boox firmware: reading statistics, home widgets, an image gallery, a
  low-battery shutdown screen.
- Set aside for now: Picco emulation for screenshot tests, the Boox `.opub` format.
