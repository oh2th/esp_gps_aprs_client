/*
   ESP GPS APRS-IS Client with SmartBeacon

   See https://github.com/oh2th/esp_gps_aprs_client
*/

#include <Arduino.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <base64.h>

// Local headers
#include "strutils.h"

/* ------------------------------------------------------------------------------- */
/* These are the pins for all ESP8266 boards */
//      Name   GPIO    Function
#define PIN_D0  16  // WAKE, Onboard LED
#define PIN_D1   5  // User purpose
#define PIN_D2   4  // User purpose
#define PIN_D3   0  // Low on boot means enter FLASH mode
#define PIN_D4   2  // TXD1 (must be high on boot to go to UART0 FLASH mode), Onboard LED
#define PIN_D5  14  // HSCLK
#define PIN_D6  12  // HMISO
#define PIN_D7  13  // HMOSI  RXD2
#define PIN_D8  15  // HCS    TXD2 (must be low on boot to enter UART0 FLASH mode)
#define PIN_D9   3  //        RXD0
#define PIN_D10  1  //        TXD0

#define PIN_MOSI 8  // SD1
#define PIN_MISO 7  // SD0
#define PIN_SCLK 6  // CLK
#define PIN_HWCS 0  // D3

#define PIN_D11  9  // SD2
#define PIN_D12 10  // SD4
/* ------------------------------------------------------------------------------- */

#define APREQUEST PIN_D1
#define APTIMEOUT 180000

#define APRSSOFTWARE "APESPG"

// Use Serial port on TXD2/RXD2
static const int RXPin = PIN_D7, TXPin = PIN_D8;
static const uint32_t GPSBaud = 9600;

// Wifi configuration
char currssid[33];              // current SSID
char currip[16];                // current IP address
char lastssid[33];              // last SSID
char lastip[16];                // last IP address

// APRS configuration
char mycall[10];                // Radioamateur callsign
char aprspass[8];               // APRS-IS aprspass for callsign
char comment[32];               // Comment string added to position
char aprshost[255];             // APRS-IS host
char symbol_str[8];             // APRS Symbol
uint16_t aprsport = 14580;      // Port is fixed to TCP/14580

// APRS SmartBeacon configuration
//Slow Speed = Speed below which I consider myself "stopped" 10 m.p.h.
//Slow Rate = Beacon rate while speed below stopped threshold (1750s = ~29mins)
//Fast Speed = Speed that I send out beacons at fast rate 100 m.p.h.
//Fast Rate = Beacon rate at fastest interval (175s ~ 3 mins)
//Any speed between these limits, the beacon rate is proportional.
char low_speed_str[8], low_rate_str[8], high_speed_str[8], high_rate_str[8];
int low_speed, low_rate, high_speed, high_rate;
//Min Turn Time = don't beacon any faster than this interval in a turn (40sec)
//Min Turn Angle = Minimum turn angle to consider beaconing. (20 degrees)
//Turn Slope = Number when divided by current speed creates an angle that is added to Min Turn Angle to trigger a beacon.
char turn_time_str[8], turn_min_str[8], turn_slope_str[8];
int turn_time, turn_min, turn_slope;
unsigned long lastBeaconMillis;
int retry_now = 1;
double prev_heading;

// InfluxDB Configuration
char measurement[32];           // measurement name for InfluxDB
char userpass[64];              // user:pass for InfluxDB
String b64pass;                 // base64 encoded user:pass for basic auth
char intervalstr[6];            // interval as string
int interval = 0;               // interval as minutes

url_info urlp;
char url[128];
char scheme[6];
char host[64];
char path[64];
uint16_t port;

// AP Portal mode
unsigned long portal_timer = 0;

// The TinyGPS++ object
TinyGPSPlus gps;

// The serial connection to the GPS device
SoftwareSerial ss(RXPin, TXPin);

ESP8266WiFiMulti WiFiMulti;
ESP8266WebServer server(80);
IPAddress apIP(192, 168, 4, 1); // portal ip address
DNSServer dnsServer;
WiFiClient client;

