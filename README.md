# Nadir: a Pebble Time 2 watchface for the sky

Shows where the Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn and the ISS
are, alongside the usual time, date, battery and weather. Target: Pebble Time
2 (`emery`, 200x228).

## How to read it

- **Outer ring** is the ecliptic bent into a loop, with a coloured tab per
  body. It is turned so the part of the ecliptic that is rising sits at the
  middle of the left edge and the part that is setting at the middle of the
  right edge. The upper half of the ring is above your horizon, the lower half below it;
  two white ticks at the edges mark the horizon (it is the halfway line).
  Zenith is the top; east is left, west is right (you are facing south).
- **Inner band** carries the twelve zodiac constellations on the same
  mapping, so the whole thing turns together once a day.
- **The ISS** is not on the ecliptic, so it is placed by elevation instead:
  straight up is the top of the ring, the horizon is the middle of either
  side, and it goes to the left or right of the ring by whether it is in the
  east or west of your sky. Its cyan tab only appears once the phone has sent
  orbital elements.
- Tabs that would land on top of each other are nudged apart (at least 13
  degrees), so a conjunction reads as two bodies side by side rather than one.

## Where the numbers come from

All sky maths runs on the watch (`src/c/astro.c`), so it keeps working with
the phone away. The phone (`src/pkjs/index.js`) only supplies things the
watch cannot know:

| Data | Source | Refreshed |
|---|---|---|
| Location | phone geolocation, falls back to Sydney | every 30 min |
| ISS orbital elements (TLE) | CelesTrak, NORAD 25544 | cached 6 h on the phone; the watch ignores them after 14 days |
| Weather | Open-Meteo, no key | every 30 min |

Planets use JPL's "Approximate Positions of the Planets" (Table 1), the Moon a
short Meeus series, and the ISS a Kepler orbit with the J2 drift of its node
and perigee. Checked against `astronomy-engine` and `satellite.js` (SGP4):
Sun, planets and ascendant within 0.1 degree, Moon 0.03, ISS elevation and
azimuth within 0.4 degree.

The Moon's tab is a small disc lit as the Moon is tonight; the lit side flips
in the southern hemisphere, as it does in the sky.

Constellation lines come from d3-celestial (BSD-3). Regenerate the header with
`node tools/gen-sky-data.mjs constellations.lines.json`.

## Build and run

The toolchain lives in the `pebble` WSL distro (Pebble SDK 4.33.1):

```bash
wsl -d pebble -- bash /mnt/c/ThinkSwift/nadir/tools/wsl-build.sh
wsl -d pebble -- bash /mnt/c/ThinkSwift/nadir/tools/wsl-build.sh install-emulator
```

That leaves `nadir.pbw` beside this file.

## Putting it on the watch through the phone

The phone is the Pebble (Core) app's gateway, so the install goes through it:

```bash
node tools/phone-relay.mjs                     # leave running; phone unlocked, USB adb
wsl -d pebble -- bash /mnt/c/ThinkSwift/nadir/tools/wsl-build.sh install-phone 172.24.64.1:9124
```

`phone-relay.mjs` wakes the Core app's developer connection (an adb
broadcast), forwards its port 9000 to the PC and relays that to an address WSL
can reach. Notes on how the pieces fit:

- Nadir's phone side is the JavaScript in `src/pkjs/index.js`, run by the Core
  app itself whenever the face is on the watch. It needs no companion app.
  (The Penny watchapp in ThinkSwift's stack repo, `tools/llm-lab`, talks to
  its own Android app through PebbleKit2 instead; that is the fallback if the
  Core app's JS ever proves unreliable.)
- Message keys are declared in `package.json` (`messageKeys`) and come out as
  `MESSAGE_KEY_*` in C; the JS uses the same names as dictionary keys.
- The watch sends `REQUEST` every 30 minutes, which makes the phone refresh;
  the phone also refreshes when the face starts.
- The phone's Pebble app logs Nadir's starts and stops (`adb logcat | grep
  AppRunState`, Nadir is `16183a4f`). A start followed by a stop within
  milliseconds is a crash at launch; the watch then shows "not responding".

## Traps

- **No libm.** The toolchain's maths library reaches its constant tables
  through absolute addresses in its code, and the Pebble loader only relocates
  `.data` (`inject_metadata.py` reads `.rel.data`). On the real watch those
  reads land outside the app, so `fmod` killed the face within milliseconds of
  launch ("not responding"); in the emulator they read junk, so `atan2` gave
  wrong angles. `astro.c` carries its own `astro_sin`, `astro_cos`,
  `astro_sqrt`, `astro_remainder` and `atan2_rad`, and no source includes
  `<math.h>`, so a libm call fails the build. To check a binary: `arm-none-eabi-nm
  build/emery/pebble-app.elf | grep -E "sin|cos|sqrt|__ieee754"` should find
  nothing. `strtod` is avoided too: it drags file I/O stubs into the link.
- The emulator hides crashes like that one. Test on the watch: install, then
  `adb logcat | grep AppRunState` should show Nadir's start with no stop after.
- The emulator washes colours out compared with the real display; judge
  colours on the watch.
- The emulator shows host local time, so a screenshot's clock is your clock.

## Fonts

Chakra Petch (and a squeezed copy of its digits, made by
`tools/make-digits-font.py`) and Noto Sans Symbols, both SIL OFL 1.1. See
`resources/fonts/LICENSES.md`. The Sun's symbol is drawn in code because Noto
Sans Symbols has no glyph for it.
