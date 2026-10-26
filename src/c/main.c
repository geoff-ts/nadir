// Nadir: a watchface that shows where the Sun, Moon, planets and the ISS are.
//
// The outer ring is the ecliptic bent into a loop. It is turned so the point
// of the ecliptic that is rising sits at the middle of the left edge and the
// one that is setting at the middle of the right edge: the upper half of the
// screen is the part of the ecliptic above your horizon, the lower half the
// part below it, and the zenith is at the top. East is left, west is right.
// The inner band carries the zodiac constellations on the same mapping, so
// everything turns together over the day. The ISS is not on the ecliptic, so
// it is placed by its elevation and by which side of the sky it is on.
#include <pebble.h>

#include "astro.h"
#include "sky_data.h"

#define DEFAULT_LAT (-33.8688)
#define DEFAULT_LON 151.2093
#define TLE_MAX_AGE_DAYS 14.0
#define REQUEST_EVERY_MINUTES 30
#define PANEL_X 14
#define PANEL_Y 17
#define PANEL_RADIUS 14
#define TAB_HALF_DEG 7.0
#define TAB_MIN_GAP_DEG 13.0
#define BAND_INSET 13
#define BAND_SPREAD 10
#define BAND_SCALE 1.3
#define BAND_RADIUS 14
#define TICK_FROM 6
#define TICK_TO 11
#define NO_TEMP INT32_MIN
#define SMALL_CAP_TOP 8  // FONT_STARLOOM_27: from the top of the text box to the top of a capital
#define SMALL_CAP_H 19
#define TIME_CAP_TOP 26  // the same for FONT_STARLOOM_88
#define DATE_CAP_TOP 56
#define TIME_DIGIT_TOP 83
#define READOUT_DIGIT_TOP 163
#define READOUT_GAP 16

enum {
  PERSIST_LAT = 1,
  PERSIST_LON,
  PERSIST_TLE,
  PERSIST_TEMP,
  PERSIST_WCODE,
};

typedef struct {
  double lat;
  double lon;
  bool has_tle;
  AstroTle tle;
  int32_t temp;
  int32_t weather_code;
} Settings;

typedef struct {
  double asc_deg;
  double precession_deg;
  double psi[BODY_COUNT];
  double moon_elongation_deg;
  bool iss_visible;
  double iss_psi;
} Sky;

static Window *s_window;
static Layer *s_canvas;
static Settings s_settings = {.lat = DEFAULT_LAT, .lon = DEFAULT_LON, .temp = NO_TEMP};
static Sky s_sky;
static GFont s_font_time;
static GFont s_font_small;
static GFont s_font_glyph;
static char s_time_text[8];
static char s_date_text[16];
static int s_battery = 100;
static bool s_charging;
static int s_minutes_since_request;

// ---------------------------------------------------------------- geometry

static int round_px(double v) { return (int)(v < 0 ? v - 0.5 : v + 0.5); }

static double wrap_deg(double d) {
  d = astro_remainder(d, 360.0);
  return d < 0 ? d + 360.0 : d;
}

// A point on a rectangle of half-extents a x b, reached by walking a fraction
// of the perimeter: psi is degrees clockwise from the top, so 90 degrees is
// always the middle of the right edge whatever the aspect ratio.
static GPoint ring_point(GPoint centre, double a, double b, double psi) {
  psi = wrap_deg(psi);
  int quadrant = (int)(psi / 90.0);
  double walk = (psi - quadrant * 90.0) / 90.0 * (a + b);
  double x, y;
  switch (quadrant) {
    case 0:
      if (walk <= a) { x = walk; y = -b; } else { x = a; y = -b + (walk - a); }
      break;
    case 1:
      if (walk <= b) { x = a; y = walk; } else { x = a - (walk - b); y = b; }
      break;
    case 2:
      if (walk <= a) { x = -walk; y = b; } else { x = -a; y = b - (walk - a); }
      break;
    default:
      if (walk <= b) { x = -a; y = -walk; } else { x = -a + (walk - b); y = -b; }
      break;
  }
  return GPoint(centre.x + round_px(x), centre.y + round_px(y));
}

