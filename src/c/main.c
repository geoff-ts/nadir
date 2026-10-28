// Nadir: a watchface that shows where the Sun, Moon, planets and space stations are.
//
// The outer ring is the ecliptic bent into a loop. It is turned so the point
// of the ecliptic that is rising sits at the middle of the left edge and the
// one that is setting at the middle of the right edge: the upper half of the
// screen is the part of the ecliptic above your horizon, the lower half the
// part below it, and the zenith is at the top. East is left, west is right, as
// you see the sky facing south from the northern hemisphere. Facing north from
// the southern hemisphere the whole face is mirrored: east is right.
// The inner band carries the zodiac constellations on the same mapping, so
// everything turns together over the day. Satellites (the ISS, Tiangong and
// Hubble) are not on the ecliptic, so they are placed by their elevation and by
// which side of the sky they are on.
#include <pebble.h>

#include "astro.h"
#include "sky_data.h"

#define DEFAULT_LAT (-33.8688)
#define DEFAULT_LON 151.2093
#define TLE_MAX_AGE_DAYS 14.0
#define REQUEST_EVERY_MINUTES 30
#define TROPIC_DEG 23.44  // the automatic view only changes once you are out of the tropics
#define PASS_UPDATE_MS 3000
#define PASS_MIN_EL_DEG 10.0
#define DEFAULT_ROLL_HOURS 4
// The roll ahead and the pause together take the chosen roll time, split 9 : 5;
// the slide back always takes ROLL_BACK_MS.
#define DEFAULT_ROLL_TENTHS 14
#define MIN_ROLL_TENTHS 5
#define MAX_ROLL_TENTHS 50
#define ROLL_BACK_MS 200
#define TAB_HALF_DEG 7.0
#define TAB_MIN_GAP_DEG 13.0
#define BAND_INSET 13
#define BAND_SPREAD 10
#define BAND_SCALE 1.3
#define BAND_RADIUS 14
#define TICK_FROM 6
#define TICK_TO 11
#define NO_TEMP INT32_MIN
#define SMALL_CAP_TOP 6  // FONT_BRUNO_ACE_21: from the top of the text box to the top of a digit
#define SMALL_CAP_H 15
#define TIME_BOLD 1
#define PANEL_RADIUS 14  // rectangular screens only

// Each screen's layout. PANEL_X and PANEL_Y are the ring's width at the sides
// and at the top and bottom; the *_TOP rows are where the tops of the digits
// sit. Round screens have a circular ring, so their PANEL_X and PANEL_Y match.
#if defined(PBL_PLATFORM_EMERY)
#define PANEL_X 14
#define PANEL_Y 17
#define DATE_CAP_TOP 73
#define TIME_DIGIT_TOP 95
#define READOUT_DIGIT_TOP 151
#define READOUT_GAP 16
#elif defined(PBL_PLATFORM_GABBRO)
#define PANEL_X 16
#define PANEL_Y 16
#define DATE_CAP_TOP 89
#define TIME_DIGIT_TOP 111
#define READOUT_DIGIT_TOP 167
#define READOUT_GAP 16
#elif defined(PBL_PLATFORM_CHALK)
#define PANEL_X 14
#define PANEL_Y 14
#define DATE_CAP_TOP 50
#define TIME_DIGIT_TOP 66
#define READOUT_DIGIT_TOP 108
#define READOUT_GAP 10
#else  // 144 x 168
#define PANEL_X 14
#define PANEL_Y 15
#define DATE_CAP_TOP 43
#define TIME_DIGIT_TOP 59
#define READOUT_DIGIT_TOP 103
#define READOUT_GAP 10
#endif

// The clock and date fonts: the larger pair on the 200-px-and-wider screens.
#if PBL_DISPLAY_WIDTH >= 200
#define CLOCK_FONT RESOURCE_ID_FONT_NADIR_CLOCK_54
#define TIME_CAP_TOP 16  // from the top of the text box to the top of a digit
#define DATE_FONT RESOURCE_ID_FONT_BRUNO_ACE_19
#define DATE_TEXT_TOP 5
#else
#define CLOCK_FONT RESOURCE_ID_FONT_NADIR_CLOCK_38
#define TIME_CAP_TOP 11
#define DATE_FONT RESOURCE_ID_FONT_BRUNO_ACE_14
#define DATE_TEXT_TOP 4
#endif

enum {
  PERSIST_LAT = 1,
  PERSIST_LON,
  PERSIST_TLE,
  PERSIST_TEMP,
  PERSIST_WCODE,
  PERSIST_PREFS,
  PERSIST_TLE_CSS,
  PERSIST_TLE_HST,
  PERSIST_AUTO_SOUTH,
};

// Satellites, each placed like the ISS. PERSIST_TLE holds the ISS's elements.
enum {
  SAT_ISS,
  SAT_CSS,
  SAT_HST,
  SAT_COUNT,
};

