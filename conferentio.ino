#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
using fs::FS;
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

// ================== CONFIG (DEFAULTS) ==================
const char* DEFAULT_WIFI_SSID1 = "Network SSID";
const char* DEFAULT_WIFI_PASS1 = "Password";

const char* DEFAULT_WIFI_SSID2 = "Network SSID";
const char* DEFAULT_WIFI_PASS2 = "Password";

const char* DEFAULT_ICS_URL =
  "https://calendar.google.com/calendar/...";

const char* DEFAULT_ROOM_NAME = "ROOM name A-101 ";
const char* TZ_EUROPE_WARSAW  = "CET-1CEST,M3.5.0/2,M10.5.0/3";

// ================== WEB AUTH ==================
const char* DEFAULT_WEB_USER = "admin";
const char* DEFAULT_WEB_PASS = "Conferentio";  

// ================== GLOBALS ==================
TFT_eSPI tft = TFT_eSPI();
WebServer web(80);
Preferences prefs;

bool isConfigured = false;   // flaga in NVS: is the device configured?
bool setupMode    = false;   // mode:  AP/SETUP
String apSsid;               // SSID AP in setup mode
const char* AP_PASS = "roomsetup";  // AP pass in setup mode

// ---------- COLOR THEME (light) ----------
const uint16_t COLOR_BG             = TFT_WHITE;
const uint16_t COLOR_TEXT           = TFT_NAVY;
const uint16_t COLOR_HEADER         = TFT_NAVY;
const uint16_t COLOR_SUBHEADER      = TFT_DARKGREY;
const uint16_t COLOR_ACCENT         = TFT_BLUE;
const uint16_t COLOR_FREE           = TFT_DARKGREEN;
const uint16_t COLOR_BUSY           = TFT_RED;
const uint16_t COLOR_TIMEBAR        = TFT_DARKGREY;
const uint16_t COLOR_BUTTON_BG      = TFT_WHITE;
const uint16_t COLOR_BUTTON_BORDER  = TFT_DARKGREY;
const uint16_t COLOR_BUTTON_TEXT    = TFT_NAVY;
const uint16_t COLOR_ERROR_TEXT     = TFT_RED;

// ================== LAYOUT ==================
const int TIME_BAR_H    = 16;
const int IP_BAR_H      = 18;

const int HEADER_Y      = TIME_BAR_H + 6;     // 22 px
const int STATUS_Y      = HEADER_Y + 28;      // 50 px

const int OCCUPY_BAR_Y  = STATUS_Y + 40;      // 90 px
const int OCCUPY_BAR_H  = 8;

const int LIST_TOP      = OCCUPY_BAR_Y + OCCUPY_BAR_H + 6;  // 104 px

const int BUTTON_BAR_H  = 34;                 // Height of Navigation Buttons

// ================== RUNTIME CONFIG ==================
struct DeviceConfig {
  String wifi1;
  String pass1;
  String wifi2;
  String pass2;
  String icsUrl;
  String roomName;
  String webUser; 
  String webPass;
};

DeviceConfig cfg;

// ================== DATA ==================
struct Event {
  time_t start;
  time_t end;
  String summary;
  String organizer;
};

const int MAX_EVENTS = 64;
Event events[MAX_EVENTS];
int eventCount = 0;

String lastError = "";

// view / navigation
int dayOffset = 0;           // -1 = yesterday, 0 = today, 1 = tomorrow...
int scrollIndex = 0;         // index of first visible row in list
unsigned long lastRefresh = 0;


// ================== CONFIG STORAGE ==================
void loadConfig() {
  prefs.begin("crdisp", true); // read-only

  cfg.wifi1    = prefs.getString("wifi1", "");
  cfg.pass1    = prefs.getString("pass1", "");
  cfg.wifi2    = prefs.getString("wifi2", "");
  cfg.pass2    = prefs.getString("pass2", "");
  cfg.icsUrl   = prefs.getString("icsUrl", DEFAULT_ICS_URL);
  cfg.roomName = prefs.getString("room",  DEFAULT_ROOM_NAME);

  isConfigured = prefs.getBool("configured", false);

  if (!isConfigured && cfg.wifi1.length() > 0) {
    isConfigured = true;
  }
  cfg.webUser = prefs.getString("webUser", DEFAULT_WEB_USER);
  cfg.webPass = prefs.getString("webPass", DEFAULT_WEB_PASS);
  prefs.end();

  Serial.println("Config loaded:");
  Serial.println("  wifi1=" + cfg.wifi1);
  Serial.println("  wifi2=" + cfg.wifi2);
  Serial.println("  room =" + cfg.roomName);
  Serial.printf("  configured=%d\n", isConfigured);
}

void saveConfig() {
  prefs.begin("crdisp", false); // write
  prefs.putString("wifi1",  cfg.wifi1);
  prefs.putString("pass1",  cfg.pass1);
  prefs.putString("wifi2",  cfg.wifi2);
  prefs.putString("pass2",  cfg.pass2);
  prefs.putString("icsUrl", cfg.icsUrl);
  prefs.putString("room",   cfg.roomName);
  prefs.putBool("configured", true);
  prefs.putString("webUser", cfg.webUser);
  prefs.putString("webPass", cfg.webPass);
  prefs.end();

  isConfigured = true;

  Serial.println("Config saved.");
}