// Fills the stretch of the ring between two rectangles that spans
// [psi - half, psi + half], walking round any corner on the way.
static void fill_tab(GContext *ctx, GPoint centre, double outer_a, double outer_b,
                     double inner_a, double inner_b, double psi, double half) {
  double angles[6];
  int count = 0;
  double start = psi - half, end = psi + half;
  angles[count++] = start;
  double corner = 90.0 * outer_a / (outer_a + outer_b);
  const double corners[4] = {corner, 180.0 - corner, 180.0 + corner, 360.0 - corner};
  for (int turn = -1; turn <= 1; turn++) {
    for (int i = 0; i < 4; i++) {
      double c = corners[i] + 360.0 * turn;
      if (c > start && c < end && count < 5) angles[count++] = c;
    }
  }
  angles[count++] = end;

  GPoint points[12];
  int n = 0;
  for (int i = 0; i < count; i++) points[n++] = ring_point(centre, outer_a, outer_b, angles[i]);
  for (int i = count - 1; i >= 0; i--) points[n++] = ring_point(centre, inner_a, inner_b, angles[i]);

  GPathInfo info = {.num_points = (uint32_t)n, .points = points};
  GPath *path = gpath_create(&info);
  if (!path) return;
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// ------------------------------------------------------------------- sky

static void update_sky(time_t now) {
  double jd = astro_jd(now);
  double lon[BODY_COUNT];
  astro_ecliptic_lon(jd, lon);
  double lst = astro_lst_deg(jd, s_settings.lon);
  s_sky.asc_deg = astro_ascendant_deg(jd, lst, s_settings.lat);
  s_sky.precession_deg = astro_precession_deg(jd);
  for (int i = 0; i < BODY_COUNT; i++) s_sky.psi[i] = s_sky.asc_deg - lon[i] - 90.0;
  s_sky.moon_elongation_deg = wrap_deg(lon[BODY_MOON] - lon[BODY_SUN]);

  s_sky.iss_visible = false;
  if (s_settings.has_tle && astro_abs(jd - s_settings.tle.epoch_jd) < TLE_MAX_AGE_DAYS) {
    double el, az;
    astro_sat_altaz(&s_settings.tle, jd, s_settings.lat, s_settings.lon, &el, &az);
    s_sky.iss_psi = (az < 180.0 ? -1.0 : 1.0) * (90.0 - el);
    s_sky.iss_visible = true;
  }

  struct tm *local = localtime(&now);
  strftime(s_time_text, sizeof(s_time_text), clock_is_24h_style() ? "%H:%M" : "%I:%M", local);
  static const char *const days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char *const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                       "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  snprintf(s_date_text, sizeof(s_date_text), "%s %d %s", days[local->tm_wday], local->tm_mday,
           months[local->tm_mon]);
}

static bool below_horizon(double psi) {
  psi = wrap_deg(psi);
  return psi > 90.0 && psi < 270.0;
}

static GColor body_color(int body) {
  switch (body) {
    case BODY_SUN: return GColorChromeYellow;
    case BODY_MOON: return GColorWhite;
    case BODY_MERCURY: return GColorVividViolet;
    case BODY_VENUS: return GColorFashionMagenta;
    case BODY_MARS: return GColorRed;
    case BODY_JUPITER: return GColorOrange;
    default: return GColorSpringBud;
  }
}

// Keeps neighbouring tabs from hiding each other; they are nudged apart
// rather than overlapped, so a conjunction reads as two bodies side by side.
static void spread_tabs(double *psi, int count) {
  int order[BODY_COUNT + 1];
  for (int i = 0; i < count; i++) {
    psi[i] = wrap_deg(psi[i]);
    order[i] = i;
  }
  for (int i = 1; i < count; i++) {
    int key = order[i];
    int j = i - 1;
    while (j >= 0 && psi[order[j]] > psi[key]) { order[j + 1] = order[j]; j--; }
    order[j + 1] = key;
  }
  for (int pass = 0; pass < 12; pass++) {
    for (int i = 0; i < count; i++) {
      int a = order[i], b = order[(i + 1) % count];
      double gap = psi[b] - psi[a];
      if (gap < 0) gap += 360.0;
      if (count > 1 && gap < TAB_MIN_GAP_DEG) {
        double shift = (TAB_MIN_GAP_DEG - gap) / 2.0;
        psi[a] -= shift;
        psi[b] += shift;
      }
    }
  }
}

