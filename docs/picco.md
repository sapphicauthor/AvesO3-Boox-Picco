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

## Picco features

**Parental PIN lock.** Settings → Controls → Set / Change PIN, then turn on PIN Lock. Boot and
wake then ask for the 4-digit PIN before continuing to the book or home screen. While the unlock screen is up, global
gestures and shortcuts (home swipe, control center, power-button shortcuts) are ignored.

**Slide Switch.** Settings → Controls → Slide Switch binds the hardware switch to the frontlight,
night mode or the reader touch controls. The switch then owns that function: its setting, reader
menu entry and control-center toggle are hidden or inactive, as in CrossPoint.

**Flip Picco.** The third control-center tile ("Flip Picco: 0°" / "180°") turns the whole
interface upside down, which gives the case's wrist strap slack. Readers start from their own
reading orientation. Switching does a full refresh so no ghost image is left.

**Raincloud and Raincloud Extended themes.** Lyra-based themes with a picture behind the home
screen: the current book's cover, otherwise a random BMP from `/.sleep` (or `/sleep`). Every
element gets a 3 px white outline so it stays readable; covers and boxes stay solid. Raincloud
Extended uses the Lyra 3 Covers layout. The background is shown at full strength in four
dithered tones (black and white only, so the home screen never needs a gray refresh), follows the
sleep screen's Cover Mode and Cover Filter settings, and is cached in PSRAM.

**Home menu.** One page; OPDS Browser is a regular entry after Settings.

**AO3 indexing.** A folder run skips books it cannot index (too little memory, or a damaged
book) and keeps going. Afterwards the Failed Books page lists up to 10 of them, and
`/.crosspoint/ao3_index_failures.log` on the SD card keeps all of them, one line per book
(moved to `.log.1` once it passes 32 KB).

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