// ================== HELPERS ==================
String currentIpString() {
  if (setupMode) {
    return WiFi.softAPIP().toString();
  } else {
    return WiFi.localIP().toString();
  }
}

String trimLine(const String& s) {
  int a = 0, b = s.length() - 1;
  while (a <= b && (s[a] <= 32)) a++;
  while (b >= a && (s[b] <= 32)) b--;
  return (a > b ? "" : s.substring(a, b + 1));
}

// Unfold ICS lines in-place (remove CRLF + space continuation)
void unfoldICS(String &txt) {
  txt.replace("\r\n ", "");
  txt.replace("\n ", "");
}

bool startsWithIgnoreCase(const String& line, const char* prefix) {
  int n = strlen(prefix);
  if (line.length() < n) return false;
  for (int i = 0; i < n; i++)
    if (tolower(line[i]) != tolower(prefix[i])) return false;
  return true;
}

String getIcsValue(const String& line) {
  int c = line.indexOf(':');
  if (c < 0) return "";
  return line.substring(c + 1);
}

// Shorten text and append ellipsis
String shorten(const String& s, int maxLen) {
  if ((int)s.length() <= maxLen) return s;
  if (maxLen <= 1) return s.substring(0, maxLen);
  return s.substring(0, maxLen - 1) + "...";
}

// Fit text into given pixel width using TFT_eSPI textWidth()
String fitTextToWidth(const String& s, int maxWidth, int font) {
  if (maxWidth <= 0) return "";
  String out = s;

  for (int i = 0; i < 64 && out.length() > 0; i++) {
    if (tft.textWidth(out, font) <= maxWidth) break;

    out.remove(out.length() - 1);
    if (out.length() > 1) {
      out.remove(out.length() - 1);
      out += "...";
    }
  }
  return out;
}

// ================== DATE PARSER ==================
bool parseIcsDateTime(const String& line, time_t& out) {
  String v = line;

  int valPos = v.indexOf("VALUE=DATE");
  if (valPos >= 0) {
    int p = v.indexOf(':', valPos);
    if (p < 0) return false;
    String d = v.substring(p + 1);
    if (d.length() < 8) return false;

    int y  = d.substring(0, 4).toInt();
    int m  = d.substring(4, 6).toInt();
    int di = d.substring(6, 8).toInt();

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon  = m - 1;
    t.tm_mday = di;
    t.tm_hour = 0;
    t.tm_min  = 0;
    t.tm_sec  = 0;

    out = mktime(&t);
    return true;
  }

  int tzPos = v.indexOf("TZID=");
  if (tzPos >= 0) {
    int p = v.indexOf(':', tzPos);
    if (p < 0) return false;
    String ts = v.substring(p + 1);
    if (ts.length() < 8) return false;

    int y  = ts.substring(0, 4).toInt();
    int m  = ts.substring(4, 6).toInt();
    int di = ts.substring(6, 8).toInt();
    int H  = 0, M = 0, S = 0;
    if (ts.length() >= 15) {
      H  = ts.substring(9, 11).toInt();
      M  = ts.substring(11, 13).toInt();
      S  = ts.substring(13, 15).toInt();
    }

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon  = m - 1;
    t.tm_mday = di;
    t.tm_hour = H;
    t.tm_min  = M;
    t.tm_sec  = S;

    out = mktime(&t);
    return true;
  }

  int p = v.lastIndexOf(':');
  if (p >= 0) {
    v = v.substring(p + 1);
  }

  bool isUtc = v.endsWith("Z");
  if (isUtc) {
    v.remove(v.length() - 1);
  }

  if (v.length() < 8) return false;

  int y  = v.substring(0, 4).toInt();
  int m  = v.substring(4, 6).toInt();
  int di = v.substring(6, 8).toInt();
  int H  = 0, M = 0, S = 0;

  if (v.length() >= 15) {
    H = v.substring(9, 11).toInt();
    M = v.substring(11, 13).toInt();
    S = v.substring(13, 15).toInt();
  }

  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = y - 1900;
  t.tm_mon  = m - 1;
  t.tm_mday = di;
  t.tm_hour = H;
  t.tm_min  = M;
  t.tm_sec  = S;

  time_t ts = mktime(&t);

  if (isUtc) {
    time_t now; time(&now);
    struct tm gm, loc;
    gmtime_r(&now, &gm);
    localtime_r(&now, &loc);
    time_t utcNow = mktime(&gm);
    time_t locNow = mktime(&loc);
    ts += (locNow - utcNow);
  }

  out = ts;
  return true;
}