File file;

/* ------------------------------------------------------------------------------- */
void setup() {
  pinMode(APREQUEST, INPUT_PULLUP);
  pinMode(PIN_D0, OUTPUT); // pin D0 and D4 are onboard leds on ESP12 boards
  pinMode(PIN_D4, OUTPUT);

  Serial.begin(115200);
  ss.begin(GPSBaud);
  Serial.println(F("ESP GPS APRS-IS Client with SmartBeacon by OH2TH."));
  Serial.print(F("Using TinyGPS++ library v. ")); Serial.println(TinyGPSPlus::libraryVersion());

  SPIFFS.begin();

  if (SPIFFS.exists("/aprs.txt")) {
    file = SPIFFS.open("/aprs.txt", "r");
    file.readBytesUntil('\n', mycall, 10);
    if (mycall[strlen(mycall)-1] == 13) {mycall[strlen(mycall)-1] = 0;}
    
    file.readBytesUntil('\n', aprspass, 7);
    if (aprspass[strlen(aprspass)-1] == 13) {aprspass[strlen(aprspass)-1] = 0;}
    
    file.readBytesUntil('\n', comment, 32);
    if (comment[strlen(comment)-1] == 13) {comment[strlen(comment)-1] = 0;}
    
    file.readBytesUntil('\n', aprshost, 255);
    if (aprshost[strlen(aprshost)-1] == 13) {aprshost[strlen(aprshost)-1] = 0;}
    
    file.readBytesUntil('\n', symbol_str, 8);
    if (symbol_str[strlen(symbol_str)-1] == 13) {symbol_str[strlen(symbol_str)-1] = 0;}
    
    file.readBytesUntil('\n', low_speed_str, 8);
    if (low_speed_str[strlen(low_speed_str)-1] == 13) {low_speed_str[strlen(low_speed_str)-1] = 0;}
    low_speed = atoi(low_speed_str);
    
    file.readBytesUntil('\n', low_rate_str, 8);
    if (low_rate_str[strlen(low_rate_str)-1] == 13) {low_rate_str[strlen(low_rate_str)-1] = 0;}
    low_rate = atoi(low_rate_str);
    
    file.readBytesUntil('\n', high_speed_str, 8);
    if (high_speed_str[strlen(high_speed_str)-1] == 13) {high_speed_str[strlen(high_speed_str)-1] = 0;}
    high_speed = atoi(high_speed_str);
    
    file.readBytesUntil('\n', high_rate_str, 8);
    if (high_rate_str[strlen(high_rate_str)-1] == 13) {high_rate_str[strlen(high_rate_str)-1] = 0;}
    high_rate = atoi(high_rate_str);
    
    file.readBytesUntil('\n', turn_min_str, 8);
    if (turn_min_str[strlen(turn_min_str)-1] == 13) {turn_min_str[strlen(turn_min_str)-1] = 0;}
    turn_min = atoi(turn_min_str);
    
    file.readBytesUntil('\n', turn_slope_str, 8);
    if (turn_slope_str[strlen(turn_slope_str)-1] == 13) {turn_slope_str[strlen(turn_slope_str)-1] = 0;}
    turn_slope = atoi(turn_slope_str);
    
    file.readBytesUntil('\n', turn_time_str, 8);
    if (turn_time_str[strlen(turn_time_str)-1] == 13) {turn_time_str[strlen(turn_time_str)-1] = 0;}
    turn_time = atoi(turn_time_str);
    
    file.close();
  }
  Serial.printf("APRS: %s %s to %s with symbol %s\n", mycall, aprspass, aprshost, symbol_str);
  Serial.printf("APRS comment: %s\n", comment);
  //Serial.printf("Low speed %s, Low rate %s, High speed %s, High rate %s, Turn min %s, Turn slope %s, Turn time %s\n", low_speed_str, low_rate_str, high_speed_str, high_rate_str, turn_min_str, turn_slope_str, turn_time_str);
  Serial.printf("Low speed %i, Low rate %i, High speed %i, High rate %i, Turn min %i, Turn slope %i, Turn time %i\n", low_speed, low_rate, high_speed, high_rate, turn_min, turn_slope, turn_time);

  int len;
  if (SPIFFS.exists("/last_wifi.txt")) {
    file = SPIFFS.open("/last_wifi.txt", "r");
    file.readBytesUntil('\n', lastssid, 33);
    if (lastssid[strlen(lastssid) - 1] == 13) {
      lastssid[strlen(lastssid) - 1] = 0;
    }
    file.readBytesUntil('\n', lastip, 16);
    if (lastip[strlen(lastip) - 1] == 13) {
      lastip[strlen(lastip) - 1] = 0;
    }
    file.close();
  } else {
    strcpy(lastssid, "none");
    strcpy(lastip, "none");
  }

  if (SPIFFS.exists("/known_wifis.txt")) {
    char ssid[33];
    char pass[65];
    WiFi.mode(WIFI_STA);
    file = SPIFFS.open("/known_wifis.txt", "r");
    while (file.available()) {
      memset(ssid, '\0', sizeof(ssid));
      memset(pass, '\0', sizeof(pass));
      file.readBytesUntil('\t', ssid, 32);
      file.readBytesUntil('\n', pass, 64);
      WiFiMulti.addAP(ssid, pass);
      Serial.printf("wifi loaded: %s / %s\n", ssid, pass);
    }
    file.close();
  } else {
    startPortal(); // no settings were found, so start the portal without button
  }
  Serial.println("Started.");
  digitalWrite(PIN_D0, HIGH); // LEDs off
  digitalWrite(PIN_D4, HIGH);
}