// Tabs on the ring: the bodies first, then the satellites.
#define TAB_IDS (BODY_COUNT + SAT_COUNT)

// What each readout slot shows; the numbers are the settings page's values.
enum {
  READOUT_NONE,
  READOUT_BATTERY,
  READOUT_WEATHER,
  READOUT_HEART,
  READOUT_STEPS,
  READOUT_KINDS,
};
#define READOUT_SLOTS 2

// Which way the face looks at the sky; the numbers are the settings page's values.
enum {
  VIEW_AUTO,
  VIEW_NORTH,  // facing south, as from the northern hemisphere
  VIEW_SOUTH,  // facing north, as from the southern hemisphere: mirrored
};

// Chosen on the phone's settings page.
typedef struct {
  uint8_t readout[READOUT_SLOTS];
  bool fahrenheit;
  bool roll;
  uint8_t roll_hours;
  bool sat[SAT_COUNT];  // added after the first release: older saves stop short of it
  uint8_t view;  // added in 1.1
  uint8_t roll_tenths;  // added in 1.2: the roll ahead and pause, in tenths of a second
} Prefs;

typedef struct {
  double lat;
  double lon;
  bool has_tle[SAT_COUNT];
  AstroTle tle[SAT_COUNT];
  int32_t temp;
  int32_t weather_code;
} Settings;

typedef struct {
  double asc_deg;
  double precession_deg;
  double psi[BODY_COUNT];
  double moon_elongation_deg;
  bool sat_known[SAT_COUNT];
  bool sat_high[SAT_COUNT];  // above PASS_MIN_EL_DEG
  double sat_psi[SAT_COUNT];
} Sky;

static Window *s_window;
static Layer *s_canvas;
static Settings s_settings = {.lat = DEFAULT_LAT, .lon = DEFAULT_LON, .temp = NO_TEMP};
static Sky s_sky;
static Prefs s_prefs = {.readout = {READOUT_BATTERY, READOUT_WEATHER}, .roll = true,
                        .roll_hours = DEFAULT_ROLL_HOURS, .sat = {true, false, false},
                        .roll_tenths = DEFAULT_ROLL_TENTHS};
static int s_heart_bpm;  // 0 while unknown
static int s_steps = -1;  // -1 while unknown
static GFont s_font_time;
static GFont s_font_small;
static GFont s_font_date;
static GFont s_font_glyph;
static char s_time_text[8];
static char s_date_text[16];
static int s_battery = 100;
static bool s_charging;
static int s_minutes_since_request;
static time_t s_offset;  // how far ahead of now the face is drawn, during a roll
static Animation *s_roll;
static AppTimer *s_pass_timer;
static GPoint s_stars[SKY_VERTEX_COUNT];  // where each star is drawn, worked out once per sky update
static bool s_stars_stale = true;
static bool s_ticks_stale = true;  // the same for the zodiac ticks
static bool s_auto_south = true;  // the automatic view's choice, kept through the tropics

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
#if defined(PBL_ROUND)
  // A circle of radius a (a and b are equal on a round screen).
  double rad = psi * 0.017453292519943295;
  return GPoint(centre.x + round_px(a * astro_sin(rad)), centre.y - round_px(a * astro_cos(rad)));
#endif
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
#if defined(PBL_ROUND)
  // A circle has no corners: a few points along the arc instead.
  for (int i = 1; i < 4; i++) angles[count++] = start + (end - start) * i / 4.0;
#else
  double corner = 90.0 * outer_a / (outer_a + outer_b);
  const double corners[4] = {corner, 180.0 - corner, 180.0 + corner, 360.0 - corner};
  for (int turn = -1; turn <= 1; turn++) {
    for (int i = 0; i < 4; i++) {
      double c = corners[i] + 360.0 * turn;
      if (c > start && c < end && count < 5) angles[count++] = c;
    }
  }
#endif
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

// The automatic view follows the hemisphere, but only changes once you are
// clear of the tropics, so living or travelling near the equator does not
// flip the face back and forth.
static void update_auto_view(void) {
  bool south = s_auto_south;
  if (!persist_exists(PERSIST_AUTO_SOUTH)) south = s_settings.lat < 0;
  if (s_settings.lat > TROPIC_DEG) south = false;
  if (s_settings.lat < -TROPIC_DEG) south = true;
  if (south != s_auto_south || !persist_exists(PERSIST_AUTO_SOUTH)) {
    s_auto_south = south;
    persist_write_bool(PERSIST_AUTO_SOUTH, south);
  }
}

// 1 facing south (east on the left), -1 facing north (mirrored, east on the right).
static double view_sign(void) {
  bool south = s_prefs.view == VIEW_SOUTH || (s_prefs.view == VIEW_AUTO && s_auto_south);
  return south ? -1.0 : 1.0;
}