// ================== ICS PARSER ==================
int parseICS(const String& txt) {
  Serial.printf("parseICS: txt.length = %d\n", txt.length());

  time_t now;
  time(&now);
  bool timeOk = (now > 1700000000L);

  time_t from = 0;

  if (timeOk) {
    struct tm d;
    localtime_r(&now, &d);
    d.tm_hour = 0;
    d.tm_min  = 0;
    d.tm_sec  = 0;
    from = mktime(&d);

    char bufFrom[32];
    strftime(bufFrom, sizeof(bufFrom), "%Y-%m-%d %H:%M", &d);
    Serial.printf("parseICS: time OK, keeping events from >= %ld (%s)\n",
                  (long)from, bufFrom);
  } else {
    Serial.println("parseICS: WARNING - system time invalid, no date filter");
  }

  int cnt = 0;
  bool inEvent = false;
  time_t st = 0, en = 0;
  String sum = "";
  String org = "";

  bool haveMinMax = false;
  time_t minSt = 0, maxSt = 0;

  int pos = 0;
  while (pos < txt.length()) {
    int n = txt.indexOf('\n', pos);
    if (n < 0) n = txt.length();
    String line = trimLine(txt.substring(pos, n));
    pos = n + 1;

    if (line.length() == 0) continue;

    if (startsWithIgnoreCase(line, "BEGIN:VEVENT")) {
      Serial.println(">> BEGIN:VEVENT");
      inEvent = true;
      st = en = 0;
      sum = "";
      org = "";
      continue;
    }

    if (startsWithIgnoreCase(line, "END:VEVENT")) {
      Serial.println("<< END:VEVENT");

      if (inEvent && st != 0) {
        if (!haveMinMax) {
          haveMinMax = true;
          minSt = maxSt = st;
        } else {
          if (st < minSt) minSt = st;
          if (st > maxSt) maxSt = st;
        }

        bool inRange = true;
        if (timeOk) {
          inRange = (st >= from);
        }

        if (inRange) {
          if (cnt < MAX_EVENTS) {
            if (en == 0) en = st + 1800;

            events[cnt].start     = st;
            events[cnt].end       = en;
            events[cnt].summary   = sum;
            events[cnt].organizer = org;

            Serial.printf("   [EVENT SAVED] #%d start=%ld end=%ld summary='%s'\n",
                          cnt, (long)st, (long)en, sum.c_str());
            cnt++;
          } else {
            Serial.println("   [EVENT SKIPPED] MAX_EVENTS reached");
          }
        } else {
          Serial.printf("   [EVENT SKIPPED] out of range st=%ld\n", (long)st);
        }
      }

      inEvent = false;
      continue;
    }

    if (!inEvent) continue;

    if (startsWithIgnoreCase(line, "DTSTART")) {
      Serial.print("DTSTART line: ");
      Serial.println(line);
      bool ok = parseIcsDateTime(line, st);
      Serial.printf("  -> parseIcsDateTime ok=%d, st=%ld\n", ok, (long)st);
    }
    else if (startsWithIgnoreCase(line, "DTEND")) {
      Serial.print("DTEND line: ");
      Serial.println(line);
      bool ok = parseIcsDateTime(line, en);
      Serial.printf("  -> parseIcsDateTime ok=%d, en=%ld\n", ok, (long)en);
    }
    else if (startsWithIgnoreCase(line, "SUMMARY")) {
      sum = getIcsValue(line);
      Serial.print("SUMMARY: ");
      Serial.println(sum);
    }
    else if (startsWithIgnoreCase(line, "ORGANIZER")) {
      int cnPos = line.indexOf("CN=");
      if (cnPos >= 0) {
        int start = cnPos + 3;
        int end   = line.indexOf(':', start);
        if (end < 0) end = line.length();
        org = line.substring(start, end);
      } else {
        org = getIcsValue(line);
      }
      Serial.print("ORGANIZER: ");
      Serial.println(org);
    }
  }

  for (int i = 0; i < cnt; i++)
    for (int j = i + 1; j < cnt; j++)
      if (events[j].start < events[i].start) {
        Event t = events[i]; events[i] = events[j]; events[j] = t;
      }

  if (haveMinMax) {
    struct tm tmMin, tmMax;
    localtime_r(&minSt, &tmMin);
    localtime_r(&maxSt, &tmMax);
    char bufMin[32], bufMax[32];
    strftime(bufMin, sizeof(bufMin), "%Y-%m-%d %H:%M", &tmMin);
    strftime(bufMax, sizeof(bufMax), "%Y-%m-%d %H:%M", &tmMax);
    Serial.printf("parseICS: min DTSTART = %ld (%s)\n", (long)minSt, bufMin);
    Serial.printf("parseICS: max DTSTART = %ld (%s)\n", (long)maxSt, bufMax);
  } else {
    Serial.println("parseICS: no valid DTSTART found");
  }

  Serial.printf("parseICS: total events = %d\n", cnt);
  for (int i = 0; i < cnt && i < 10; i++) {
    struct tm tt;
    localtime_r(&events[i].start, &tt);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tt);
    Serial.printf("   #%d  %s  %s\n", i, buf, events[i].summary.c_str());
  }

  return cnt;
}

bool isSameDay(time_t ts, time_t dayStart) {
  struct tm a, b;
  localtime_r(&ts, &a);
  localtime_r(&dayStart, &b);
  return (a.tm_year == b.tm_year) && (a.tm_yday == b.tm_yday);
}

time_t getViewDayStart() {
  time_t now; time(&now);
  struct tm base;
  localtime_r(&now, &base);
  base.tm_hour = 0;
  base.tm_min  = 0;
  base.tm_sec  = 0;
  base.tm_mday += dayOffset;
  return mktime(&base);
}