void loop() {
  double cur_speed, cur_heading;
  int beacon_rate, turn_threshold;
  unsigned long currentMillis = millis(), secs_since_beacon = (currentMillis - lastBeaconMillis) / 1000;

  // Normal running mode, connect to wifi, decode GPS and send APRS packets.
  if (WiFi.getMode() == WIFI_STA) {

    // Try to connect to known WiFi
    char foo[64];
    if (WiFiMulti.run() != WL_CONNECTED) {
      currssid[0] = '\0';
      delay(1000);
    } else {
      WiFi.SSID().toCharArray(foo, 64);
      if (strcmp(currssid, foo) != 0) {
        strcpy(currssid, foo);
      }
      WiFi.localIP().toString().toCharArray(foo, 64);
      if (strcmp(currip, foo) != 0) {
        strcpy(currip, foo);
      }
      // if our connection has changed, save last wifi info
      if (strcmp(currip, lastip) != 0 || strcmp(currssid, lastssid) != 0) {
        strcpy(lastip, currip);
        strcpy(lastssid, currssid);
        file = SPIFFS.open("/last_wifi.txt", "w");
        file.printf("%s\n%s\n", lastssid, lastip);
        file.close();
      }
    }

    // When connected to WiFi
    if ((WiFiMulti.run() == WL_CONNECTED)) {
      digitalWrite(PIN_D0, LOW); // led on when in connect
      digitalWrite(PIN_D4, LOW); // interval led on as a heartbeat

      smartDelay(100); // initial feeding of the GPS to make sure we have data

      // For a good position report HDOP should be 1-2 for excellent position or no more that 5 for good position, a value of 5-10 is still moderate
      // Value of 0 or 99 means there is no coverage.
      float hdop_value = (float)gps.hdop.value() / 100;
      if ( hdop_value > 0 && hdop_value < 10 ) {
        const char* report = positionReportWithAltitude();
        if (report[0] != '\0') {
          // Position Report available, lets transmit to APRS-IS
          cur_speed = gps.speed.kmph();
          cur_heading = gps.course.deg();
          // SmartBeacon (http://www.hamhud.net/hh2/smartbeacon.html)
          if (cur_speed < low_speed) {
            beacon_rate = low_rate;                             // Stopped - slow rate beacon
          } else {                                              // We are moving - varies with speed
            if (cur_speed > high_speed) {
              beacon_rate = high_rate;                          // Fast speed = fast rate beacon
            } else {
              beacon_rate = high_rate * high_speed / cur_speed; // Intermediate beacon rate
            }                                                   // Corner pegging - if not stopped
            turn_threshold = turn_min + turn_slope / cur_speed; // turn threshold speed-dependent
            if ((prev_heading - cur_heading > turn_threshold) && (secs_since_beacon > turn_time)) {
              secs_since_beacon = beacon_rate;                  // transmit beacon now
              prev_heading = cur_heading;
            }
          }
          // Send beacon if SmartBeacon interval (beacon_rate) is reached
          Serial.printf("Seconds since last beacon %lu and beacon_rate is %i. Retry = %i\n", secs_since_beacon, beacon_rate, retry_now);
          if (secs_since_beacon > beacon_rate || retry_now == 1) {
            lastBeaconMillis = currentMillis;
            if (client.connect(aprshost, aprsport)) {
              Serial.printf("HDOP=%0.1f and connected to %s:%u\n", hdop_value, aprshost, aprsport);
              client.printf("user %s pass %s\r\n", mycall, aprspass);
              delay(100);
              client.printf("%s%s\r\n", report, comment);
              client.stop();
              Serial.printf("OK: %s\n", report);
            } else {
              Serial.println("Unable to connect to APRS-IS.");
              Serial.printf("HDOP=%0.1f but failed to connect to %s:%u as %s %s\n", hdop_value, aprshost, aprsport, mycall, aprspass);
              Serial.printf("FAIL: %s\n", report);
            }
            retry_now = 0;
          }
        } else {
          Serial.println("No report.");
          retry_now = 1;
        }
      } else {
        Serial.printf("HDOP=%0.1f GPS is not ready.\n", hdop_value);
        retry_now = 1;
      }

      if (millis() > 5000 && gps.charsProcessed() < 10) {
        Serial.println(F("No GPS detected: check wiring."));
        while (true);
      }
      digitalWrite(PIN_D4, HIGH); // led off
      smartDelay(5000);
    }
  } else if (WiFi.getMode() == WIFI_AP) { // portal mode
    dnsServer.processNextRequest();
    server.handleClient();

    // blink onboard leds if we are in portal mode
    if (int(millis() % 1000) < 500) {
      digitalWrite(PIN_D0, LOW);
      digitalWrite(PIN_D4, HIGH);
    } else {
      digitalWrite(PIN_D0, HIGH);
      digitalWrite(PIN_D4, LOW);
    }
  }
  if (digitalRead(APREQUEST) == LOW && WiFi.getMode() == WIFI_STA) {
    startPortal();
  }
  if (millis() - portal_timer > APTIMEOUT && WiFi.getMode() == WIFI_AP) {
    Serial.println("Portal timeout. Booting.");
    delay(1000);
    ESP.restart();
  }
}

