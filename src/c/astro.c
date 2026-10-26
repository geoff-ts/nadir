#include "astro.h"

#include <string.h>

#define PI 3.14159265358979323846
#define DEG (PI / 180.0)
#define J2000 2451545.0

// No libm on the watch. Its code reaches its constant tables through absolute
// addresses, and the Pebble loader relocates only .data, so on the real watch
// those reads land outside the app: fmod crashed the face at launch, and
// atan2 gave wrong answers even in the emulator (2026-10-26). Everything the
// sky maths needs is done here instead; src/c has no #include <math.h>, so a
// libm call cannot creep back in (the build treats the warning as an error).
#define TWO_PI_HI 6.28318530717958623200
#define TWO_PI_LO 2.44929359829470635445e-16

double astro_abs(double v) { return v < 0 ? -v : v; }

double astro_remainder(double x, double y) { return x - (double)(long long)(x / y) * y; }

// Reduced to [-pi/2, pi/2], then the Taylor series to x^17: error under 1e-13.
double astro_sin(double x) {
  double turns = (double)(long long)(x / TWO_PI_HI + (x >= 0 ? 0.5 : -0.5));
  double r = (x - turns * TWO_PI_HI) - turns * TWO_PI_LO;
  if (r > PI / 2) r = PI - r;
  else if (r < -PI / 2) r = -PI - r;
  double r2 = r * r;
  return r * (1 + r2 * (-1.0 / 6 + r2 * (1.0 / 120 + r2 * (-1.0 / 5040 + r2 * (1.0 / 362880 +
         r2 * (-1.0 / 39916800 + r2 * (1.0 / 6227020800.0 + r2 * (-1.0 / 1307674368000.0 +
         r2 * (1.0 / 355687428096000.0)))))))));
}

double astro_cos(double x) { return astro_sin(x + PI / 2); }

// Scaled by powers of four into [0.25, 4], then Newton's method.
double astro_sqrt(double x) {
  if (x <= 0) return 0;
  double scale = 1.0;
  while (x > 4.0) { x *= 0.25; scale *= 2.0; }
  while (x < 0.25) { x *= 4.0; scale *= 0.5; }
  double root = 1.0;
  for (int i = 0; i < 7; i++) root = 0.5 * (root + x / root);
  return root * scale;
}

static double wrap360(double d) {
  d = astro_remainder(d, 360.0);
  return d < 0 ? d + 360.0 : d;
}

// The watch toolchain's libm returns wrong values from atan2 for arguments
// between about 0.44 and 1.19, so the few inverse trig calls this file needs
// are done here. Error is under 1e-5 radian, far below what a watch can show.
static double atan_unit(double z) {
  double z2 = z * z;
  return z * (0.9998660 + z2 * (-0.3302995 + z2 * (0.1801410 + z2 * (-0.0851330 + z2 * 0.0208351))));
}

static double atan2_rad(double y, double x) {
  double ax = astro_abs(x), ay = astro_abs(y);
  if (ax == 0.0 && ay == 0.0) return 0.0;
  bool steep = ay > ax;
  double angle = atan_unit(steep ? ax / ay : ay / ax);
  if (steep) angle = PI / 2 - angle;
  if (x < 0) angle = PI - angle;
  return y < 0 ? -angle : angle;
}

double astro_jd(time_t unix_seconds) {
  return (double)unix_seconds / 86400.0 + 2440587.5;
}

static double centuries(double jd) { return (jd - J2000) / 36525.0; }

double astro_precession_deg(double jd) {
  double t = centuries(jd);
  return (5028.796195 * t + 1.1054348 * t * t) / 3600.0;
}

// JPL "Approximate Positions of the Planets", Table 1 (1800-2050), elements
// referred to the J2000 ecliptic: a, e, I, L, long.peri., long.node. then rates.
typedef struct {
  double v[6];
  double rate[6];
} Elements;

static const Elements EARTH_MOON_BARYCENTRE = {
    {1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0},
    {0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0}};

static const Elements PLANETS[] = {
    // Mercury
    {{0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593},
     {0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081}},
    // Venus
    {{0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255},
     {0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418}},
    // Mars
    {{1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891},
     {0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343}},
    // Jupiter
    {{5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909},
     {-0.00011607, -0.00013253, -0.00183714, 3034.74612775, 0.21252668, 0.20469106}},
    // Saturn
    {{9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448},
     {-0.00125060, -0.00050991, 0.00193609, 1222.49362201, -0.41897216, -0.28867794}},
};