// ================== WiFi helper ==================
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);

  Serial.printf("Connecting to WiFi 1: %s\n", cfg.wifi1.c_str());
  if (cfg.wifi1.length() > 0) {
    WiFi.begin(cfg.wifi1.c_str(), cfg.pass1.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi 1 connected");
      return true;
    }
  }

  Serial.printf("WiFi 1 failed, trying WiFi 2: %s\n", cfg.wifi2.c_str());
  if (cfg.wifi2.length() > 0) {
    WiFi.begin(cfg.wifi2.c_str(), cfg.pass2.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi 2 connected");
      return true;
    }
  }

  Serial.println("WiFi connect failed");
  return false;
}

// ================== ICS DOWNLOAD ==================
bool downloadICS() {
  lastError = "";

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("downloadICS: WiFi disconnected, attempting to reconnect...");
    if (!connectWiFi()) {
      lastError = "WiFi reconnect failed";
      return false;
    }
  }

  WiFiClientSecure cli;
  cli.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  Serial.println("downloadICS: starting HTTP GET...");
  if (!http.begin(cli, cfg.icsUrl)) {
    lastError = "HTTP begin failed";
    Serial.println("downloadICS: http.begin failed");
    return false;
  }

  http.addHeader("Accept-Encoding", "identity");

  int rc = http.GET();
  Serial.printf("downloadICS: http.GET rc=%d (%s)\n", rc, http.errorToString(rc).c_str());

  if (rc != 200) {
    lastError = "HTTP " + String(rc);
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  String data = "";

  uint8_t buf[512];
  while (http.connected() || stream->available()) {
    int len = stream->read(buf, sizeof(buf));
    if (len > 0) {
      data.concat((char*)buf, len);
    }
    delay(1);
  }

  http.end();

  Serial.printf("downloadICS: downloaded %d bytes\n", data.length());
  if (data.indexOf("BEGIN:VEVENT") < 0) {
    lastError = "No VEVENT";
    eventCount = 0;
    return false;
  }

  unfoldICS(data);

  int newCount = parseICS(data);
  eventCount = newCount;

  if (newCount <= 0) {
    lastError = "No events parsed";
    return false;
  }

  return true;
}

// ================== SIMPLE UI HELPERS ==================
void simpleWipe() {
  for (int y = 0; y < tft.height(); y += 24) {
    tft.fillRect(0, y, tft.width(), 24, COLOR_BG);
    delay(5);
  }
}

void drawStatus(const String& msg) {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(2);
  tft.drawCentreString(msg, tft.width()/2, tft.height()/2, 2);
}

// * Draw time/date at the very top (y=0)
void updateTimeBar() {
  int timeBarY = 0;
  int fullW = tft.width();

  // Clear area
  tft.fillRect(0, timeBarY, fullW, TIME_BAR_H, COLOR_BG);

  struct tm lt;
  time_t now2 = time(nullptr);
  localtime_r(&now2, &lt);
  char tb[16], db2[16];

  if (now2 > 1700000000L) {
    strftime(tb, sizeof(tb), "%H:%M", &lt);
    strftime(db2, sizeof(db2), "%d.%m.%Y", &lt);

    // HOUR
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.drawString(tb, 4, timeBarY + 2, 1);

    // EVENTS FOR
    String ev = String("Events for: ") + db2;
    tft.setTextColor(COLOR_SUBHEADER, COLOR_BG);
    tft.drawCentreString(ev, fullW / 2, timeBarY + 2, 1);
  } else {
    tft.setTextColor(COLOR_ERROR_TEXT, COLOR_BG);
    tft.drawCentreString("TIME SYNC FAIL", fullW / 2, timeBarY + 2, 2);
  }
}

// * Draw IP/Error bar at the very bottom
void updateBottomBar() {
  int fullW = tft.width();
  int bottomY = tft.height() - IP_BAR_H;

  // Clear area and draw separator line above it
  tft.fillRect(0, bottomY, fullW, IP_BAR_H, COLOR_BG);
  tft.drawFastHLine(0, bottomY - 1, fullW, COLOR_TIMEBAR);

  // Error Message (Left)
  if (lastError.length() > 0) {
    tft.setTextColor(COLOR_ERROR_TEXT, COLOR_BG);
    String err = shorten("ERR: " + lastError, 15);
    tft.drawString(err, 4, bottomY + 4, 1);
  }

  // Network info (Right)
  if (setupMode) {
    tft.setTextColor(COLOR_SUBHEADER, COLOR_BG);
    String apInfo = String("AP: ") + currentIpString();
    tft.drawRightString(apInfo, fullW - 4, bottomY + 4, 1);
  } else if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(COLOR_SUBHEADER, COLOR_BG);
    String netInfo = WiFi.SSID() + String("  ") + WiFi.localIP().toString();
    tft.drawRightString(netInfo, fullW - 4, bottomY + 4, 1);
  }
}