// -------------------------------------------------------------------------------
// Support functions
// -------------------------------------------------------------------------------

// This custom version of delay() ensures that the gps object is being "fed".
static void smartDelay(unsigned long ms)
{
  unsigned long start = millis();
  do
  {
    while (ss.available())
      gps.encode(ss.read());
  } while (millis() - start < ms);
}

// -------------------------------------------------------------------------------
// APRS position with without timestamp (no APRS messaging)
//   !lati.xxN/long.xxEvCRS/SPD/comment
// -------------------------------------------------------------------------------
char* positionReportWithAltitude() {
  static char report [64];
  memset (report, '\0' , sizeof(report));
  String symbolStr = String(symbol_str);

  if (gps.location.isValid()) {
    sprintf(report, "%s>%s,TCPIP*:!%02.0f%02.2f%s%c%03.0f%02.2f%s%c%03.0f/%03.0f/A=%06.0f",
            mycall, APRSSOFTWARE,
            (float)gps.location.rawLat().deg, (float)gps.location.rawLat().billionths / 1000000000 * 60, (gps.location.rawLat().negative ? "S" : "N"), symbol_str[0],
            (float)gps.location.rawLng().deg, (float)gps.location.rawLng().billionths / 1000000000 * 60, (gps.location.rawLng().negative ? "W" : "E"), symbol_str[1],
            (float)gps.course.deg(), (float)gps.speed.knots(), (float)gps.altitude.feet());
  }
  return (report);
}