static void update_sats(time_t when) {
  double jd = astro_jd(when);
  for (int sat = 0; sat < SAT_COUNT; sat++) {
    s_sky.sat_known[sat] = s_prefs.sat[sat] && s_settings.has_tle[sat] &&
                           astro_abs(jd - s_settings.tle[sat].epoch_jd) < TLE_MAX_AGE_DAYS;
    s_sky.sat_high[sat] = false;
    if (!s_sky.sat_known[sat]) continue;
    double el, az;
    astro_sat_altaz(&s_settings.tle[sat], jd, s_settings.lat, s_settings.lon, &el, &az);
    s_sky.sat_psi[sat] = view_sign() * (az < 180.0 ? -1.0 : 1.0) * (90.0 - el);
    s_sky.sat_high[sat] = el > PASS_MIN_EL_DEG;
  }
}

static void update_sky(time_t when) {
  double jd = astro_jd(when);
  double lon[BODY_COUNT];
  astro_ecliptic_lon(jd, lon);
  double lst = astro_lst_deg(jd, s_settings.lon);
  s_sky.asc_deg = astro_ascendant_deg(jd, lst, s_settings.lat);
  s_sky.precession_deg = astro_precession_deg(jd);
  for (int i = 0; i < BODY_COUNT; i++) s_sky.psi[i] = view_sign() * (s_sky.asc_deg - lon[i] - 90.0);
  s_sky.moon_elongation_deg = wrap_deg(lon[BODY_MOON] - lon[BODY_SUN]);
  s_stars_stale = s_ticks_stale = true;
  update_sats(when);
}