// ================== MAIN SCREEN LAYOUT ==================
void drawMainScreen() {
  simpleWipe();
  tft.fillScreen(COLOR_BG);
  tft.setTextSize(2);

  int screenH    = tft.height();
  int bottomY    = screenH - IP_BAR_H;
  int btnBottom  = bottomY - 4;
  int btnTop     = btnBottom - BUTTON_BAR_H;
  int listBottom = btnTop - 10;

  // -------- TIME & DATE (TOP BAR) --------
  updateTimeBar();

  // -------- ROOM HEADER --------
  tft.setTextColor(COLOR_HEADER, COLOR_BG);
  String roomText = fitTextToWidth(cfg.roomName, tft.width() - 10, 2);
  tft.drawCentreString(roomText, tft.width()/2, HEADER_Y, 2);

  time_t viewStart = getViewDayStart();
  struct tm vd;
  localtime_r(&viewStart, &vd);
  char dateBuf[32];

  if (time(nullptr) > 1700000000L) {
    strftime(dateBuf, sizeof(dateBuf), "%d.%m.%Y", &vd);
  } else {
    strcpy(dateBuf, "xx.xx.xxxx");
  }

  // -------- FIND EVENTS & OCCUPANCY BAR (Preparation) --------
  int idx[MAX_EVENTS];
  int dayEvents = 0;

  if (time(nullptr) > 1700000000L) {
    for (int i = 0; i < eventCount; i++) {
      if (isSameDay(events[i].start, viewStart)) {
        idx[dayEvents++] = i;
      }
    }
  }

  // -------- STATUS (FREE/BUSY) --------
  bool busy = false;
  time_t freeFrom = 0;
  time_t now = time(nullptr);

  int statusFontId = 2;
  int statusTh     = tft.fontHeight(statusFontId);
  int statusBarH   = statusTh + 6;
  int statusTextY  = STATUS_Y + (statusBarH - statusTh) / 2;

  tft.fillRect(0, STATUS_Y, tft.width(), statusBarH, COLOR_BG);

  if (dayOffset == 0 && now > 1700000000L) {
    for (int k = 0; k < dayEvents; k++) {
      Event &e = events[idx[k]];
      if (now >= e.start && now < e.end) {
        busy = true;
        freeFrom = e.end;
        break;
      }
    }

    if (busy) {
      tft.fillRect(0, STATUS_Y, tft.width(), statusBarH, COLOR_BUSY);
      tft.setTextColor(TFT_WHITE, COLOR_BUSY);

      struct tm tmf;
      localtime_r(&freeFrom, &tmf);
      char b[32];
      strftime(b, sizeof(b), "BUSY / FREE FROM %H:%M", &tmf);

      String txt = fitTextToWidth(String(b), tft.width() - 10, statusFontId);
      tft.drawCentreString(txt, tft.width() / 2, statusTextY, statusFontId);
    } else {
      tft.fillRect(0, STATUS_Y, tft.width(), statusBarH, COLOR_FREE);
      tft.setTextColor(TFT_WHITE, COLOR_FREE);
      tft.drawCentreString("FREE NOW", tft.width() / 2, statusTextY, statusFontId);
    }
  } else {
    tft.fillRect(0, STATUS_Y, tft.width(), statusBarH, COLOR_SUBHEADER);
    tft.setTextColor(TFT_WHITE, COLOR_SUBHEADER);
    tft.drawCentreString("SELECTED DAY", tft.width() / 2, statusTextY, statusFontId);
  }

  // -------- OCCUPANCY BAR --------
  if (dayEvents > 0) {
    int fullW = tft.width();
    tft.fillRect(0, OCCUPY_BAR_Y, fullW, OCCUPY_BAR_H, tft.color565(220,230,240));

    for (int k = 0; k < dayEvents; k++) {
      Event &e = events[idx[k]];
      struct tm ts; localtime_r(&e.start, &ts);
      int sec = ts.tm_hour*3600 + ts.tm_min*60 + ts.tm_sec;
      int dur = e.end - e.start;
      if (dur < 600) dur = 600;

      float xf = sec / 86400.0f * fullW;
      float wf = dur / 86400.0f * fullW;

      int x = (int)xf;
      int w = (int)wf;
      if (w < 2) w = 2;
      if (x + w > fullW) w = fullW - x;

      if (w > 0)
        tft.fillRect(x, OCCUPY_BAR_Y, w, OCCUPY_BAR_H, COLOR_BUSY);
    }
  }

  // -------- LIST AREA --------
  int listFontId = 2;
  int listTh     = tft.fontHeight(listFontId);
  int lineH      = listTh + 6;

  int maxLines = (listBottom - LIST_TOP) / lineH;
  if (maxLines < 1) maxLines = 1;
  if (maxLines > 6) maxLines = 6;

  if (scrollIndex > dayEvents - maxLines)
    scrollIndex = max(0, dayEvents - maxLines);
  if (scrollIndex < 0)
    scrollIndex = 0;

  int y = LIST_TOP;
  tft.setTextColor(COLOR_TEXT, COLOR_BG);

  if (dayEvents == 0) {
    if (eventCount == 0 && lastError.length() > 0) {
      tft.drawCentreString("ICS ERROR/NO DATA", tft.width()/2, y + 10, listFontId);
    } else if (eventCount == 0) {
      tft.drawCentreString("No event data loaded", tft.width()/2, y + 10, listFontId);
    } else if (dayOffset != 0) {
      tft.drawCentreString("No events for this day", tft.width()/2, y + 10, listFontId);
    } else {
      tft.drawCentreString("No events today", tft.width()/2, y + 10, listFontId);
    }
  } else {
    int maxTW = tft.width() - 8;

    for (int l = 0; l < maxLines && (scrollIndex + l) < dayEvents; l++) {
      Event &e = events[idx[scrollIndex + l]];

      struct tm a, b;
      char s1[8], s2[8];
      localtime_r(&e.start, &a);
      localtime_r(&e.end, &b);
      strftime(s1, sizeof(s1), "%H:%M", &a);
      strftime(s2, sizeof(s2), "%H:%M", &b);

      String txt = String(s1) + "-" + String(s2) + " " +
                   shorten(e.summary, 22);

      txt = fitTextToWidth(txt, maxTW, listFontId);
      tft.drawString(txt, 4, y, listFontId);
      y += lineH;
    }

    // ------- STRZAŁKI SCROLLA (▲ ▼) -------
    bool canScrollUp   = (scrollIndex > 0);
    bool canScrollDown = (scrollIndex + maxLines < dayEvents);

    tft.setTextColor(COLOR_SUBHEADER, COLOR_BG);

    int arrowFontId = 1;
    int arrowX      = tft.width() - 8;

    if (canScrollUp) {
      tft.drawString("^", arrowX, LIST_TOP + 2, arrowFontId);
    }
    if (canScrollDown) {
      tft.drawString("v", arrowX, listBottom - lineH + 2, arrowFontId);
    }
  }

  // // -------- BUTTONS --------
  // int fullW = tft.width();
  // int w1 = fullW / 3;
  // int w2 = fullW / 3;
  // int w3 = fullW - w1 - w2;

  // tft.fillRect(0,     btnTop, w1-1, BUTTON_BAR_H, COLOR_BUTTON_BG);
  // tft.fillRect(w1,    btnTop, w2-1, BUTTON_BAR_H, COLOR_BUTTON_BG);
  // tft.fillRect(w1+w2, btnTop, w3,   BUTTON_BAR_H, COLOR_BUTTON_BG);

  // tft.drawRect(0,     btnTop, w1-1, BUTTON_BAR_H, COLOR_BUTTON_BORDER);
  // tft.drawRect(w1,    btnTop, w2-1, BUTTON_BAR_H, COLOR_BUTTON_BORDER);
  // tft.drawRect(w1+w2, btnTop, w3,   BUTTON_BAR_H, COLOR_BUTTON_BORDER);

  // int btnFontId = 2;
  // int btnTh     = tft.fontHeight(btnFontId);
  // int btnTextY  = btnTop + (BUTTON_BAR_H - btnTh) / 2;

  // tft.setTextColor(COLOR_BUTTON_TEXT, COLOR_BUTTON_BG);

  // // PREV „wyszarzany” przy dayOffset <= 0 (Twoja logika)
  // if (dayOffset <= 0) {
  //   tft.setTextColor(COLOR_SUBHEADER, COLOR_BUTTON_BG);
  //   tft.drawCentreString("PREV", w1 / 2, btnTextY, btnFontId);
  //   tft.setTextColor(COLOR_BUTTON_TEXT, COLOR_BUTTON_BG);
  // } else {
  //   tft.drawCentreString("PREV", w1 / 2, btnTextY, btnFontId);
  // }

  // tft.drawCentreString("TODAY", w1 + w2 / 2,      btnTextY, btnFontId);
  // tft.drawCentreString("NEXT",  w1 + w2 + w3 / 2, btnTextY, btnFontId);

  // -------- BOTTOM IP/ERROR BAR --------
  updateBottomBar();
}