/* ------------------------------------------------------------------------------- */
/* Portal code begins here                                                         */
/* ------------------------------------------------------------------------------- */

void startPortal() {
  portal_timer = millis();
  WiFi.disconnect();
  delay(100);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("ESP8266 APRS");

  dnsServer.setTTL(300);
  dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
  dnsServer.start(53, "*", apIP);

  server.on("/", httpRoot);
  server.on("/style.css", httpStyle);
  server.on("/wifis.html", httpWifis);
  server.on("/savewifi", httpSaveWifi);
  server.on("/aprs.html", httpAPRS);
  server.on("/saveaprs", httpSaveAPRS);
  server.on("/influx.html", httpInflux);
  server.on("/saveinfl", httpSaveInflux);
  server.on("/boot", httpBoot);
  server.on("/dl", httpDownload);

  server.onNotFound([]() {
    server.sendHeader("Refresh", "1;url=/");
    server.send(404, "text/plain", "QSD QSY");
  });
  server.begin();
  Serial.println("Started portal");
}
/* ------------------------------------------------------------------------------- */

void httpRoot() {
  portal_timer = millis();
  String html;

  file = SPIFFS.open("/index.html", "r");
  html = file.readString();
  file.close();
  html.replace("###LASTSSID###", lastssid);
  html.replace("###LASTIP###", lastip);

  server.send(200, "text/html; charset=UTF-8", html);
}

/* ------------------------------------------------------------------------------- */

void httpWifis() {
  String html;
  char tablerows[1024];
  char rowbuf[256];
  char ssid[33];
  char pass[33];
  int counter = 0;

  portal_timer = millis();
  memset(tablerows, '\0', sizeof(tablerows));

  file = SPIFFS.open("/wifis.html", "r");
  html = file.readString();
  file.close();

  if (SPIFFS.exists("/known_wifis.txt")) {
    file = SPIFFS.open("/known_wifis.txt", "r");
    while (file.available()) {
      memset(rowbuf, '\0', sizeof(rowbuf));
      memset(ssid, '\0', sizeof(ssid));
      memset(pass, '\0', sizeof(pass));
      file.readBytesUntil('\t', ssid, 33);
      file.readBytesUntil('\n', pass, 33);
      sprintf(rowbuf, "<tr><td>SSID</td><td><input type=\"text\" name=\"ssid%d\" maxlength=\"32\" value=\"%s\"></td></tr>", counter, ssid);
      strcat(tablerows, rowbuf);
      sprintf(rowbuf, "<tr><td>PASS</td><td><input type=\"text\" name=\"pass%d\" maxlength=\"32\" value=\"%s\"></td></tr>", counter, pass);
      strcat(tablerows, rowbuf);
      counter++;
    }
    file.close();
  }
  html.replace("###TABLEROWS###", tablerows);
  html.replace("###COUNTER###", String(counter));

  if (counter > 3) {
    html.replace("table-row", "none");
  }

  server.send(200, "text/html; charset=UTF-8", html);
}
/* ------------------------------------------------------------------------------- */

void httpSaveWifi() {
  portal_timer = millis();
  String html;

  file = SPIFFS.open("/known_wifis.txt", "w");

  for (int i = 0; i < server.arg("counter").toInt(); i++) {
    if (server.arg("ssid" + String(i)).length() > 0) {
      file.print(server.arg("ssid" + String(i)));
      file.print("\t");
      file.print(server.arg("pass" + String(i)));
      file.print("\n");
    }
  }
  // Add new
  if (server.arg("ssid").length() > 0) {
    file.print(server.arg("ssid"));
    file.print("\t");
    file.print(server.arg("pass"));
    file.print("\n");
  }
  file.close();

  file = SPIFFS.open("/ok.html", "r");
  html = file.readString();
  file.close();

  server.sendHeader("Refresh", "3;url=/");
  server.send(200, "text/html; charset=UTF-8", html);
}
/* ------------------------------------------------------------------------------- */