static void draw_sun_glyph(GContext *ctx, GPoint c, GColor ink) {
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, c, 5);
  graphics_context_set_fill_color(ctx, ink);
  graphics_fill_circle(ctx, c, 1);
}

// The Moon as a small disc, lit as it is tonight. A waxing Moon is lit on its
// right in the north and on its left in the south, so the lit side flips with
// the sign of the latitude.
static void draw_moon_glyph(GContext *ctx, GPoint c, GColor ink) {
  const int radius = 7;
  double elongation = s_sky.moon_elongation_deg;
  double terminator = astro_cos(elongation * 0.017453292519943295);
  bool waxing = elongation <= 180.0;
  int flip = s_settings.lat < 0 ? -1 : 1;

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, c, radius);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  for (int dy = -radius; dy <= radius; dy++) {
    int half = round_px(astro_sqrt((double)(radius * radius - dy * dy)));
    int edge = round_px(terminator * half);
    int from = waxing ? edge : -half;
    int to = waxing ? half : -edge;
    if (from > to) continue;
    graphics_draw_line(ctx, GPoint(c.x + flip * from, c.y + dy), GPoint(c.x + flip * to, c.y + dy));
  }
  graphics_context_set_stroke_color(ctx, ink);
  graphics_draw_circle(ctx, c, radius);
}

static void draw_iss_glyph(GContext *ctx, GPoint c, GColor ink) {
  graphics_context_set_fill_color(ctx, ink);
  graphics_fill_rect(ctx, GRect(c.x - 1, c.y - 4, 3, 9), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(c.x - 7, c.y - 3, 5, 3), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(c.x + 3, c.y - 3, 5, 3), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(c.x - 7, c.y + 1, 5, 3), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(c.x + 3, c.y + 1, 5, 3), 0, GCornerNone);
}