// ================== TOUCH HANDLER (UPDATED LAYOUT) ==================
void handleTouch(uint16_t tx, uint16_t ty) {
  int screenH   = tft.height();
  int bottomY   = screenH - IP_BAR_H;
  int btnBottom = bottomY - 4;
  int btnTop    = btnBottom - BUTTON_BAR_H;
  int listBottom = btnTop - 10;

  // // --- Day buttons ---
  // if (ty >= btnTop && ty <= btnBottom) {
  //   int third = tft.width() / 3;

  //   if (tx < third) { // PREV
  //     if (dayOffset <= 0) return; // ignoruj PREV gdy jesteśmy „tu lub wcześniej”
  //     dayOffset--;
  //     scrollIndex = 0;
  //     drawStatus("Prev day...");
  //   } else if (tx < 2 * third) { // TODAY
  //     if (dayOffset == 0) return; // ignoruj gdy już TODAY
  //     dayOffset = 0;
  //     scrollIndex = 0;
  //     drawStatus("Today...");
  //   } else { // NEXT
  //     dayOffset++;
  //     scrollIndex = 0;
  //     drawStatus("Next day...");
  //   }
  //   delay(150);
  //   drawMainScreen();
  //   return;
  // }

  // --- Tap Top Header (incl. time bar) -> refresh ICS ---
  if (ty < HEADER_Y + 40) {
    drawStatus("Refreshing data...");
    if (!setupMode && WiFi.status() != WL_CONNECTED) {
      drawStatus("Reconnecting WiFi...");
      connectWiFi();
    }

    if (!setupMode && WiFi.status() == WL_CONNECTED) {
      downloadICS();
    } else if (!setupMode) {
      lastError = "Manual refresh failed: WiFi";
    }

    drawMainScreen();
    delay(200);
    return;
  }

  // --- Scroll list ---
  if (ty >= LIST_TOP && ty <= listBottom) {
    int midpoint = (LIST_TOP + listBottom) / 2;
    time_t viewStart = getViewDayStart();

    int dayEvents = 0;
    if (time(nullptr) > 1700000000L) {
      for (int i = 0; i < eventCount; i++)
        if (isSameDay(events[i].start, viewStart))
          dayEvents++;
    }

    int listFontId = 2;
    int listTh     = tft.fontHeight(listFontId);
    int lineH      = listTh + 6;

    int maxLines = (listBottom - LIST_TOP) / lineH;
    if (maxLines > 6) maxLines = 6;
    if (maxLines < 1) maxLines = 1;

    if (ty < midpoint) scrollIndex--;
    else scrollIndex++;

    if (scrollIndex < 0) scrollIndex = 0;
    if (scrollIndex > dayEvents - maxLines)
      scrollIndex = max(0, dayEvents - maxLines);

    drawMainScreen();
    delay(120);
  }
}