void httpAPRS() {
  portal_timer = millis();
  String html;
  String symtab;

  file = SPIFFS.open("/aprs.html", "r");
  html = file.readString();
  file.close();

  html.replace("###MYCALL###", String(mycall));
  html.replace("###APRSPASS###", String(aprspass));
  html.replace("###COMMENT###", String(comment));
  html.replace("###APRSHOST###", String(aprshost));
  html.replace("###SYMBOL###", String(symbol_str));
  html.replace("###LOWSPEED###", String(low_speed));
  html.replace("###LOWRATE###", String(low_rate));
  html.replace("###HIGHSPEED###", String(high_speed));
  html.replace("###HIGHRATE###", String(high_rate));
  html.replace("###TURNMIN###", String(turn_min));
  html.replace("###TURNSLOPE###", String(turn_slope));
  html.replace("###TURNTIME###", String(turn_time));

  server.send(200, "text/html; charset=UTF-8", html);
}
/* ------------------------------------------------------------------------------- */

void httpSaveAPRS() {
  portal_timer = millis();
  String html;

  file = SPIFFS.open("/aprs.txt", "w");
  file.println(server.arg("mycall"));
  file.println(server.arg("aprspass"));
  file.println(server.arg("comment"));
  file.println(server.arg("aprshost"));
  file.println(server.arg("symbol"));
  file.println(server.arg("low_speed"));
  file.println(server.arg("low_rate"));
  file.println(server.arg("high_speed"));
  file.println(server.arg("high_rate"));
  file.println(server.arg("turn_min"));
  file.println(server.arg("turn_slope"));
  file.println(server.arg("turn_time"));
  file.close();

  if (SPIFFS.exists("/aprs.txt")) {
    file = SPIFFS.open("/aprs.txt", "r");
    file.readBytesUntil('\n', mycall, 10);
    if (mycall[strlen(mycall)-1] == 13) {mycall[strlen(mycall)-1] = 0;}
    
    file.readBytesUntil('\n', aprspass, 7);
    if (aprspass[strlen(aprspass)-1] == 13) {aprspass[strlen(aprspass)-1] = 0;}
    
    file.readBytesUntil('\n', comment, 32);
    if (comment[strlen(comment)-1] == 13) {comment[strlen(comment)-1] = 0;}
    
    file.readBytesUntil('\n', aprshost, 255);
    if (aprshost[strlen(aprshost)-1] == 13) {aprshost[strlen(aprshost)-1] = 0;}
    
    file.readBytesUntil('\n', symbol_str, 8);
    if (symbol_str[strlen(symbol_str)-1] == 13) {symbol_str[strlen(symbol_str)-1] = 0;}
    
    file.readBytesUntil('\n', low_speed_str, 8);
    if (low_speed_str[strlen(low_speed_str)-1] == 13) {low_speed_str[strlen(low_speed_str)-1] = 0;}
    low_speed = atoi(low_speed_str);
    
    file.readBytesUntil('\n', low_rate_str, 8);
    if (low_rate_str[strlen(low_rate_str)-1] == 13) {low_rate_str[strlen(low_rate_str)-1] = 0;}
    low_rate = atoi(low_rate_str);
    
    file.readBytesUntil('\n', high_speed_str, 8);
    if (high_speed_str[strlen(high_speed_str)-1] == 13) {high_speed_str[strlen(high_speed_str)-1] = 0;}
    high_speed = atoi(high_speed_str);
    
    file.readBytesUntil('\n', high_rate_str, 8);
    if (high_rate_str[strlen(high_rate_str)-1] == 13) {high_rate_str[strlen(high_rate_str)-1] = 0;}
    high_rate = atoi(high_rate_str);
    
    file.readBytesUntil('\n', turn_min_str, 8);
    if (turn_min_str[strlen(turn_min_str)-1] == 13) {turn_min_str[strlen(turn_min_str)-1] = 0;}
    turn_min = atoi(turn_min_str);
    
    file.readBytesUntil('\n', turn_slope_str, 8);
    if (turn_slope_str[strlen(turn_slope_str)-1] == 13) {turn_slope_str[strlen(turn_slope_str)-1] = 0;}
    turn_slope = atoi(turn_slope_str);
    
    file.readBytesUntil('\n', turn_time_str, 8);
    if (turn_time_str[strlen(turn_time_str)-1] == 13) {turn_time_str[strlen(turn_time_str)-1] = 0;}
    turn_time = atoi(turn_time_str);
    
    file.close();
  }

  file = SPIFFS.open("/ok.html", "r");
  html = file.readString();
  file.close();

  server.sendHeader("Refresh", "3;url=/");
  server.send(200, "text/html; charset=UTF-8", html);
}
/* ------------------------------------------------------------------------------- */