static void draw_glyph(GContext *ctx, GPoint c, int body, GColor ink) {
  static const char *const glyphs[BODY_COUNT] = {NULL, "\xE2\x98\xBD", "\xE2\x98\xBF",
                                                  "\xE2\x99\x80", "\xE2\x99\x82",
                                                  "\xE2\x99\x83", "\xE2\x99\x84"};
  if (body == BODY_SUN) {
    draw_sun_glyph(ctx, c, ink);
    return;
  }
  if (body == BODY_MOON) {
    draw_moon_glyph(ctx, c, ink);
    return;
  }
  graphics_context_set_text_color(ctx, ink);
  graphics_draw_text(ctx, glyphs[body], s_font_glyph, GRect(c.x - 12, c.y - 12, 24, 22),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// Where each tab sits after nudging; the ISS, when known, is the last entry.
static int tab_positions(double *psi) {
  for (int i = 0; i < BODY_COUNT; i++) psi[i] = s_sky.psi[i];
  int count = BODY_COUNT;
  if (s_sky.iss_visible) psi[count++] = s_sky.iss_psi;
  spread_tabs(psi, count);
  return count;
}

// Tab i of tab_positions(): its colour, and whether its body is down.
static GColor tab_color(int i) { return i < BODY_COUNT ? body_color(i) : GColorCyan; }

static bool tab_below(int i) {
  return below_horizon(i < BODY_COUNT ? s_sky.psi[i] : s_sky.iss_psi);
}

// A body below the horizon keeps only the two sides of its tab, so the solid
// tabs are exactly what is up in the sky now.
static void outline_tab(GContext *ctx, GPoint centre, double hw, double hh, double ia, double ib,
                        double psi) {
  graphics_context_set_stroke_width(ctx, 2);
  for (int side = -1; side <= 1; side += 2) {
    double edge = psi + side * TAB_HALF_DEG;
    graphics_draw_line(ctx, ring_point(centre, hw, hh, edge), ring_point(centre, ia, ib, edge));
  }
}

static void draw_ring(GContext *ctx, GPoint centre, double hw, double hh) {
  double ia = hw - PANEL_X - 6, ib = hh - PANEL_Y - 6;
  double psi[BODY_COUNT + 1];
  int count = tab_positions(psi);

  // Outlines under the solid tabs; then slower bodies first, so the Sun and
  // Moon are never the ones covered. The ISS, last, goes on top.
  static const int order[] = {BODY_SATURN, BODY_JUPITER, BODY_MARS, BODY_VENUS,
                              BODY_MERCURY, BODY_MOON, BODY_SUN, BODY_COUNT};
  for (int pass = 0; pass < 2; pass++) {
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
      int tab = order[i];
      if (tab >= count || tab_below(tab) != (pass == 0)) continue;
      if (pass == 0) {
        graphics_context_set_stroke_color(ctx, tab_color(tab));
        outline_tab(ctx, centre, hw, hh, ia, ib, psi[tab]);
      } else {
        graphics_context_set_fill_color(ctx, tab_color(tab));
        fill_tab(ctx, centre, hw, hh, ia, ib, psi[tab], TAB_HALF_DEG);
      }
    }
  }
}

// Drawn after the panel is cut out so the glyphs are never clipped by it.
// Black on a solid tab, the body's colour on an outlined one.
static void draw_ring_glyphs(GContext *ctx, GPoint centre, double hw, double hh) {
  double ma = hw - PANEL_X / 2.0, mb = hh - PANEL_Y / 2.0;
  double psi[BODY_COUNT + 1];
  int count = tab_positions(psi);
  for (int tab = 0; tab < count; tab++) {
    GPoint c = ring_point(centre, ma, mb, psi[tab]);
    GColor ink = tab_below(tab) ? tab_color(tab) : GColorBlack;
    if (tab < BODY_COUNT) draw_glyph(ctx, c, tab, ink);
    else draw_iss_glyph(ctx, c, ink);
  }
}

// First quarter of a rounded rectangle's outline, walked by arc length from the
// top centre to the middle of the right edge: base point and outward normal.
static void corner_quarter(double a, double b, double r, double walk, double *x, double *y,
                           double *nx, double *ny) {
  const double half_pi = 1.5707963267948966;
  double top = a - r, arc = half_pi * r;
  if (walk <= top) {
    *x = walk; *y = -b; *nx = 0; *ny = -1;
  } else if (walk <= top + arc) {
    double theta = -half_pi + (walk - top) / r;
    *nx = astro_cos(theta); *ny = astro_sin(theta);
    *x = a - r + r * *nx; *y = -(b - r) + r * *ny;
  } else {
    *x = a; *y = -(b - r) + (walk - top - arc); *nx = 1; *ny = 0;
  }
}

// A point on a rounded rectangle's outline, psi degrees clockwise from the top
// (90 is always the middle of the right edge), pushed out along the outline's
// own normal. Walking the true outline keeps a constellation's shape, rather
// than smearing it round the corners.
static GPoint band_point(GPoint centre, double a, double b, double r, double psi, double offset) {
  psi = wrap_deg(psi);
  int quadrant = (int)(psi / 90.0);
  double quarter = (a - r) + 1.5707963267948966 * r + (b - r);
  double walk = (psi - quadrant * 90.0) / 90.0 * quarter;
  if (quadrant == 1 || quadrant == 3) walk = quarter - walk;
  double x, y, nx, ny;
  corner_quarter(a, b, r, walk, &x, &y, &nx, &ny);
  if (quadrant == 1 || quadrant == 2) { y = -y; ny = -ny; }
  if (quadrant >= 2) { x = -x; nx = -nx; }
  return GPoint(centre.x + round_px(x + nx * offset), centre.y + round_px(y + ny * offset));
}

static GPoint star_point(GPoint centre, double hw, double hh, const SkyVertex *v) {
  double lon = v->lon / 100.0 + s_sky.precession_deg;
  double psi = s_sky.asc_deg - lon - 90.0;
  double offset = v->lat / 100.0 * BAND_SCALE;
  if (offset > BAND_SPREAD) offset = BAND_SPREAD;
  if (offset < -BAND_SPREAD) offset = -BAND_SPREAD;
  return band_point(centre, hw - PANEL_X - BAND_INSET, hh - PANEL_Y - BAND_INSET, BAND_RADIUS, psi, offset);
}

static void draw_constellations(GContext *ctx, GPoint centre, double hw, double hh) {
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < SKY_SEGMENT_COUNT; i++) {
    GPoint p = star_point(centre, hw, hh, &SKY_VERTICES[SKY_SEGMENTS[i].from]);
    GPoint q = star_point(centre, hw, hh, &SKY_VERTICES[SKY_SEGMENTS[i].to]);
    graphics_draw_line(ctx, p, q);
  }
  graphics_context_set_fill_color(ctx, GColorLightGray);
  for (int i = 0; i < SKY_VERTEX_COUNT; i++) {
    GPoint p = star_point(centre, hw, hh, &SKY_VERTICES[i]);
    graphics_fill_rect(ctx, GRect(p.x - 1, p.y - 1, 2, 2), 0, GCornerNone);
  }
}