// ================== SIMPLE WEB CONFIG ==================
String htmlPage() {
  String h;
  h.reserve(2600);

  h += F(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<title>Room Display</title>"
    "<style>"
    "body{font-family:Arial,Helvetica,sans-serif;background:#f4f4f4;margin:0;padding:16px;}"
    "h1{color:#111;}"
    "form{background:#fff;padding:16px;border-radius:8px;max-width:520px;margin:0 auto 16px auto;"
      "box-shadow:0 0 8px rgba(0,0,0,.15);}"
    "label{display:block;margin-top:10px;font-weight:bold;font-size:14px;}"
    "input[type=text],input[type=password]{width:100%;padding:6px 8px;margin-top:4px;"
      "box-sizing:border-box;border:1px solid #ccc;border-radius:4px;font-size:14px;}"
    "button{margin-top:16px;padding:8px 16px;border:none;border-radius:4px;"
      "background:#1e3a8a;color:#fff;font-weight:bold;font-size:14px;cursor:pointer;}"
    "button:hover{opacity:.9;}"
    ".info{max-width:520px;margin:12px auto;color:#333;font-size:13px;}"
    "code{background:#eee;padding:2px 4px;border-radius:3px;}"
    ".danger{background:#fee2e2;border:1px solid #fecaca;color:#991b1b;}"
    ".danger button{background:#b91c1c;}"
    "</style></head><body>"
    "<h1>Conference Room Display Configuration</h1>"
    "<div class='info'>Device IP: <code>"
  );

  h += currentIpString();
  h += F("</code><br>");

  if (setupMode) {
    h += F("<b>SETUP MODE</b><br>"
           "You are connected to device AP. Configure WiFi and calendar below, then press "
           "<b>Save &amp; Reboot</b>. After reboot device will join your WiFi.");
  } else {
    h += F("You are connected via existing WiFi network. Change settings and press "
           "<b>Save &amp; Reboot</b>.");
  }

  h += F("</div><form method='POST' action='/save'>");

  // Jeśli brak zapisanej wartości – pokazuj domyślną jako podpowiedź
  h += F("<label>WiFi 1 SSID</label><input type='text' name='wifi1' value='");
  h += (cfg.wifi1.length() ? cfg.wifi1 : DEFAULT_WIFI_SSID1); h += F("'>");

  h += F("<label>WiFi 1 password</label><input type='password' name='pass1' value='");
  h += (cfg.pass1.length() ? cfg.pass1 : DEFAULT_WIFI_PASS1); h += F("'>");

  h += F("<label>WiFi 2 SSID</label><input type='text' name='wifi2' value='");
  h += (cfg.wifi2.length() ? cfg.wifi2 : DEFAULT_WIFI_SSID2); h += F("'>");

  h += F("<label>WiFi 2 password</label><input type='password' name='pass2' value='");
  h += (cfg.pass2.length() ? cfg.pass2 : DEFAULT_WIFI_PASS2); h += F("'>");

  h += F("<label>Calendar ICS URL (must be public)</label><input type='text' name='icsUrl' value='");
  h += cfg.icsUrl; h += F("'>");

  h += F("<label>Room name</label><input type='text' name='room' value='");
  h += cfg.roomName; h += F("'>");

  h += F("<button type='submit'>Save &amp; Reboot</button></form>");

  // Factory reset form
  h += F(
    "<form method='POST' action='/factoryreset' class='danger'"
    " onsubmit='return confirm(\"This will clear WiFi & config and restart to setup mode. Continue?\");'>"
    "<p><b>Factory Reset</b><br>Clear configuration (WiFi, ICS URL, room name) "
    "and reboot to Setup Mode.</p>"
    "<button type='submit'>Factory Reset</button></form>"
  );

  h += F("</body></html>");
  return h;
}

void setupWeb() {
  // MAIN PAGE
  web.on("/", HTTP_GET, []() {
    if (!web.authenticate(cfg.webUser.c_str(), cfg.webPass.c_str())) {
      return web.requestAuthentication();
    }
    web.send(200, "text/html", htmlPage());
  });

  // SAVE CONFIG
  web.on("/save", HTTP_POST, []() {
    if (!web.authenticate(cfg.webUser.c_str(), cfg.webPass.c_str())) {
      return web.requestAuthentication();
    }

    cfg.wifi1    = web.arg("wifi1");
    cfg.pass1    = web.arg("pass1");
    cfg.wifi2    = web.arg("wifi2");
    cfg.pass2    = web.arg("pass2");
    cfg.icsUrl   = web.arg("icsUrl");
    cfg.roomName = web.arg("room");

    saveConfig();

    web.send(200, "text/html",
      "<html><body><h2>Configuration saved. Rebooting...</h2></body></html>");

    delay(1000);
    ESP.restart();
  });

  // FACTORY RESET
  web.on("/factoryreset", HTTP_POST, []() {
    if (!web.authenticate(cfg.webUser.c_str(), cfg.webPass.c_str())) {
      return web.requestAuthentication();
    }

    web.send(200, "text/html",
      "<html><body><h2>Factory reset... Rebooting to setup mode.</h2></body></html>");

    delay(500);

    factoryReset();
    ESP.restart();
  });

  web.onNotFound([]() {
    web.send(404, "text/plain", "Not found");
  });

  web.begin();
  String ipStr = currentIpString();
  Serial.println("Web config server started: http://" + ipStr + "/");
}