void httpInflux() {
  portal_timer = millis();
  String html;

  file = SPIFFS.open("/influx.html", "r");
  html = file.readString();
  file.close();

  html.replace("###URL###", String(url));
  html.replace("###USERPASS###", String(userpass));
  html.replace("###IDBM###", String(measurement));
  html.replace("###INTERVAL###", String(intervalstr));

  server.send(200, "text/html; charset=UTF-8", html);
}
/* ------------------------------------------------------------------------------- */

void httpSaveInflux() {
  portal_timer = millis();
  String html;

  file = SPIFFS.open("/influxdb.txt", "w");
  file.println(server.arg("url"));
  file.println(server.arg("userpass"));
  file.println(server.arg("idbm"));
  file.println(server.arg("interval"));
  file.close();

  // reread
  file = SPIFFS.open("/influxdb.txt", "r");
  file.readBytesUntil('\n', url, 128);
  if (url[strlen(url) - 1] == 13) {
    url[strlen(url) - 1] = 0;
  }
  file.readBytesUntil('\n', userpass, 64);
  if (userpass[strlen(userpass) - 1] == 13) {
    userpass[strlen(userpass) - 1] = 0;
  }
  file.readBytesUntil('\n', measurement, 32);
  if (measurement[strlen(measurement) - 1] == 13) {
    measurement[strlen(measurement) - 1] = 0;
  }
  file.readBytesUntil('\n', intervalstr, 8);
  if (intervalstr[strlen(intervalstr) - 1] == 13) {
    intervalstr[strlen(intervalstr) - 1] = 0;
  }
  file.close();

  file = SPIFFS.open("/ok.html", "r");
  html = file.readString();
  file.close();

  server.sendHeader("Refresh", "3;url=/");
  server.send(200, "text/html; charset=UTF-8", html);
}
/* ------------------------------------------------------------------------------- */

void httpStyle() {
  portal_timer = millis();
  String css;

  file = SPIFFS.open("/style.css", "r");
  css = file.readString();
  file.close();
  server.send(200, "text/css", css);
}
/* ------------------------------------------------------------------------------- */

void httpBoot() {
  portal_timer = millis();
  String html;

  file = SPIFFS.open("/ok.html", "r");
  html = file.readString();
  file.close();

  server.sendHeader("Refresh", "3;url=about:blank");
  server.send(200, "text/html; charset=UTF-8", html);
  delay(1000);
  ESP.restart();
}
/* ------------------------------------------------------------------------------- */

void httpDownload(){
    String str = "";
    file = SPIFFS.open(server.arg(0), "r");
    if (!file) {
      Serial.println("Can't open SPIFFS file !\r\n");         
    }
    else {
      char buf[1024];
      int siz = file.size();
      while(siz > 0) {
        size_t len = std::min((int)(sizeof(buf) - 1), siz);
        file.read((uint8_t *)buf, len);
        buf[len] = 0;
        str += buf;
        siz -= sizeof(buf) - 1;
      }
      file.close();
      server.send(200, "text/plain", str);
    }
}
/* ------------------------------------------------------------------------------- */
