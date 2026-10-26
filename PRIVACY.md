# Privacy

Nadir collects nothing about you and has no server of its own. No analytics,
no accounts, no tracking.

## What leaves your phone

To place the sky correctly, the part of Nadir that runs inside the Pebble
phone app (`src/pkjs/index.js`) makes three kinds of request, when the face
starts and then about every 30 minutes:

| Request | Sent to | What it carries |
|---|---|---|
| Weather | [Open-Meteo](https://open-meteo.com) | your latitude and longitude, rounded to 3 decimal places (about 100 m) |
| Satellite orbits | [CelesTrak](https://celestrak.org) | the catalogue numbers of the ISS, Tiangong and Hubble; nothing about you |
| Location | your phone's own location service | nothing leaves the phone |

Like any web request, these reach the services from your phone's IP address.
Their own privacy policies apply to what they log.

## What stays on the watch and phone

- Your latitude and longitude, the latest weather, the satellite orbits and
  your settings are stored on the watch so the face keeps working without the
  phone.
- The phone app caches the satellite orbits for 6 hours, and keeps your
  settings so the settings page opens with them.
- Heart rate and step counts, if you choose those readouts, are read on the
  watch from its own health data and are only drawn on the face. They are
  never sent anywhere.

## If you deny location access

Nadir falls back to Sydney, Australia, and skips the weather request.
