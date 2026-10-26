# Publishing Nadir to the Pebble appstore

Watchfaces are submitted through the Rebble developer portal,
[dev-portal.rebble.io](https://dev-portal.rebble.io). Apps published there
also appear in Core Devices' Pebble Appstore in the Pebble phone app. The
portal puts a listing live straight away; there is no separate review step.

## Before submitting

- [ ] Build a release `.pbw` with the released SDK (`pebble build`), from a
      clean tree with `"version"` in `package.json` set (1.0.0 for the first
      release).
- [ ] Install that exact `.pbw` on a watch and check that it starts and stays
      running, that the settings page opens and saves, and that weather and the
      ISS appear.
- [ ] Keep the UUID in `package.json` (`16183a4f-c48d-4702-8a48-1211031a8115`).
      It identifies Nadir in the store; every later release must use the same
      one.
- [x] Make the source repository public first, if you want the listing to
      link to it: https://github.com/geoff-ts/nadir

## What the portal asks for

| Field | For Nadir |
|---|---|
| Account | A Rebble account, with a developer name to publish under |
| App type | Watchface |
| Name | Nadir |
| Category | Not asked for watchfaces (watch apps only) |
| Description | See below |
| Screenshots | At least one per supported platform. Nadir supports only Emery: `docs/screenshots/emery-*.png`, 200 x 228, unframed |
| Banner | Optional for watchfaces: `docs/banner.png`, 720 x 320, drawn by `tools/make-banner.py` |
| Icons | Watch apps only; not needed |
| Website / source URL | Optional: the public repository |
| Release notes | Required for the first upload; see below |
| App binary | The `.pbw` |

Screenshots in the listing must be plain watch-screen captures. Framed images
(a screenshot inside a watch drawing) are only for banners. Take fresh
captures with `pebble screenshot --emulator emery` if the face changes.

## Listing text

**Name:** Nadir

**Description:**

> Where is everything in the sky right now? Nadir wraps the Sun, Moon,
> planets and space stations around the time, placed where they are in your
> sky.
>
> The ring is the ecliptic, turned so the top of the screen is overhead and
> the middle of each side is the horizon: east on the left, west on the right.
> Bodies that are up get a solid tab; bodies below the horizon get an outline.
> The zodiac constellations turn with them through the day. The Moon shows
> tonight's phase.
>
> The ISS, and optionally Tiangong and Hubble, are placed by how high they
> are, and move every few seconds while one is passing overhead.
>
> Flick your wrist to roll the sky a few hours ahead and see what is coming
> up.
>
> Choose two readouts from battery, weather, heart rate and steps, in Celsius
> or Fahrenheit. All the astronomy runs on the watch, so it keeps working
> away from your phone.
>
> Uses your location for the sky and the weather. Your rounded location is
> sent to Open-Meteo for the weather and nowhere else; nothing is collected.
> Weather data by Open-Meteo.com (CC BY 4.0). Satellite orbits from
> CelesTrak. Fonts: Bruno Ace and Noto Sans Symbols (SIL OFL).
>
> Open source: https://github.com/geoff-ts/nadir

**Release notes for 1.0.0:** First release.

## Later releases

1. Bump `"version"` in `package.json` (Pebble shows it as major.minor).
2. Build, and test the `.pbw` on a watch.
3. In the portal, open Nadir and publish a new release with the `.pbw` and
   its release notes. Screenshot changes take effect immediately.

## Other platforms

Nadir targets only `emery` (Pebble Time 2). The layout is drawn for 200 x 228;
other watches would need their own layout constants and their own
screenshots in the listing.