// Heliocentric J2000 ecliptic position, au.
static void heliocentric(const Elements *el, double t, double *x, double *y) {
  double a = el->v[0] + el->rate[0] * t;
  double e = el->v[1] + el->rate[1] * t;
  double inc = (el->v[2] + el->rate[2] * t) * DEG;
  double mean_long = el->v[3] + el->rate[3] * t;
  double peri_long = el->v[4] + el->rate[4] * t;
  double node = (el->v[5] + el->rate[5] * t) * DEG;
  double argp = peri_long * DEG - node;
  double m = astro_remainder((mean_long - peri_long) * DEG, 2 * PI);

  double ecc_anom = m + e * astro_sin(m);
  for (int i = 0; i < 8; i++) {
    ecc_anom -= (ecc_anom - e * astro_sin(ecc_anom) - m) / (1 - e * astro_cos(ecc_anom));
  }
  double xp = a * (astro_cos(ecc_anom) - e);
  double yp = a * astro_sqrt(1 - e * e) * astro_sin(ecc_anom);

  double cw = astro_cos(argp), sw = astro_sin(argp), cn = astro_cos(node), sn = astro_sin(node);
  double ci = astro_cos(inc);
  *x = (cw * cn - sw * sn * ci) * xp + (-sw * cn - cw * sn * ci) * yp;
  *y = (cw * sn + sw * cn * ci) * xp + (-sw * sn + cw * cn * ci) * yp;
}

// Meeus ch. 47, the largest longitude terms only (about 0.3 degree).
static double moon_longitude(double t) {
  double lp = 218.3164477 + 481267.88123421 * t;
  double d = (297.8501921 + 445267.1114034 * t) * DEG;
  double m = (357.5291092 + 35999.0502909 * t) * DEG;
  double mp = (134.9633964 + 477198.8675055 * t) * DEG;
  double f = (93.2720950 + 483202.0175233 * t) * DEG;
  return wrap360(lp + 6.288774 * astro_sin(mp) + 1.274027 * astro_sin(2 * d - mp) +
                 0.658314 * astro_sin(2 * d) + 0.213618 * astro_sin(2 * mp) -
                 0.185116 * astro_sin(m) - 0.114332 * astro_sin(2 * f) +
                 0.058793 * astro_sin(2 * d - 2 * mp) + 0.057066 * astro_sin(2 * d - m - mp) +
                 0.053322 * astro_sin(2 * d + mp) + 0.045758 * astro_sin(2 * d - m) -
                 0.040923 * astro_sin(m - mp) - 0.034720 * astro_sin(d) -
                 0.030383 * astro_sin(m + mp));
}

void astro_ecliptic_lon(double jd, double out_deg[BODY_COUNT]) {
  double t = centuries(jd);
  double precession = astro_precession_deg(jd);
  double ex, ey;
  heliocentric(&EARTH_MOON_BARYCENTRE, t, &ex, &ey);

  out_deg[BODY_SUN] = wrap360(atan2_rad(-ey, -ex) / DEG + precession);
  out_deg[BODY_MOON] = moon_longitude(t);
  for (int i = 0; i < 5; i++) {
    double px, py;
    heliocentric(&PLANETS[i], t, &px, &py);
    out_deg[BODY_MERCURY + i] = wrap360(atan2_rad(py - ey, px - ex) / DEG + precession);
  }
}

double astro_lst_deg(double jd, double lon_east_deg) {
  double t = centuries(jd);
  double gmst = 280.46061837 + 360.98564736629 * (jd - J2000) + 0.000387933 * t * t;
  return wrap360(gmst + lon_east_deg);
}

double astro_ascendant_deg(double jd, double lst_deg, double lat_deg) {
  double eps = (23.4392911 - 0.0130042 * centuries(jd)) * DEG;
  double theta = lst_deg * DEG;
  double lat = lat_deg * DEG;
  return wrap360(atan2_rad(astro_cos(theta) * astro_cos(lat), -(astro_sin(theta) * astro_cos(eps) * astro_cos(lat) + astro_sin(lat) * astro_sin(eps))) / DEG);
}

// Fixed-width numeric column of a TLE (1-based, inclusive). Hand-rolled
// because strtod drags the C library's file I/O into the watch binary.
static double field(const char *line, int first, int last) {
  double value = 0.0, scale = 1.0;
  bool negative = false, fraction = false;
  for (int i = first - 1; i < last; i++) {
    char c = line[i];
    if (c == '-') {
      negative = true;
    } else if (c == '.') {
      fraction = true;
    } else if (c >= '0' && c <= '9') {
      if (fraction) {
        scale /= 10.0;
        value += (c - '0') * scale;
      } else {
        value = value * 10.0 + (c - '0');
      }
    }
  }
  return negative ? -value : value;
}

