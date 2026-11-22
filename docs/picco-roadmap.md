# Picco roadmap

What is left before the Picco build is complete, then ideas for after. `CHANGELOG.md` lists what
is done.

## Before the first device test

1. ~~Debug run~~ (2026-11-21): 180/180 host tests pass (`cmake -S test -B build/test`, then
   `cmake --build build/test` and `ctest --test-dir build/test`); `pio check -e picco` finds no
   high or medium defects and none in changed code (its 32 low style notes are all in files
   unchanged since 1.6.0: Ao3Librarian.cpp, Ao3FandomLogo.cpp, the SDK); a clean Picco build has
   no warnings (RAM 28.4%, flash 38.1%); the X4 `default` build also succeeds (flash 90.2%). The X4
   build refuses a project path with a space, so build it from a copy at a space-free path.
2. ~~Before the next flash~~: the Raincloud background was tried in real gray tones, but leaving
   gray needs a full refresh on every change, so it is back to full-strength Bayer dithering
   (owner's choice, 2026-11-21). Leaving USB Drive mode without unplugging was dropped.
3. **Device test (first round done 2026-11-21; fixed since: frontlight and control center, books
   failing to index; Raincloud background, outline and refreshes passed 2026-11-21)**, only when the
   owner says so: Flip Picco and
   its full refresh, the PIN screen ignoring gestures, each Slide Switch mode, gray images and
   sleep images, AO3 folder indexing (the Failed Books page and the failure log), and the AO3
   library Sort & Filter from the DalysLibrary fork (Library Cleanup first, then filters, search,
   Favorite, chapter counts, and memory with a large library),
   and reading progress on the home screen (Lyra, Raincloud, and the cover bar on Raincloud Extended),
   and the shared header on Raincloud, Raincloud Extended, Lyra, Classic and RoundedRaff (battery and
   clock corners, header clock, the tappable back arrow, also with Flip Picco at 180°)),
   and the power-button privacy blank (every sleep screen mode, from the reader with gray text and
   images, and in night mode).

Checked and closed: sleep screen and 4-level images (CrossPoint's versions), AO3 indexing (matches
the device), SecureHttpClient (all differences come from the SDK), the settings loader (same keys
and messages as the device), and the crash record (fixed by dd6581cd).

## After the base is done

- CrossPoint 2.0 sleep features: every image once before repeats (d663dcc6), merged directory
  pools (6251054b), overlay decoupled from sleep mode (51089beb), sleep-screen confirmation
  (6d649b66).
- AvesO3 1.6.5 merge once upstream finishes it, and the AO3 Bookmarks feature.
- After a low-memory skip, restart silently and resume indexing (AvesO3 1.6.5's restart), so
  fewer books are skipped on long runs.
- Bluetooth page turner (CrossPoint fecee610, 0727f9f0). CrossPoint leaves Bluetooth off on the
  Picco, because its USB Drive mode build uses the prebuilt core without the Bluetooth settings.
- Picco emulation for screenshot tests ([eink-qemu](https://github.com/serialx/eink-qemu)).
- Study the Boox `.opub` format.
- Ideas from the stock Boox firmware: reading statistics, home widgets, an image gallery with
  "set as sleep screen", a low-battery shutdown screen.