// ================== SETUP MODE / FACTORY RESET ==================
void startSetupMode() {
  setupMode = true;
  lastError = "";

  WiFi.mode(WIFI_AP);
  WiFi.disconnect(true);

  uint64_t chipid = ESP.getEfuseMac();
  apSsid = "ROOM-SETUP-" + String((uint32_t)(chipid & 0xFFFFFF), HEX);

  WiFi.softAP(apSsid.c_str(), AP_PASS);

  IPAddress ip = WiFi.softAPIP();

  Serial.println("=== SETUP MODE ===");
  Serial.println("AP SSID: " + apSsid);
  Serial.println("AP PASS: " + String(AP_PASS));
  Serial.print("AP IP: ");
  Serial.println(ip);

  // INstructions 
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(1);

  int y = 10;

  // Header 
  tft.drawCentreString("SETUP MODE", tft.width()/2, y, 4);  
  y += tft.fontHeight(4) + 10;

  // Instructions
  tft.setTextColor(COLOR_SUBHEADER, COLOR_BG);

  tft.drawCentreString("1) Connect WiFi:", tft.width()/2, y, 2);
  y += tft.fontHeight(2) + 6;

  tft.drawCentreString(apSsid, tft.width()/2, y, 2);
  y += tft.fontHeight(2) + 6;

  tft.drawCentreString(String("Pass: ") + AP_PASS, tft.width()/2, y, 2);
  y += tft.fontHeight(2) + 10;

  tft.drawCentreString("2) Open in browser:", tft.width()/2, y, 2);
  y += tft.fontHeight(2) + 6;

  String ipStr = ip.toString();
  tft.drawCentreString(ipStr, tft.width()/2, y, 2);
  y += tft.fontHeight(2) + 10;

  // Login info 
  String loginInfo = "3) Login: admin / Conferentio";
  tft.drawCentreString(loginInfo, tft.width()/2, y, 2);


  setupWeb(); 
}

void factoryReset() {
  Serial.println("FACTORY RESET: clearing NVS (crdisp)...");
  prefs.begin("crdisp", false);
  prefs.clear();
  prefs.end();

  isConfigured = false;
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);

  // TFT jako pierwsze, bo startSetupMode korzysta z ekranu
  tft.begin();
  tft.setRotation(1);

  // Load config from NVS
  loadConfig();

  // If nto configured → SETUP MODE (AP)
  if (!isConfigured || cfg.wifi1.length() == 0) {
    startSetupMode();
    return;
  }

  // Normal mode
  drawStatus("Connecting WiFi...");
  if (!connectWiFi()) {
    lastError = "WiFi config failed";
    // jeśli nie da rady – przejście do setup mode
    startSetupMode();
    return;
  }
  delay(600);

  // Time setup (NTP)
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  setenv("TZ", TZ_EUROPE_WARSAW, 1);
  tzset();

  time_t now = 0;
  for (int i = 0; i < 40; i++) {
    now = time(nullptr);
    if (now > 1700000000L) break;
    delay(300);
  }

  if (now < 1700000000L) {
    if (lastError.length() == 0) lastError = "NTP sync failed!";
    Serial.println("Warning: NTP synchronization failed!");
  } else {
    Serial.printf("NTP Synced. Time is: %s", ctime(&now));
  }

  // Initial Data Download
  drawStatus("Downloading ICS...");
  downloadICS();
  drawMainScreen();
  lastRefresh = millis();

  // Start Web Server (w trybie STA)
  if (WiFi.status() == WL_CONNECTED) {
    setupWeb();
  } else {
    Serial.println("WiFi not connected, web config will start after reconnect.");
  }
}

// ================== LOOP ==================
void loop() {
  static unsigned long lastTimeUpdate = 0;

  // W setup mode: tylko WWW
  if (setupMode) {
    web.handleClient();
    return;
  }

  // Normalny tryb pracy
  web.handleClient();

  // Handle display touch
  uint16_t tx, ty;
  if (tft.getTouch(&tx, &ty)) {
    handleTouch(tx, ty);
  }

  // Network Keep-Alive Check
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  // Auto-refresh ICS data (co 5 minut)
  if (millis() - lastRefresh > 300000) {
    lastRefresh = millis();
    if (WiFi.status() == WL_CONNECTED) {
      downloadICS();
      drawMainScreen();
    }
  }

  // Refresh clock and bottom bar every 5 seconds
  if (millis() - lastTimeUpdate > 5000) {
    lastTimeUpdate = millis();
    updateTimeBar();
    updateBottomBar();
  }
}
