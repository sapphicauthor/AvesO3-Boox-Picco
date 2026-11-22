# Changelog

Changes in AvesO3 for Boox Picco (branch `boox-picco`) on top of [AvesO3](https://github.com/SiliconAves/AvesO3)
1.6.0, for the Boox Picco (see `docs/picco.md`). Entries are grouped the way they would be offered
as pull requests; documentation commits like this file are not part of them.

## AvesO3 Picco, first release (2026-11-21)

Firmware version `1.6.0-picco`, released as `AvesO3_Picco.bin` (install: `docs/picco.md`). Everything
below builds, passes the host tests, and was tested on the owner's Picco on 2026-11-21. Commits ported
from CrossPoint, AvesO3 or the DalysLibrary fork keep their author or a `Co-Authored-By` credit.

### Picco support
- Build environment `[env:picco]`, `partitions_picco.csv` (keeps Boox's `safeboot` recovery
  firmware), board tag "picco", USB name AvesO3_Picco. (6bd774c6)
- `pio run -e picco -t upload` writes only the app image, keeping Boox's bootloader, partition
  table and NVS; it stops if any extra image is left (CrossPoint b6b7a6c). (2ac720c8)
- Online updates are off unless built with `AVESO3_ENABLE_OTA`. (f45e0b25)
- Picco book covers are cached under their own filenames. (7d74f435)
- SD-card firmware update refuses to run when the only app slot is the running one (the Picco's
  layout, also CrossPoint's), instead of erasing the running app. (55692bac)
- The frontlight is probed before checking that it exists, so the Picco's light, its slide switch
  and the control-center swipe work again (from CrossPoint #4234). (3ed6f713)

### Privacy and security
- Saved Wi-Fi networks (names and passwords) and the PIN are encrypted on the SD card with
  AES-256-GCM under a per-device key kept in internal flash; old files migrate. (2b5ec0b3, 3d342b9c)
- Network names, a password length and the access point address no longer go to the serial log;
  a readable Wi-Fi config left by older firmware is cleared from NVS. (7c4467f6, 98a60311)
- freeink-sdk follows CrossPoint `develop`: now `634a3f8` (was `e88ebd5`), bringing the newer
  Picco display work (SSD2677 light-gray timing, one-pass anti-aliasing, darker AA pixels, gray
  artifact fixes, lighter transfers, B/W fallback). (f15c111b, b15f3a24)

### Picco controls
- Faster typing on the on-screen keyboard: a tapped key no longer lights up first, so each letter
  costs one screen refresh instead of two; a held key still lights up before its long press. (40fe1055)
- Parental PIN lock, with Set/Change PIN and an unlock screen at boot and wake. (20d0f5f7)
  - Settings are re-saved when the stored lock value is invalid. (fc2bf6a8)
  - The home gesture and the control-center swipe no longer leave the unlock screen. (595557c1)
  - The PIN pad's 12 keys are twice as tall. (c363401a)
- Slide Switch setting: the hardware switch drives the frontlight, night mode or the touch
  reader controls. (37d13e1f)
  - Following CrossPoint #4234, the UI no longer offers a second toggle for the function the
    switch owns. (6c10e9b8)
  - The switch position is applied before the first boot screen. (fdf7d6a2)
- Privacy on sleep: holding the power button to sleep wipes the page off the screen at once, before
  the sleep screen loads (not in Quick Resume or Transparent, which reuse the screen). (b12ba0fa)
- The control-center touch tile reads "Page Touch: On/Off": it only switches taps and swipes on the
  book page; with it off, the reader's edge swipes do nothing either. (d246991e, 4b7657ea)
- Flip Picco: a control-center tile turns the whole UI 180 degrees, giving the case's wrist
  strap slack, with a full refresh so no ghost is left. (66b8411a)

### Home screen and themes
- The home menu is a single page; OPDS Browser is a regular entry. (fcc38f68)
- Raincloud theme: Lyra with a background behind the home screen (the current book's cover, or a
  random image from the sleep folder). (ebcd2696)
- Raincloud Extended theme: Raincloud with the Lyra 3 Covers tiles. (b0d062ee)
- The Raincloud background is drawn at full strength, black and white only, dithered from the
  picture's full luminance like CrossPoint's cover thumbnails, and follows the sleep screen's Cover
  Mode (Fit/Crop) and Cover Filter. (5ad0aba9, c0c5e005, d5e8b36c, f0d50dc8)
- The Raincloud home screen skips refreshes that would show the same frame again. (76cff77f)
- Every home-screen element gets a 3 px white outline, including the selected book's text on its
  gray highlight; covers and boxes stay solid. (0c28a1f1, 4a6629f4, 9f2e9268)
- Faster Raincloud drawing: precomputed shading (de6f8d00), source columns worked out once
  (df6c4b8d), one-pass random pick (a5572ccf), the image folder scanned once per boot or SD/USB
  change (7bd7cd50), a bit-parallel outline (7da663a1), and CrossPoint's gray-plane helper
  (0a85952c).
- Lyra caches the home cover after drawing a real cover instead of re-reading it from the SD card
  on every redraw (same fix as AvesO3's 1.6.5 merge). (c9574d93)

### Reading
- Tighter line spacing (Tight 0.8, Normal 0.9, Wide 1.0, Extra Wide 1.1) and a finer screen margin
  (0 to 20 px in 2 px steps, default 0). (bacf776f)
- While reading, a swipe in from the left edge turns back a page instead of leaving the book, and a
  swipe in from the right edge turns to the next page; a slow edge swipe is never taken as a long
  press (chapter skip). (af7096bc, 2ea733d7)
- The open chapter is laid out to the end while the reader is idle (only in frames with no button or
  page turn pending), and its page table and link list grow in small pieces, so a very long chapter
  no longer needs one large memory block; a chapter whose background build fails is then built only
  as you read, CrossPoint f8936ba6 (without its Bluetooth parts). (4a5be270)
- XTC: pages with zero dimensions are rejected. (da49384b)
- XTC: pages are fitted to the screen. (d5c8c8e3)
- EPUB: damaged cache files are detected and rebuilt instead of crashing, by the AO3 indexer too.
  (142b02e9, 4e35ad39)
- EPUB: building a book's spine/TOC cache stops cleanly when memory runs low instead of aborting
  (the crash in the device's core dump, during AO3 indexing). (dd6581cd)
- AO3 folder indexing logs every book it could not index, with the reason, to
  `/.crosspoint/ao3_index_failures.log`, in addition to the Failed Books page. The reason names the
  step that failed (zip, book cache, contents list, memory, SD card, AO3 details). (bb804ddb,
  d3a96e0d, 6f2943d7)
- AO3 folder indexing skips hidden files, such as the "._" companions macOS writes next to every
  file it copies, which it used to count and fail as books (3c874e3b), and the `/read` archive,
  whose books it used to find again on every run (107af170).
- EPUBs whose zip end record is more than 1 KB from the end (a long comment or trailing bytes)
  open now; the search covers the 64 KB the zip format allows. (b02544c3)

### AO3 library (from the DalysLibrary fork)
- Sort & Filter: Title starts / Author starts letter grids, a Status tick list (All/Any, Favorite,
  reading statuses, WIP, Completed in AO3) and a Text or tag search with quotes and `-` exclusions;
  a Favorite heart in the book menu; library rows show `Chapters: 12/20`; the index holds up to
  2500 books on the Picco (1000 on boards without PSRAM). Ported from DalysLibrary/aveso3-test
  a4a5fd1e. (1107b9ab)
- The `SilentRestart.h` include uses the file's real capitalization, AvesO3 c42c118e. (36fb56da)

### Images and sleep screen (from CrossPoint)
- Setting a picture as the sleep screen asks for confirmation first, CrossPoint 6d649b66. (9ee441d5)
- Sleep images come from `/.sleep` and `/sleep` together, CrossPoint 6251054b (4226fd3f); the Raincloud
  background uses both folders too (9a7e1492).
- Every sleep image is shown once, in random order, before any repeats, CrossPoint d663dcc6. (79d229fe)
- JPEG pictures open in the file browser's picture viewer and can be set as the sleep image,
  CrossPoint 15503ac3. (b235e159)
- New sleep screen "Cover + Transparent": the transparent overlay drawn over the current book's
  cover, CrossPoint 51089beb. (43e0b930)
- Absolute grayscale for supported screens, CrossPoint e5dcc64f. (95e0b288)
- Sleep images skip the B/W pass, and white is transparent on sleep covers, CrossPoint 92ef5707.
  (1cc7b5e1)
- Full-tone images in the EPUB reader on the Picco, CrossPoint 6e7caa22. (cc201c49)
- The custom 4-level render modes (54fd01fa) were replaced by CrossPoint's version above.

### Lyra theme (from CrossPoint)
- Lyra's continue-reading tile shows the reading percentage and a progress bar, CrossPoint 08d9d68.
  Raincloud shows the same on its tile, and Raincloud Extended a thin bar along the bottom of the
  first cover. (32ac94ed, 044d13f9)

### Header and clock (from CrossPoint, as in AvesO3's 1.6.5 merge)
- Clock settings use a time zone with daylight saving instead of a UTC offset, and the clock can be
  shown in screen headers (Clock → Show in Header), CrossPoint 581c6374. (17f6b68e)
- One shared header for every theme, with a tappable back arrow on touch screens like the Picco
  (not on Home or the crash screen), CrossPoint 720b995a; the header respects the screen's content
  insets and corners, CrossPoint 01f09e0e. Lyra's header (and so Raincloud's) now starts 5 px lower,
  with a 20 px status strip. (adcd1ad2, 83a2a1e8)
- Clock and battery sit in the same top corners on every built-in theme (battery top right, clock
  top left), CrossPoint 8d758438 and ca1a8c1e. (670e23f2, a6592685)

### Other fixes from CrossPoint
- Finding your place in a chapter reads the page positions 32 at a time instead of one by one,
  CrossPoint 1a5c8cd1. (4295162e)
- Time a screen spends blocked (a long download or index build) no longer counts toward auto-sleep,
  CrossPoint 209cb53. (9865be4b)
- The web rename dialog selects only the name, so the extension is kept, CrossPoint bb7b653.
  (f17591e8)

### Text
- Picco UI wording (sentence-style strings). (740acb20)