static void update_clock(time_t now) {
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

// Black-and-white screens dither colours into a speckle that hides the
// glyphs, so there every tab and icon is plain white.
#define INK(colour) PBL_IF_COLOR_ELSE(colour, GColorWhite)

static GColor body_color(int body) {
#if defined(PBL_BW)
  return GColorWhite;
#endif
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
  int order[TAB_IDS];
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
// west side: the right facing south, the left facing north, so the lit side
// flips with the view.
static void draw_moon_glyph(GContext *ctx, GPoint c, GColor ink) {
  const int radius = 7;
  double elongation = s_sky.moon_elongation_deg;
  double terminator = astro_cos(elongation * 0.017453292519943295);
  bool waxing = elongation <= 180.0;
  int flip = (int)view_sign();

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

// The ISS as its truss and four wings, Tiangong as its T of modules, Hubble
// as its tube between two panels.
static void draw_sat_glyph(GContext *ctx, GPoint c, int sat, GColor ink) {
  graphics_context_set_fill_color(ctx, ink);
  switch (sat) {
    case SAT_ISS:
      graphics_fill_rect(ctx, GRect(c.x - 1, c.y - 4, 3, 9), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(c.x - 7, c.y - 3, 5, 3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(c.x + 3, c.y - 3, 5, 3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(c.x - 7, c.y + 1, 5, 3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(c.x + 3, c.y + 1, 5, 3), 0, GCornerNone);
      break;
    case SAT_CSS:
      graphics_fill_rect(ctx, GRect(c.x - 6, c.y - 4, 13, 3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(c.x - 1, c.y - 1, 3, 7), 0, GCornerNone);
      break;
    default:
      graphics_fill_rect(ctx, GRect(c.x - 2, c.y - 5, 5, 11), 1, GCornersAll);
      graphics_fill_rect(ctx, GRect(c.x - 8, c.y - 2, 4, 5), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(c.x + 5, c.y - 2, 4, 5), 0, GCornerNone);
      break;
  }
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

// A tab's true place, its colour, and whether its body is down.
static double tab_psi(int id) { return id < BODY_COUNT ? s_sky.psi[id] : s_sky.sat_psi[id - BODY_COUNT]; }

static GColor tab_color(int id) {
  static const uint8_t sat_colours[SAT_COUNT] = {GColorCyanARGB8, GColorPictonBlueARGB8,
                                                 GColorLightGrayARGB8};
  return id < BODY_COUNT ? body_color(id) : INK(((GColor){.argb = sat_colours[id - BODY_COUNT]}));
}

static bool tab_below(int id) { return below_horizon(tab_psi(id)); }

// Which tabs are on the ring, and where each sits after nudging, by tab id.
static void tab_positions(double psi[TAB_IDS], bool shown[TAB_IDS]) {
  double packed[TAB_IDS];
  int ids[TAB_IDS], count = 0;
  for (int id = 0; id < TAB_IDS; id++) {
    shown[id] = id < BODY_COUNT || s_sky.sat_known[id - BODY_COUNT];
    if (!shown[id]) continue;
    packed[count] = tab_psi(id);
    ids[count++] = id;
  }
  spread_tabs(packed, count);
  for (int i = 0; i < count; i++) psi[ids[i]] = packed[i];
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
  double psi[TAB_IDS];
  bool shown[TAB_IDS];
  tab_positions(psi, shown);

  // Outlines under the solid tabs; then slower bodies first, so the Sun and
  // Moon are never the ones covered. The satellites, last, go on top.
  static const int order[] = {BODY_SATURN, BODY_JUPITER, BODY_MARS, BODY_VENUS, BODY_MERCURY,
                              BODY_MOON, BODY_SUN, BODY_COUNT + SAT_HST, BODY_COUNT + SAT_CSS,
                              BODY_COUNT + SAT_ISS};
  for (int pass = 0; pass < 2; pass++) {
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
      int tab = order[i];
      if (!shown[tab] || tab_below(tab) != (pass == 0)) continue;
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
  double psi[TAB_IDS];
  bool shown[TAB_IDS];
  tab_positions(psi, shown);
  for (int tab = 0; tab < TAB_IDS; tab++) {
    if (!shown[tab]) continue;
    GPoint c = ring_point(centre, ma, mb, psi[tab]);
    GColor ink = tab_below(tab) ? tab_color(tab) : GColorBlack;
    if (tab < BODY_COUNT) draw_glyph(ctx, c, tab, ink);
    else draw_sat_glyph(ctx, c, tab - BODY_COUNT, ink);
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
#if defined(PBL_ROUND)
  return ring_point(centre, a + offset, b + offset, psi);
#endif
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
  // Mirroring also turns the band inside out: facing north, stars north of the
  // ecliptic sit nearer the horizon, so each constellation is upside down.
  double psi = view_sign() * (s_sky.asc_deg - lon - 90.0);
  double offset = view_sign() * v->lat / 100.0 * BAND_SCALE;
  if (offset > BAND_SPREAD) offset = BAND_SPREAD;
  if (offset < -BAND_SPREAD) offset = -BAND_SPREAD;
  return band_point(centre, hw - PANEL_X - BAND_INSET, hh - PANEL_Y - BAND_INSET, BAND_RADIUS, psi, offset);
}

static void draw_constellations(GContext *ctx, GPoint centre, double hw, double hh) {
  if (s_stars_stale) {
    for (int i = 0; i < SKY_VERTEX_COUNT; i++) s_stars[i] = star_point(centre, hw, hh, &SKY_VERTICES[i]);
    s_stars_stale = false;
  }
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < SKY_SEGMENT_COUNT; i++) {
    graphics_draw_line(ctx, s_stars[SKY_SEGMENTS[i].from], s_stars[SKY_SEGMENTS[i].to]);
  }
  graphics_context_set_fill_color(ctx, GColorLightGray);
  for (int i = 0; i < SKY_VERTEX_COUNT; i++) {
    graphics_fill_rect(ctx, GRect(s_stars[i].x - 1, s_stars[i].y - 1, 2, 2), 0, GCornerNone);
  }
}

// A tick every 30 degrees of longitude, where one sign of the zodiac hands
// over to the next.
static void draw_zodiac_ticks(GContext *ctx, GPoint centre, double hw, double hh) {
  static GPoint ends[12][2];
  if (s_ticks_stale) {
    double a = hw - PANEL_X - BAND_INSET, b = hh - PANEL_Y - BAND_INSET;
    for (int sign = 0; sign < 12; sign++) {
      double psi = view_sign() * (s_sky.asc_deg - sign * 30.0 - 90.0);
      ends[sign][0] = band_point(centre, a, b, BAND_RADIUS, psi, TICK_FROM);
      ends[sign][1] = band_point(centre, a, b, BAND_RADIUS, psi, TICK_TO);
    }
    s_ticks_stale = false;
  }
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  for (int sign = 0; sign < 12; sign++) graphics_draw_line(ctx, ends[sign][0], ends[sign][1]);
}

// ---------------------------------------------------------------- widgets

// Each readout is an icon with its number laid over it. A black box behind the
// digits cuts the icon away where they cross, so the icon wraps round the
// number instead of standing beside it.

// How far each readout's icon reaches left of its digits, and how wide the
// icon is at least, by readout kind.
static const int8_t s_readout_lead[READOUT_KINDS] = {0, 7, 9, 12, 13};
static const int8_t s_readout_min_w[READOUT_KINDS] = {0, 0, 36, 27, 0};

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
  int left = digits_x - s_readout_lead[READOUT_BATTERY], top = READOUT_DIGIT_TOP - 7;
  graphics_context_set_fill_color(ctx, INK(s_battery <= 20 ? GColorRed
                                           : (s_charging ? GColorYellow : GColorGreen)));
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

// The cloud's visible left edge is 2 px right of o, the digits 9 px further
// in and 14 px below its top, under the left bumps.
static void draw_weather_icon(GContext *ctx, int digits_x) {
  GPoint o = GPoint(digits_x - s_readout_lead[READOUT_WEATHER] - 2, READOUT_DIGIT_TOP - 14);
  int code = (int)s_settings.weather_code;
  GColor blue = INK(GColorVividCerulean);
  if (code <= 1) {
    graphics_context_set_stroke_color(ctx, INK(GColorChromeYellow));
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
    graphics_context_set_stroke_color(ctx, INK(GColorYellow));
    graphics_draw_line(ctx, GPoint(o.x + 33, o.y + 17), GPoint(o.x + 29, o.y + 23));
    graphics_draw_line(ctx, GPoint(o.x + 29, o.y + 23), GPoint(o.x + 34, o.y + 23));
    graphics_draw_line(ctx, GPoint(o.x + 34, o.y + 23), GPoint(o.x + 30, o.y + 29));
  }
}

static void heart_shape(GContext *ctx, GPoint o, int grow) {
  graphics_fill_circle(ctx, GPoint(o.x + 9, o.y + 9), 7 + grow);
  graphics_fill_circle(ctx, GPoint(o.x + 22, o.y + 9), 7 + grow);
  GPoint points[3] = {GPoint(o.x + 2 - grow, o.y + 11), GPoint(o.x + 29 + grow, o.y + 11),
                      GPoint(o.x + 15, o.y + 26 + grow)};
  GPathInfo info = {.num_points = 3, .points = points};
  GPath *path = gpath_create(&info);
  if (!path) return;
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// A heart outline whose left half shows beside the digits and whose right
// lobe shows above them.
static void draw_heart_icon(GContext *ctx, int digits_x) {
  GPoint o = GPoint(digits_x - s_readout_lead[READOUT_HEART] - 2, READOUT_DIGIT_TOP - 15);
  graphics_context_set_fill_color(ctx, INK(GColorFolly));
  heart_shape(ctx, o, 0);
  graphics_context_set_fill_color(ctx, GColorBlack);
  heart_shape(ctx, o, -2);
}

// Two footprints, one a step ahead of the other, left of the digits.
static void draw_steps_icon(GContext *ctx, int digits_x) {
  int left = digits_x - s_readout_lead[READOUT_STEPS], top = READOUT_DIGIT_TOP;
  graphics_context_set_fill_color(ctx, INK(GColorOrange));
  graphics_fill_rect(ctx, GRect(left, top - 2, 5, 9), 2, GCornersAll);
  graphics_fill_rect(ctx, GRect(left, top + 9, 5, 4), 1, GCornersAll);
  graphics_fill_rect(ctx, GRect(left + 6, top - 9, 5, 9), 2, GCornersAll);
  graphics_fill_rect(ctx, GRect(left + 6, top + 2, 5, 4), 1, GCornersAll);
}

static int celsius_shown(int celsius) {
  if (!s_prefs.fahrenheit) return celsius;
  int tenths = celsius * 18 + 320;
  return tenths >= 0 ? (tenths + 5) / 10 : (tenths - 5) / 10;
}

// The number a readout shows, or false when it has nothing to show at all.
static bool readout_text(int kind, char *text, size_t size) {
  switch (kind) {
    case READOUT_BATTERY:
      snprintf(text, size, "%d", s_battery);
      return true;
    case READOUT_WEATHER:
      if (s_settings.temp == NO_TEMP) return false;
      snprintf(text, size, "%d", celsius_shown((int)s_settings.temp));
      return true;
    case READOUT_HEART:
      if (s_heart_bpm > 0) snprintf(text, size, "%d", s_heart_bpm);
      else snprintf(text, size, "--");
      return true;
    case READOUT_STEPS:
      if (s_steps >= 0) snprintf(text, size, "%d", s_steps);
      else snprintf(text, size, "--");
      return true;
    default:
      return false;
  }
}

static void draw_readout_icon(GContext *ctx, int kind, int digits_x) {
  switch (kind) {
    case READOUT_BATTERY: draw_battery_icon(ctx, digits_x); break;
    case READOUT_WEATHER: draw_weather_icon(ctx, digits_x); break;
    case READOUT_HEART: draw_heart_icon(ctx, digits_x); break;
    case READOUT_STEPS: draw_steps_icon(ctx, digits_x); break;
    default: break;
  }
}

// The chosen readouts side by side, centred together under the time.
static void draw_readouts(GContext *ctx, int cx) {
  char text[READOUT_SLOTS][12];
  int digits_w[READOUT_SLOTS], width[READOUT_SLOTS];
  bool shown[READOUT_SLOTS];
  int total = -READOUT_GAP;
  for (int slot = 0; slot < READOUT_SLOTS; slot++) {
    int kind = s_prefs.readout[slot];
    shown[slot] = readout_text(kind, text[slot], sizeof(text[slot]));
    if (!shown[slot]) continue;
    digits_w[slot] = small_text_size(text[slot]).w;
    width[slot] = s_readout_lead[kind] + digits_w[slot];
    if (width[slot] < s_readout_min_w[kind]) width[slot] = s_readout_min_w[kind];
    total += width[slot] + READOUT_GAP;
  }
  int x = cx - total / 2;
  for (int slot = 0; slot < READOUT_SLOTS; slot++) {
    if (!shown[slot]) continue;
    int kind = s_prefs.readout[slot];
    int digits_x = x + s_readout_lead[kind];
    draw_readout_icon(ctx, kind, digits_x);
    draw_digits(ctx, text[slot], digits_x, digits_w[slot]);
    x += width[slot] + READOUT_GAP;
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
  graphics_context_set_fill_color(ctx, GColorBlack);
#if defined(PBL_ROUND)
  graphics_fill_circle(ctx, centre, (uint16_t)(hw - PANEL_X));
#else
  GRect panel = GRect(PANEL_X, PANEL_Y, bounds.size.w - 2 * PANEL_X, bounds.size.h - 2 * PANEL_Y);
  graphics_fill_rect(ctx, panel, PANEL_RADIUS, GCornersAll);
#endif

  draw_ring_glyphs(ctx, centre, hw, hh);
  draw_constellations(ctx, centre, hw, hh);
  draw_zodiac_ticks(ctx, centre, hw, hh);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_date_text, s_font_date,
                     GRect(0, DATE_CAP_TOP - DATE_TEXT_TOP, bounds.size.w, 40),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  // Drawn again shifted by up to TIME_BOLD px each way, which thickens every
  // stroke with square corners.
  for (int dy = 0; dy <= TIME_BOLD; dy++) {
    for (int dx = 0; dx <= TIME_BOLD; dx++) {
      graphics_draw_text(ctx, s_time_text, s_font_time,
                         GRect(dx, TIME_DIGIT_TOP - TIME_CAP_TOP + dy, bounds.size.w, 120),
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }
  }

  draw_readouts(ctx, centre.x);
}

// ----------------------------------------------------------- phone and tick

static void schedule_pass_update(void);

// A roll moves the sky only; the clock and date always say now.
static void refresh(void) {
  time_t now = time(NULL);
  update_sky(now + s_offset);
  update_clock(now);
  layer_mark_dirty(s_canvas);
  schedule_pass_update();
}

static void pass_update(void *context) {
  s_pass_timer = NULL;
  if (s_offset == 0) {
    update_sats(time(NULL));
    layer_mark_dirty(s_canvas);
  }
  schedule_pass_update();
}

// A satellite crosses the sky in a few minutes, so while one is well up (above
// PASS_MIN_EL_DEG) it is moved every PASS_UPDATE_MS rather than once a minute.
static void schedule_pass_update(void) {
  bool any_high = false;
  for (int sat = 0; sat < SAT_COUNT; sat++) any_high = any_high || s_sky.sat_high[sat];
  if (any_high && !s_pass_timer) s_pass_timer = app_timer_register(PASS_UPDATE_MS, pass_update, NULL);
}

static void request_refresh(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_uint8(out, MESSAGE_KEY_REQUEST, 1);
  app_message_outbox_send();
  s_minutes_since_request = 0;
}

static const uint32_t s_tle_persist[SAT_COUNT] = {PERSIST_TLE, PERSIST_TLE_CSS, PERSIST_TLE_HST};

static void load_tle(int sat, const char *both_lines) {
  const char *newline = strchr(both_lines, '\n');
  if (!newline) return;
  char first[80];
  size_t len = (size_t)(newline - both_lines);
  if (len >= sizeof(first)) return;
  memcpy(first, both_lines, len);
  first[len] = '\0';
  AstroTle parsed;
  if (astro_tle_parse(first, newline + 1, &parsed)) {
    s_settings.tle[sat] = parsed;
    s_settings.has_tle[sat] = true;
  }
}

// The settings page sends a select's value as text and a toggle or slider's
// as a number.
static int tuple_int(const Tuple *tuple) {
  if (tuple->type == TUPLE_CSTRING) {
    int value = 0;
    for (const char *c = tuple->value->cstring; *c >= '0' && *c <= '9'; c++) value = value * 10 + (*c - '0');
    return value;
  }
  switch (tuple->length) {
    case 1: return tuple->type == TUPLE_INT ? tuple->value->int8 : tuple->value->uint8;
    case 2: return tuple->type == TUPLE_INT ? tuple->value->int16 : tuple->value->uint16;
    default: return (int)tuple->value->int32;
  }
}

static bool read_prefs(DictionaryIterator *iter) {
  const uint32_t readout_keys[READOUT_SLOTS] = {MESSAGE_KEY_READOUT_LEFT, MESSAGE_KEY_READOUT_RIGHT};
  bool changed = false;
  for (int slot = 0; slot < READOUT_SLOTS; slot++) {
    Tuple *tuple = dict_find(iter, readout_keys[slot]);
    int kind = tuple ? tuple_int(tuple) : -1;
    if (kind >= 0 && kind < READOUT_KINDS) {
      s_prefs.readout[slot] = (uint8_t)kind;
      changed = true;
    }
  }
  Tuple *unit = dict_find(iter, MESSAGE_KEY_TEMP_UNIT);
  if (unit) { s_prefs.fahrenheit = tuple_int(unit) == 1; changed = true; }
  Tuple *roll = dict_find(iter, MESSAGE_KEY_ROLL_ON);
  if (roll) { s_prefs.roll = tuple_int(roll) != 0; changed = true; }
  const uint32_t sat_keys[SAT_COUNT] = {MESSAGE_KEY_SAT_ISS, MESSAGE_KEY_SAT_CSS, MESSAGE_KEY_SAT_HST};
  for (int sat = 0; sat < SAT_COUNT; sat++) {
    Tuple *shown = dict_find(iter, sat_keys[sat]);
    if (shown) { s_prefs.sat[sat] = tuple_int(shown) != 0; changed = true; }
  }
  Tuple *hours = dict_find(iter, MESSAGE_KEY_ROLL_HOURS);
  Tuple *tenths = dict_find(iter, MESSAGE_KEY_ROLL_TIME);
  if (tenths && tuple_int(tenths) >= MIN_ROLL_TENTHS && tuple_int(tenths) <= MAX_ROLL_TENTHS) {
    s_prefs.roll_tenths = (uint8_t)tuple_int(tenths);
    changed = true;
  }
  Tuple *view = dict_find(iter, MESSAGE_KEY_VIEW);
  if (view && tuple_int(view) >= VIEW_AUTO && tuple_int(view) <= VIEW_SOUTH) {
    s_prefs.view = (uint8_t)tuple_int(view);
    changed = true;
  }
  if (hours && tuple_int(hours) >= 1 && tuple_int(hours) <= 24) {
    s_prefs.roll_hours = (uint8_t)tuple_int(hours);
    changed = true;
  }
  return changed;
}

static void apply_prefs(void);

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *lat = dict_find(iter, MESSAGE_KEY_LAT);
  Tuple *lon = dict_find(iter, MESSAGE_KEY_LON);
  if (lat && lon) {
    s_settings.lat = lat->value->int32 / 10000.0;
    s_settings.lon = lon->value->int32 / 10000.0;
    persist_write_int(PERSIST_LAT, lat->value->int32);
    persist_write_int(PERSIST_LON, lon->value->int32);
    update_auto_view();
  }
  const uint32_t tle_keys[SAT_COUNT] = {MESSAGE_KEY_TLE_LINES, MESSAGE_KEY_TLE_CSS, MESSAGE_KEY_TLE_HST};
  for (int sat = 0; sat < SAT_COUNT; sat++) {
    Tuple *tle = dict_find(iter, tle_keys[sat]);
    if (!tle) continue;
    load_tle(sat, tle->value->cstring);
    persist_write_string(s_tle_persist[sat], tle->value->cstring);
  }
  Tuple *temp = dict_find(iter, MESSAGE_KEY_TEMP);
  Tuple *code = dict_find(iter, MESSAGE_KEY_WCODE);
  if (temp && code) {
    s_settings.temp = temp->value->int32;
    s_settings.weather_code = code->value->int32;
    persist_write_int(PERSIST_TEMP, temp->value->int32);
    persist_write_int(PERSIST_WCODE, code->value->int32);
  }
  if (read_prefs(iter)) {
    persist_write_data(PERSIST_PREFS, &s_prefs, sizeof(s_prefs));
    apply_prefs();
  }
  refresh();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  refresh();
  if (++s_minutes_since_request >= REQUEST_EVERY_MINUTES) request_refresh();
}

// A tap or flick of the wrist rolls the sky s_prefs.roll_hours ahead, easing in
// and out, holds there for a moment, then slides quickly back to now.
static int roll_forward_ms(void) { return s_prefs.roll_tenths * 100 * 9 / 14; }
static int roll_hold_ms(void) { return s_prefs.roll_tenths * 100 - roll_forward_ms(); }

static void roll_update(Animation *animation, const AnimationProgress progress) {
  double forward_ms = roll_forward_ms(), hold_ms = roll_hold_ms();
  double ms = (double)progress / ANIMATION_NORMALIZED_MAX * (forward_ms + hold_ms + ROLL_BACK_MS);
  double ahead;
  if (ms < forward_ms) {
    double t = ms / forward_ms;
    ahead = t * t * (3.0 - 2.0 * t);
  } else if (ms < forward_ms + hold_ms) {
    ahead = 1.0;
  } else {
    double t = 1.0 - (ms - forward_ms - hold_ms) / ROLL_BACK_MS;
    ahead = t * t;
  }
  s_offset = (time_t)(ahead * s_prefs.roll_hours * 3600);
  refresh();
}

static void roll_teardown(Animation *animation) {
  s_roll = NULL;
  s_offset = 0;
  refresh();
}

static const AnimationImplementation s_roll_impl = {.update = roll_update, .teardown = roll_teardown};

static void start_roll(void) {
  if (s_roll) return;
  s_roll = animation_create();
  if (!s_roll) return;
  animation_set_implementation(s_roll, &s_roll_impl);
  animation_set_duration(s_roll, roll_forward_ms() + roll_hold_ms() + ROLL_BACK_MS);
  animation_set_curve(s_roll, AnimationCurveLinear);
  animation_schedule(s_roll);
}

static void tap_handler(AccelAxisType axis, int32_t direction) { start_roll(); }

static bool shows_readout(int kind) {
  for (int slot = 0; slot < READOUT_SLOTS; slot++) {
    if (s_prefs.readout[slot] == kind) return true;
  }
  return false;
}

static void update_health(void) {
  time_t now = time(NULL);
  s_steps = -1;
  if (health_service_metric_accessible(HealthMetricStepCount, time_start_of_today(), now) &
      HealthServiceAccessibilityMaskAvailable) {
    s_steps = (int)health_service_sum_today(HealthMetricStepCount);
  }
  s_heart_bpm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
}

static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventMovementUpdate || event == HealthEventHeartRateUpdate ||
      event == HealthEventSignificantUpdate) {
    update_health();
    layer_mark_dirty(s_canvas);
  }
}

// Listens only for what the settings use: the tap service and health
// tracking stay off unless something on the face needs them. The touch
// screen is never used: keeping it powered costs more than the roll is worth.
static void apply_prefs(void) {
  static bool rolling, health;
  if (s_prefs.roll != rolling) {
    rolling = s_prefs.roll;
    if (rolling) accel_tap_service_subscribe(tap_handler);
    else accel_tap_service_unsubscribe();
  }
  bool wants_health = shows_readout(READOUT_HEART) || shows_readout(READOUT_STEPS);
  if (wants_health != health) {
    health = wants_health;
    if (health) {
      health_service_events_subscribe(health_handler, NULL);
      update_health();
    } else {
      health_service_events_unsubscribe();
    }
  }
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
  if (persist_exists(PERSIST_AUTO_SOUTH)) s_auto_south = persist_read_bool(PERSIST_AUTO_SOUTH);
  update_auto_view();
  for (int sat = 0; sat < SAT_COUNT; sat++) {
    char buffer[160];
    if (persist_exists(s_tle_persist[sat]) &&
        persist_read_string(s_tle_persist[sat], buffer, sizeof(buffer)) > 0) {
      load_tle(sat, buffer);
    }
  }
  if (persist_exists(PERSIST_TEMP) && persist_exists(PERSIST_WCODE)) {
    s_settings.temp = persist_read_int(PERSIST_TEMP);
    s_settings.weather_code = persist_read_int(PERSIST_WCODE);
  }
  // A save from an older version is shorter; the fields it lacks keep their defaults.
  int saved = persist_get_size(PERSIST_PREFS);
  if (saved > 0 && saved <= (int)sizeof(s_prefs)) persist_read_data(PERSIST_PREFS, &s_prefs, saved);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) { layer_destroy(s_canvas); }

static void init(void) {
  s_font_time = fonts_load_custom_font(resource_get_handle(CLOCK_FONT));
  s_font_small = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BRUNO_ACE_21));
  s_font_date = fonts_load_custom_font(resource_get_handle(DATE_FONT));
  s_font_glyph = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_NOTO_16));

  load_settings();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){.load = window_load, .unload = window_unload});
  window_stack_push(s_window, true);

  BatteryChargeState battery = battery_state_service_peek();
  s_battery = battery.charge_percent;
  s_charging = battery.is_charging;
  refresh();

  battery_state_service_subscribe(battery_handler);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  apply_prefs();
  app_message_register_inbox_received(inbox_received);
  app_message_open(1024, 32);
}

static void deinit(void) {
  s_prefs.roll = false;
  s_prefs.readout[0] = s_prefs.readout[1] = READOUT_NONE;
  apply_prefs();
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_window);
  fonts_unload_custom_font(s_font_time);
  fonts_unload_custom_font(s_font_small);
  fonts_unload_custom_font(s_font_date);
  fonts_unload_custom_font(s_font_glyph);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
