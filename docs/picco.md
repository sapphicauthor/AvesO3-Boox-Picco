# AvesO3 on the Boox Picco

This branch builds AvesO3 for the **Boox Picco** (ESP32-S3, 480x800 e-ink with touch, frontlight
and a hardware slide switch) and adds a few Picco features. `CHANGELOG.md` lists every change
from AvesO3 1.6.0; `docs/picco-roadmap.md` lists what is still open.

## Building

```
pio run -e picco
```

`[env:picco]` extends the base environment with the Picco board, USB mass storage and
`LOG_LEVEL=2`. Online updates are off; build with `-DAVESO3_ENABLE_OTA` to turn them back on.

freeink-sdk follows the commit CrossPoint's `develop` branch uses (currently `634a3f8`).

## Partitions: read before flashing

The Picco keeps Boox's own recovery firmware in the `safeboot` partition at `0x20000`; it can
reinstall the stock firmware from `/update/firmware.bin` on the SD card. AvesO3 lives at
`0xE0000`. Always use `partitions_picco.csv`, never the default `partitions.csv`, and write
only the app image:

| Name     | Type     | Offset     | Size       |
|----------|----------|------------|------------|
| nvs      | data     | `0x9000`   | `0xC000`   |
| otadata  | data     | `0x15000`  | `0x2000`   |
| safeboot | app      | `0x20000`  | `0xC0000`  |
| ota_0    | app      | `0xE0000`  | `0xE70000` |
| spiffs   | data     | `0xF50000` | `0x80000`  |
| coredump | data     | `0xFD0000` | `0x10000`  |

`pio run -e picco -t upload` writes only the app at `0xE0000`: `scripts/app_only_upload.py`
(from CrossPoint) drops the bootloader, partition table and `boot_app0.bin` that a default
ESP32 upload adds, and stops with an error if any of them is left. `boot_app0.bin` would
otherwise land at `0xE000`, inside the Picco's NVS.

## Picco features

**Parental PIN lock.** Settings → Controls → Set / Change PIN, then turn on PIN Lock. Boot and
wake then ask for the 4-digit PIN before continuing to the book or home screen. The 12-key PIN pad has extra-tall keys. While the unlock screen is up, global
gestures and shortcuts (home swipe, control center, power-button shortcuts) are ignored.

**Slide Switch.** Settings → Controls → Slide Switch binds the hardware switch to the frontlight,
night mode or the reader touch controls. The switch then owns that function: its setting, reader
menu entry and control-center toggle are hidden or inactive, as in CrossPoint.

**Privacy on sleep.** Putting the Picco to sleep with the power button first wipes the screen
white with one fast refresh, so the page is gone straight away; then the sleep screen loads as
usual. Quick Resume and Transparent sleep screens keep the current screen, so they are not wiped.
Auto-sleep after the timeout is unchanged.

**Page Touch.** The control-center tile "Page Touch: On/Off" switches only the taps and swipes on the
book page (Settings → Controls → Touch Reader Controls), for reading with a palm on the glass. The
rest of the screen keeps responding, so swiping down from the top and tapping the tile again turns it
back on.

**Flip Picco.** The third control-center tile ("Flip Picco: 0°" / "180°") turns the whole
interface upside down, which gives the case's wrist strap slack. Readers start from their own
reading orientation. Switching does a full refresh so no ghost image is left.

**Raincloud and Raincloud Extended themes.** Lyra-based themes with a picture behind the home
screen: the current book's cover, otherwise a random BMP from `/.sleep` (or `/sleep`). Every
element gets a 3 px white outline so it stays readable, including the selected book's text on
its gray highlight; covers and boxes stay solid. Raincloud
Extended uses the Lyra 3 Covers layout. The background is shown at full strength, dithered
from the picture's full luminance (black and white only, so the home screen never needs a gray
refresh), follows the
sleep screen's Cover Mode and Cover Filter settings, and is cached in PSRAM.

**Reading progress on the home screen.** Lyra and Raincloud show how far into the current book you
are, as a percentage and a thin bar under the author. Raincloud Extended shows a thin bar along the
bottom of the first cover. Raincloud is built on Lyra's code but kept a separate theme: it has its own
tile override, so later Lyra changes reach it only on purpose.

**Text layout.** Line spacing is tighter than in AvesO3 (Tight 0.8, Normal 0.9, Wide 1.0, Extra Wide
1.1 of the font's line height), and Settings → Text → Screen Margin goes from 0 to 20 px in 2 px steps.

**Reading gestures.** While reading an EPUB, a swipe in from the left edge turns back a page and a
swipe in from the right edge turns to the next page; the swipe up from the bottom edge (Home) and the physical Back button leave the book as before.

**Header.** Every theme shares one header: battery top right, clock top left (Settings → Clock →
Show in Header, once the clock has the time), and a back arrow you can tap on every screen except
Home. Raincloud uses Lyra's header measurements, so it follows Lyra here.

**Sleep screen.** Pictures come from `/.sleep` and `/sleep` together, each shown once in random order
before repeats; Raincloud's home background uses the same two folders. "Cover + Transparent" draws
the transparent overlay (`/sleep-overlay.png`, `/sleep-overlay.bmp` or the `.sleep-overlay` folder)
over the current book's cover. JPEG, PNG and BMP pictures open from the file browser, and setting
one as the sleep image asks first.

**Home menu.** One page; OPDS Browser is a regular entry after Settings.

**AO3 indexing.** A folder run skips books it cannot index and keeps going. Afterwards the
Failed Books page lists up to 10 of them, and `/.crosspoint/ao3_index_failures.log` on the SD card
keeps all of them, one line per book with the step that failed (zip, book cache, contents list,
memory, SD card, AO3 details); it moves to `.log.1` once it passes 32 KB. Hidden files (such as the
`._` files macOS copies alongside every file) and the `/read` archive are not indexed, and a
damaged book cache is rebuilt, as when the book is opened.

**AO3 library Sort & Filter** (from the DalysLibrary fork). Besides Fandom, Relationship, Sort and
Order, the sheet has Title starts / Author starts (a letter grid; `&` is any first character that
is not a letter or digit), Status (ticks for reading status, WIP, Completed in AO3 and Favorite,
combined with All or Any) and Text or tag (title, author, series, fandom, relationships and the
first four short tags; a space means either word, quotes keep a phrase, `-` excludes). Nothing
changes until Apply. A long press on a book opens its menu, whose first row toggles the Favorite
heart. Library rows show `Chapters: 12/20`, or `12/?` until the book is indexed again. The library
holds up to 2500 books. Run Library Cleanup once after installing, so books indexed earlier get
their status bits and existing hearts are copied into the progress files. The search text lives
in `/.crosspoint/ao3_tag_text.tsv`; the first text search builds it, which can take a few minutes.

**XTC.** Pages are fitted to the screen, and pages with zero dimensions are rejected.

## Notes for developers

- Flip Picco: `applyActivityOrientation()` in `ActivityManager.cpp` sets the orientation when an
  activity starts and before every render. Touch and button directions follow the live renderer
  orientation (`MappedInputManager`), so nothing else needs to know about the flip.
- The slide switch's ownership checks live in `src/util/SlideSwitch.h`.
- Raincloud draws the home UI on white, then `finishHomeScreen()` lays the background back in
  where it stays clear of the UI. It needs 48 KB + 144 KB of PSRAM; without it the background is
  drawn under the UI. The background folder is scanned once and rescanned when
  `SdContentEpoch` moves (USB plugged or unplugged, USB Drive or web transfer finished).
- The home screen renders in BW only; images use CrossPoint's absolute grayscale where the panel
  supports it.
