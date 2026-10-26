// Phone side of the Nadir watchface: finds where you are, fetches the ISS's
// orbital elements and the current weather, and hands them to the watch.
// The watch does all the sky maths itself, so it keeps working offline.

var TLE_URL = 'https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=tle';
var TLE_MAX_AGE_MS = 6 * 60 * 60 * 1000;

function fetchText(url, done) {
  var request = new XMLHttpRequest();
  request.onload = function () {
    done(request.status === 200 ? request.responseText : null);
  };
  request.onerror = function () {
    done(null);
  };
  request.timeout = 20000;
  request.ontimeout = function () {
    done(null);
  };
  request.open('GET', url);
  request.send();
}

function locate(done) {
  navigator.geolocation.getCurrentPosition(
    function (position) {
      done(position.coords.latitude, position.coords.longitude);
    },
    function () {
      done(null, null);
    },
    { timeout: 15000, maximumAge: 10 * 60 * 1000 }
  );
}

function fetchTle(done) {
  var fetchedAt = Number(localStorage.getItem('tle_fetched_at') || 0);
  var cached = localStorage.getItem('tle_lines');
  if (cached && Date.now() - fetchedAt < TLE_MAX_AGE_MS) {
    done(cached);
    return;
  }
  fetchText(TLE_URL, function (text) {
    if (text) {
      var lines = text
        .split(/\r?\n/)
        .map(function (line) {
          return line.trim();
        })
        .filter(function (line) {
          return line.length > 0;
        });
      var first = lines.length >= 3 ? 1 : 0;
      var valid =
        lines[first] && lines[first + 1] && lines[first].charAt(0) === '1' && lines[first + 1].charAt(0) === '2';
      if (valid) {
        cached = lines[first] + '\n' + lines[first + 1];
        localStorage.setItem('tle_lines', cached);
        localStorage.setItem('tle_fetched_at', String(Date.now()));
      }
    }
    done(cached || null);
  });
}

function fetchWeather(lat, lon, done) {
  var url =
    'https://api.open-meteo.com/v1/forecast?latitude=' +
    lat.toFixed(3) +
    '&longitude=' +
    lon.toFixed(3) +
    '&current=temperature_2m,weather_code';
  fetchText(url, function (text) {
    try {
      var current = JSON.parse(text).current;
      done(Math.round(current.temperature_2m), current.weather_code);
    } catch (error) {
      done(null, null);
    }
  });
}

var refreshing = false;

function refresh() {
  if (refreshing) return;
  refreshing = true;
  locate(function (lat, lon) {
    var message = {};
    if (lat !== null) {
      message.LAT = Math.round(lat * 10000);
      message.LON = Math.round(lon * 10000);
    }
    fetchTle(function (tle) {
      if (tle) message.TLE_LINES = tle;
      var finish = function () {
        refreshing = false;
        if (Object.keys(message).length === 0) return;
        Pebble.sendAppMessage(message, null, function () {
          console.log('Nadir: the watch did not accept the update');
        });
      };
      if (lat === null) {
        finish();
        return;
      }
      fetchWeather(lat, lon, function (temp, code) {
        if (temp !== null) {
          message.TEMP = temp;
          message.WCODE = code;
        }
        finish();
      });
    });
  });
}

Pebble.addEventListener('ready', refresh);
Pebble.addEventListener('appmessage', refresh);
