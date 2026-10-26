#pragma once
// Low-precision sky maths for the watchface: ecliptic longitudes of the Sun,
// Moon and naked-eye planets, the ascendant, and a simple ISS propagator.
// No Pebble dependencies, so the same file is tested natively on a desktop.
#include <stdbool.h>
#include <time.h>

typedef enum {
  BODY_SUN,
  BODY_MOON,
  BODY_MERCURY,
  BODY_VENUS,
  BODY_MARS,
  BODY_JUPITER,
  BODY_SATURN,
  BODY_COUNT
} Body;

// Mean elements from a two-line element set. Angles in radians, n in rad/s.
typedef struct {
  double epoch_jd;
  double inc;
  double raan;
  double ecc;
  double argp;
  double mean_anomaly;
  double n;
} AstroTle;

// Stand-ins for libm, which cannot be used on the watch (see astro.c).
double astro_abs(double v);
double astro_remainder(double x, double y);  // fmod: x - n*y, n = x/y toward zero
double astro_sin(double x);
double astro_cos(double x);
double astro_sqrt(double x);

double astro_jd(time_t unix_seconds);

// Ecliptic longitude of date, degrees 0..360, for every Body.
void astro_ecliptic_lon(double jd, double out_deg[BODY_COUNT]);

// Degrees to add to a J2000 ecliptic longitude to get the longitude of date.
double astro_precession_deg(double jd);

double astro_lst_deg(double jd, double lon_east_deg);

// Ecliptic longitude of the point of the ecliptic that is rising, degrees.
double astro_ascendant_deg(double jd, double lst_deg, double lat_deg);

bool astro_tle_parse(const char *line1, const char *line2, AstroTle *out);

// Elevation and azimuth (from north, through east) of the satellite.
void astro_sat_altaz(const AstroTle *tle, double jd, double lat_deg,
                     double lon_east_deg, double *el_deg, double *az_deg);