// A tick every 30 degrees of longitude, where one sign of the zodiac hands
// over to the next.
static void draw_zodiac_ticks(GContext *ctx, GPoint centre, double hw, double hh) {
  double a = hw - PANEL_X - BAND_INSET, b = hh - PANEL_Y - BAND_INSET;
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  for (int sign = 0; sign < 12; sign++) {
    double psi = s_sky.asc_deg - sign * 30.0 - 90.0;
    graphics_draw_line(ctx, band_point(centre, a, b, BAND_RADIUS, psi, TICK_FROM),
                       band_point(centre, a, b, BAND_RADIUS, psi, TICK_TO));
  }
}

// ---------------------------------------------------------------- widgets

// Each readout is an icon with its number laid over it. A black box behind the
// digits cuts the icon away where they cross, so the icon wraps round the
// number instead of standing beside it.

// The battery reaches this far left of its digits, and the cloud's digits sit
// this far into the cloud, which is at least WEATHER_ICON_W across.
#define BATTERY_LEAD 7
#define WEATHER_LEAD 9
#define WEATHER_ICON_W 36

static GSize small_text_size(const char *text) {
  return graphics_text_layout_get_content_size(text, s_font_small, GRect(0, 0, 120, 60),
                                               GTextOverflowModeFill, GTextAlignmentLeft);
}

static void draw_digits(GContext *ctx, const char *text, int x, int w) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(x - 2, READOUT_DIGIT_TOP - 3, w + 4, SMALL_CAP_H + 6), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, text, s_font_small, GRect(x, READOUT_DIGIT_TOP - SMALL_CAP_TOP, w + 4, 40),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// An upright cell whose right side the digits cover.
static void draw_battery_icon(GContext *ctx, int digits_x) {
  int left = digits_x - BATTERY_LEAD, top = READOUT_DIGIT_TOP - 7;
  graphics_context_set_fill_color(ctx, s_battery <= 20 ? GColorRed
                                       : (s_charging ? GColorYellow : GColorGreen));
  graphics_fill_rect(ctx, GRect(left + 3, top - 3, 6, 3), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(left, top, 12, 2), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(left, top, 2, 24), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(left, top + 22, 12, 2), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(left + 10, top, 2, 24), 0, GCornerNone);
}

static void cloud_shape(GContext *ctx, GPoint o, int grow) {
  graphics_fill_circle(ctx, GPoint(o.x + 8, o.y + 14), 6 + grow);
  graphics_fill_circle(ctx, GPoint(o.x + 19, o.y + 9), 9 + grow);
  graphics_fill_circle(ctx, GPoint(o.x + 31, o.y + 13), 7 + grow);
  graphics_fill_rect(ctx, GRect(o.x + 8, o.y + 14 - grow, 23, 6 + grow * 2), 0, GCornerNone);
}