static double days_before_year(int year) {
  int y = year - 1;
  int leaps = (y / 4 - y / 100 + y / 400) - (1999 / 4 - 1999 / 100 + 1999 / 400);
  return 365.0 * (year - 2000) + leaps;
}

bool astro_tle_parse(const char *line1, const char *line2, AstroTle *out) {
  if (strlen(line1) < 63 || strlen(line2) < 63) return false;
  if (line1[0] != '1' || line2[0] != '2') return false;

  int yy = (int)field(line1, 19, 20);
  int year = yy < 57 ? 2000 + yy : 1900 + yy;
  double day_of_year = field(line1, 21, 32);
  out->epoch_jd = 2451544.5 + days_before_year(year) + (day_of_year - 1.0);

  out->inc = field(line2, 9, 16) * DEG;
  out->raan = field(line2, 18, 25) * DEG;
  out->ecc = field(line2, 27, 33) * 1e-7;
  out->argp = field(line2, 35, 42) * DEG;
  out->mean_anomaly = field(line2, 44, 51) * DEG;
  out->n = field(line2, 53, 63) * 2 * PI / 86400.0;
  return out->n > 0 && out->ecc >= 0 && out->ecc < 0.5;
}

void astro_sat_altaz(const AstroTle *tle, double jd, double lat_deg,
                     double lon_east_deg, double *el_deg, double *az_deg) {
  const double mu = 398600.4418;
  const double re = 6378.137;
  const double j2 = 1.08262668e-3;
  const double flattening = 1.0 / 298.257223563;

  double dt = (jd - tle->epoch_jd) * 86400.0;
  double cubed = mu / (tle->n * tle->n);
  double a = 7000.0;
  for (int i = 0; i < 8; i++) a = (2.0 * a + cubed / (a * a)) / 3.0;
  double p = a * (1 - tle->ecc * tle->ecc);
  double factor = 1.5 * j2 * (re / p) * (re / p) * tle->n;
  double ci = astro_cos(tle->inc), si = astro_sin(tle->inc);
  double raan = tle->raan - factor * ci * dt;
  double argp = tle->argp + 0.5 * factor * (5 * ci * ci - 1) * dt;
  double m = astro_remainder(tle->mean_anomaly + tle->n * dt, 2 * PI);

  double e = tle->ecc;
  double ecc_anom = m + e * astro_sin(m);
  for (int i = 0; i < 8; i++) {
    ecc_anom -= (ecc_anom - e * astro_sin(ecc_anom) - m) / (1 - e * astro_cos(ecc_anom));
  }
  double xp = a * (astro_cos(ecc_anom) - e);
  double yp = a * astro_sqrt(1 - e * e) * astro_sin(ecc_anom);

  double cw = astro_cos(argp), sw = astro_sin(argp), cn = astro_cos(raan), sn = astro_sin(raan);
  double sx = (cw * cn - sw * sn * ci) * xp + (-sw * cn - cw * sn * ci) * yp;
  double sy = (cw * sn + sw * cn * ci) * xp + (-sw * sn + cw * cn * ci) * yp;
  double sz = (sw * si) * xp + (cw * si) * yp;

  double lat = lat_deg * DEG;
  double theta = astro_lst_deg(jd, lon_east_deg) * DEG;
  double c = 1.0 / astro_sqrt(1 + flattening * (flattening - 2) * astro_sin(lat) * astro_sin(lat));
  double s = (1 - flattening) * (1 - flattening) * c;
  double ox = re * c * astro_cos(lat) * astro_cos(theta);
  double oy = re * c * astro_cos(lat) * astro_sin(theta);
  double oz = re * s * astro_sin(lat);

  double rx = sx - ox, ry = sy - oy, rz = sz - oz;
  double south = astro_sin(lat) * astro_cos(theta) * rx + astro_sin(lat) * astro_sin(theta) * ry - astro_cos(lat) * rz;
  double east = -astro_sin(theta) * rx + astro_cos(theta) * ry;
  double up = astro_cos(lat) * astro_cos(theta) * rx + astro_cos(lat) * astro_sin(theta) * ry + astro_sin(lat) * rz;
  
  *el_deg = atan2_rad(up, astro_sqrt(east * east + south * south)) / DEG;
  *az_deg = wrap360(atan2_rad(east, -south) / DEG);
}
