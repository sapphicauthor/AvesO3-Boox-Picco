# Picco roadmap

What is left before the Picco build is complete, then ideas for after. `CHANGELOG.md` lists what
is done.

## Before the first device test

1. **AO3 indexing guard.** Skip-and-continue indexing is in and matches the device; check where
   the skipped titles are listed. `Ao3IndexActivity::loadSettings` still differs from the device.
2. **SecureHttpClient.** Three functions differ from the device firmware; find out whether that is
   the SDK version or a local edit.
3. **Settings loader.** `CrossPointSettings::fromJson` still differs from the device; identify why.
4. **Crash record.** Review the coredump partition from the device (book titles in it are private
   and ignored) and fix what the AvesO3 repositories have not.
5. **Debug run.** Host tests in `test/` (needs cmake), static checks, a clean build.
6. **Device test**, only when the owner says so: Raincloud background and outline, Flip Picco and
   its full refresh, the PIN screen ignoring gestures, each Slide Switch mode, gray images and
   sleep images.

## After the base is done

- CrossPoint 2.0 sleep features: every image once before repeats (d663dcc6), merged directory
  pools (6251054b), overlay decoupled from sleep mode (51089beb), sleep-screen confirmation
  (6d649b66).
- AvesO3 1.6.5 merge once upstream finishes it, and the AO3 Bookmarks feature.
- Picco emulation for screenshot tests ([eink-qemu](https://github.com/serialx/eink-qemu)).
- Study the Boox `.opub` format.
- Ideas from the stock Boox firmware: reading statistics, home widgets, an image gallery with
  "set as sleep screen", a low-battery shutdown screen.