// o is the icon's origin: its visible left edge is o.x + 2, and the digits go
// WEATHER_LEAD in from there and 14 below its top, under the left bumps.
static void draw_weather_icon(GContext *ctx, GPoint o, int code) {
  GColor blue = GColorVividCerulean;
  if (code <= 1) {
    graphics_context_set_stroke_color(ctx, GColorChromeYellow);
    graphics_context_set_stroke_width(ctx, 2);
    GPoint c = GPoint(o.x + 30, o.y + 8);
    graphics_draw_circle(ctx, c, 6);
    for (int i = 0; i < 8; i++) {
      double a = i * 0.78539816339744830962;
      graphics_draw_line(ctx, GPoint(c.x + round_px(9 * astro_sin(a)), c.y - round_px(9 * astro_cos(a))),
                         GPoint(c.x + round_px(12 * astro_sin(a)), c.y - round_px(12 * astro_cos(a))));
    }
    return;
  }
  graphics_context_set_fill_color(ctx, blue);
  cloud_shape(ctx, o, 0);
  graphics_context_set_fill_color(ctx, GColorBlack);
  cloud_shape(ctx, o, -2);

  // Rain, snow and lightning hang under the right bump, clear of the digits.
  bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95;
  graphics_context_set_stroke_width(ctx, 2);
  if (rain) {
    graphics_context_set_stroke_color(ctx, blue);
    for (int i = 0; i < 2; i++) {
      graphics_draw_line(ctx, GPoint(o.x + 32 + i * 5, o.y + 23), GPoint(o.x + 30 + i * 5, o.y + 28));
    }
  }
  if (snow) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int i = 0; i < 2; i++) graphics_fill_circle(ctx, GPoint(o.x + 31 + i * 5, o.y + 25), 1);
  }
  if (code >= 95) {
    graphics_context_set_stroke_color(ctx, GColorYellow);
    graphics_draw_line(ctx, GPoint(o.x + 33, o.y + 17), GPoint(o.x + 29, o.y + 23));
    graphics_draw_line(ctx, GPoint(o.x + 29, o.y + 23), GPoint(o.x + 34, o.y + 23));
    graphics_draw_line(ctx, GPoint(o.x + 34, o.y + 23), GPoint(o.x + 30, o.y + 29));
  }
}

// Battery and weather side by side, centred together under the time.
static void draw_readouts(GContext *ctx, int cx) {
  char battery[12], temp[12];
  snprintf(battery, sizeof(battery), "%d", s_battery);
  int battery_w = small_text_size(battery).w;
  int width = BATTERY_LEAD + battery_w;
  bool has_temp = s_settings.temp != NO_TEMP;
  int temp_w = 0;
  if (has_temp) {
    snprintf(temp, sizeof(temp), "%d", (int)s_settings.temp);
    temp_w = small_text_size(temp).w;
    int weather_w = WEATHER_LEAD + temp_w;
    width += READOUT_GAP + (weather_w > WEATHER_ICON_W ? weather_w : WEATHER_ICON_W);
  }
  int battery_x = cx - width / 2 + BATTERY_LEAD;
  draw_battery_icon(ctx, battery_x);
  draw_digits(ctx, battery, battery_x, battery_w);
  if (has_temp) {
    int temp_x = battery_x + battery_w + READOUT_GAP + WEATHER_LEAD;
    draw_weather_icon(ctx, GPoint(temp_x - WEATHER_LEAD - 2, READOUT_DIGIT_TOP - 14),
                      (int)s_settings.weather_code);
    draw_digits(ctx, temp, temp_x, temp_w);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  double hw = bounds.size.w / 2.0, hh = bounds.size.h / 2.0;
  GPoint centre = GPoint(bounds.size.w / 2, bounds.size.h / 2);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  draw_ring(ctx, centre, hw, hh);

  // Cuts the tabs' inner ends to the panel's rounded outline.
  GRect panel = GRect(PANEL_X, PANEL_Y, bounds.size.w - 2 * PANEL_X, bounds.size.h - 2 * PANEL_Y);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, panel, PANEL_RADIUS, GCornersAll);

  draw_ring_glyphs(ctx, centre, hw, hh);
  draw_constellations(ctx, centre, hw, hh);
  draw_zodiac_ticks(ctx, centre, hw, hh);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_date_text, s_font_small,
                     GRect(0, DATE_CAP_TOP - SMALL_CAP_TOP, bounds.size.w, 40),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, s_time_text, s_font_time,
                     GRect(0, TIME_DIGIT_TOP - TIME_CAP_TOP, bounds.size.w, 120),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  draw_readouts(ctx, centre.x);
}

