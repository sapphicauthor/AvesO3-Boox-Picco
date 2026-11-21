# Changelog

Changes on this branch (`picco-recovery`) on top of [AvesO3](https://github.com/SiliconAves/AvesO3)
1.6.0, for the Boox Picco (see `docs/picco.md`). Entries are grouped the way they would be offered
as pull requests; documentation commits like this file are not part of them.

## Unreleased (2026-11-20)

Not yet tested on the device. Everything below builds.

### Picco support
- Build environment `[env:picco]`, `partitions_picco.csv` (keeps Boox's `safeboot` recovery
  firmware), board tag "picco", USB name AvesO3_Picco. (6bd774c6)
- Online updates are off unless built with `AVESO3_ENABLE_OTA`. (f45e0b25)
- Picco book covers are cached under their own filenames. (7d74f435)
- SD-card firmware update refuses to run when the only app slot is the running one (the Picco's
  layout, also CrossPoint's), instead of erasing the running app. (9808f4e7)
- The frontlight is probed before checking that it exists, so the Picco's light, its slide switch
  and the control-center swipe work again (from CrossPoint #4234). (7d4ec7f9)

### Privacy and security
- Saved Wi-Fi networks (names and passwords) and the PIN are encrypted on the SD card with
  AES-256-GCM under a per-device key kept in internal flash; old files migrate. (30dcff35, 2708d895)
- Network names, a password length and the access point address no longer go to the serial log;
  a readable Wi-Fi config left by older firmware is cleared from NVS. (76fd41dd, e09adede)
- freeink-sdk follows CrossPoint `develop`: now `634a3f8` (was `e88ebd5`), bringing the newer
  Picco display work (SSD2677 light-gray timing, one-pass anti-aliasing, darker AA pixels, gray
  artifact fixes, lighter transfers, B/W fallback). (f15c111b, 28b79e29)

### Picco controls
- Parental PIN lock, with Set/Change PIN and an unlock screen at boot and wake. (20d0f5f7)
  - Settings are re-saved when the stored lock value is invalid. (fc2bf6a8)
  - The home gesture and the control-center swipe no longer leave the unlock screen. (595557c1)
- Slide Switch setting: the hardware switch drives the frontlight, night mode or the touch
  reader controls. (37d13e1f)
  - Following CrossPoint #4234, the UI no longer offers a second toggle for the function the
    switch owns. (23dd6b25)
  - The switch position is applied before the first boot screen. (bae74c67)
- Flip Picco: a control-center tile turns the whole UI 180 degrees, giving the case's wrist
  strap slack, with a full refresh so no ghost is left. (66b8411a)

### Home screen and themes
- The home menu is a single page; OPDS Browser is a regular entry. (fcc38f68)
- Raincloud theme: Lyra with a background behind the home screen (the current book's cover, or a
  random image from the sleep folder). (ebcd2696)
- Raincloud Extended theme: Raincloud with the Lyra 3 Covers tiles. (b0d062ee)
- The Raincloud background is drawn like the sleep screen: same Cover Mode (Fit/Crop) and Cover
  Filter, no re-dithering, and the sleep screen's gray pass once per visit when the home screen
  is idle for 0.6 s; menus and text stay black and white. (787a8eed, 1b357e31, 4e5f5ed7, bdec9e87)
- The Raincloud home screen skips refreshes that would show the same frame again. (bdec9e87)
- Every home-screen element gets a 3 px white outline; covers and boxes stay solid. (8d9532fd, 6b59c8c5)
- Faster Raincloud drawing: precomputed shading (de6f8d00), source columns worked out once
  (df6c4b8d), one-pass random pick (a5572ccf), the image folder scanned once per boot or SD/USB
  change (7bd7cd50), a bit-parallel outline (246ebd67), and CrossPoint's gray-plane helper
  (bf603b46).
- Lyra caches the home cover after drawing a real cover instead of re-reading it from the SD card
  on every redraw (same fix as AvesO3's 1.6.5 merge). (a6ce0a3e)

### Reading
- XTC: pages with zero dimensions are rejected. (da49384b)
- XTC: pages are fitted to the screen. (d5c8c8e3)
- EPUB: damaged cache files are detected and rebuilt instead of crashing; the AO3 indexer skips
  books with an invalid cache. (142b02e9)
- EPUB: building a book's spine/TOC cache stops cleanly when memory runs low instead of aborting
  (the crash in the device's core dump, during AO3 indexing). (d88550a2)
- AO3 folder indexing logs every book it could not index, with the reason, to
  `/.crosspoint/ao3_index_failures.log`, in addition to the Failed Books page. (3a980914, 1b3e6915)
- EPUBs whose zip end record is more than 1 KB from the end (a long comment or trailing bytes)
  open now; the search covers the 64 KB the zip format allows. (a6b73dbb)

### Images and sleep screen (from CrossPoint)
- Absolute grayscale for supported screens, CrossPoint e5dcc64f. (4aa4b3cf)
- Sleep images skip the B/W pass, and white is transparent on sleep covers, CrossPoint 92ef5707.
  (ea85f4d7)
- Full-tone images in the EPUB reader on the Picco, CrossPoint 6e7caa22. (ac4024cc)
- The custom 4-level render modes (54fd01fa) were replaced by CrossPoint's version above.

### Text
- Picco UI wording (sentence-style strings). (740acb20)