// ----------------------------------------------------------- phone and tick

static void request_refresh(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_uint8(out, MESSAGE_KEY_REQUEST, 1);
  app_message_outbox_send();
  s_minutes_since_request = 0;
}

static void load_tle(const char *both_lines) {
  const char *newline = strchr(both_lines, '\n');
  if (!newline) return;
  char first[80];
  size_t len = (size_t)(newline - both_lines);
  if (len >= sizeof(first)) return;
  memcpy(first, both_lines, len);
  first[len] = '\0';
  AstroTle parsed;
  if (astro_tle_parse(first, newline + 1, &parsed)) {
    s_settings.tle = parsed;
    s_settings.has_tle = true;
  }
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *lat = dict_find(iter, MESSAGE_KEY_LAT);
  Tuple *lon = dict_find(iter, MESSAGE_KEY_LON);
  if (lat && lon) {
    s_settings.lat = lat->value->int32 / 10000.0;
    s_settings.lon = lon->value->int32 / 10000.0;
    persist_write_int(PERSIST_LAT, lat->value->int32);
    persist_write_int(PERSIST_LON, lon->value->int32);
  }
  Tuple *tle = dict_find(iter, MESSAGE_KEY_TLE_LINES);
  if (tle) {
    load_tle(tle->value->cstring);
    persist_write_string(PERSIST_TLE, tle->value->cstring);
  }
  Tuple *temp = dict_find(iter, MESSAGE_KEY_TEMP);
  Tuple *code = dict_find(iter, MESSAGE_KEY_WCODE);
  if (temp && code) {
    s_settings.temp = temp->value->int32;
    s_settings.weather_code = code->value->int32;
    persist_write_int(PERSIST_TEMP, temp->value->int32);
    persist_write_int(PERSIST_WCODE, code->value->int32);
  }
  update_sky(time(NULL));
  layer_mark_dirty(s_canvas);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_sky(time(NULL));
  layer_mark_dirty(s_canvas);
  if (++s_minutes_since_request >= REQUEST_EVERY_MINUTES) request_refresh();
}

static void battery_handler(BatteryChargeState state) {
  s_battery = state.charge_percent;
  s_charging = state.is_charging;
  layer_mark_dirty(s_canvas);
}

static void load_settings(void) {
  if (persist_exists(PERSIST_LAT) && persist_exists(PERSIST_LON)) {
    s_settings.lat = persist_read_int(PERSIST_LAT) / 10000.0;
    s_settings.lon = persist_read_int(PERSIST_LON) / 10000.0;
  }
  if (persist_exists(PERSIST_TLE)) {
    char buffer[160];
    if (persist_read_string(PERSIST_TLE, buffer, sizeof(buffer)) > 0) load_tle(buffer);
  }
  if (persist_exists(PERSIST_TEMP) && persist_exists(PERSIST_WCODE)) {
    s_settings.temp = persist_read_int(PERSIST_TEMP);
    s_settings.weather_code = persist_read_int(PERSIST_WCODE);
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) { layer_destroy(s_canvas); }

static void init(void) {
  s_font_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_STARLOOM_88));
  s_font_small = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_STARLOOM_27));
  s_font_glyph = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_NOTO_16));

  load_settings();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){.load = window_load, .unload = window_unload});
  window_stack_push(s_window, true);

  BatteryChargeState battery = battery_state_service_peek();
  s_battery = battery.charge_percent;
  s_charging = battery.is_charging;
  update_sky(time(NULL));

  battery_state_service_subscribe(battery_handler);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 32);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_window);
  fonts_unload_custom_font(s_font_time);
  fonts_unload_custom_font(s_font_small);
  fonts_unload_custom_font(s_font_glyph);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
