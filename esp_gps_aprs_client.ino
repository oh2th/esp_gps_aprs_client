/*
   ESP SmartBeacon APRS-IS Client

   See https://github.com/oh2th/esp_gps_aprs_client
*/

#include <Arduino.h>
// GPS
#include <TinyGPS++.h>
#include <SoftwareSerial.h>

// Display
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// NodeMCU ESP8266 Wifi
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

// Filesystem
#include <FS.h>
#include <base64.h>

// Local headers
#include "strutils.h"

/* ------------------------------------------------------------------------------- */
/* These are the pins for all ESP8266 boards */
//      Name   GPIO    Function     My function
#define PIN_D0  16  // WAKE         Onboard LED
#define PIN_D1   5  // User purpose I2C_SCL
#define PIN_D2   4  // User purpose I2C_SDA
#define PIN_D3   0  //              APREQUEST   (Low on boot means enter FLASH mode)
#define PIN_D4   2  // TXD1         Onboard LED (must be high on boot to go to UART0 FLASH mode)
#define PIN_D5  14  // HSCLK        
#define PIN_D6  12  // HMISO
#define PIN_D7  13  // HMOSI  RXD2  GPS
#define PIN_D8  15  // HCS    TXD2  GPS         (must be low on boot to enter UART0 FLASH mode)
#define PIN_D9   3  //        RXD0              (Same as USB Serial)
#define PIN_D10  1  //        TXD0              (Same as USB Serial)

#define PIN_MOSI 8  // SD1
#define PIN_MISO 7  // SD0
#define PIN_SCLK 6  // CLK
#define PIN_HWCS 0  // D3

#define PIN_D11  9  // SD2
#define PIN_D12 10  // SD4

/* ------------------------------------------------------------------------------- */
#define APRSSOFTWARE "APESPG"

// Configuration AP-mode when LOW
// Note the PIN_D3 is the same as enter flash mode while pressing reset.
// If PIN_D3 is pulled low while in running mode, we enter AP-mode
#define APREQUEST PIN_D3
#define APTIMEOUT 180000

// SSD1306 I2C Display
#define SSD1306_128_64  // Comment out this line to disable display code
// If this display is used, onboard LEDs are off while normal operation
#ifdef SSD1306_128_64
#define OLED_ADDR 0x3C
#define OLED_RESET -1
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
#endif //SSD1306_128_64

// Use Serial port on TXD2/RXD2 for GPS
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
char low_speed_str[8], low_rate_str[8], high_speed_str[8], high_rate_str[8];
int low_speed, low_rate, high_speed, high_rate;
char turn_time_str[8], turn_min_str[8], turn_slope_str[8];
int turn_time, turn_min, turn_slope;
unsigned long lastBeaconMillis;
bool send_now = true, aprs_ok = false;
int prev_heading;

// InfluxDB Configuration
char measurement[32];           // measurement name for InfluxDB
char userpass[64];              // user:pass for InfluxDB
String b64pass;                 // base64 encoded user:pass for basic auth
// Influx position report
bool influx_ok = false;
double influx_prev_lat=0, influx_prev_lng=0;
int distance_travelled=0;

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
TinyGPSCustom gpsFix(gps, "GPGSA", 2); // 1= No fix, 2=2D, 3=3D

// The serial connection to the GPS device
SoftwareSerial gpsSerial(RXPin, TXPin);

ESP8266WiFiMulti WiFiMulti;
ESP8266WebServer server(80);
IPAddress apIP(192, 168, 4, 1); // portal ip address
DNSServer dnsServer;
BearSSL::WiFiClientSecure httpsclient;
WiFiClient httpclient;

File file;

const int API_TIMEOUT = 10000; // timeout in milliseconds for http/https client

/* ------------------------------------------------------------------------------- */
void setup() {
  pinMode(APREQUEST, INPUT_PULLUP);
  pinMode(PIN_D0, OUTPUT); // pin D0 and D4 are onboard leds on ESP12 boards
  pinMode(PIN_D4, OUTPUT);

  Serial.begin(115200);
  gpsSerial.begin(GPSBaud);
  Serial.println(F("ESP SmartBeacon APRS-IS Client by OH2TH."));

#ifdef SSD1306_128_64
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  // Displays Adafruit logo for 2 secs, initialized be Adafruit GFX library.
  // display.display();
  // delay(2000);
  display.clearDisplay();
  display.display();
#endif

  SPIFFS.begin();

  readCfgAPRS();
  readCfgInfluxDB();

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
    }
    file.close();
    httpsclient.setInsecure(); // For allowing self signed, don't care certificate validity
    httpsclient.setTimeout(API_TIMEOUT);
    httpclient.setTimeout(API_TIMEOUT);
  } else {
    startPortal(); // no settings were found, so start the portal without button
  }
  Serial.println("Started.");
  digitalWrite(PIN_D0, HIGH); // LEDs off
  digitalWrite(PIN_D4, HIGH);
}

void loop() {
  int cur_speed, cur_heading, beacon_rate, turn_threshold, heading_change_since_beacon = 0;
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
#ifndef SSD1306_128_64 // Onboard LEDs are used as indicators, if no OLED.
      digitalWrite(PIN_D0, LOW); // led on when in connect
      digitalWrite(PIN_D4, LOW); // interval led on as a heartbeat
#endif
#ifdef SSD1306_128_64
      updateDisplay();
#endif

      smartDelay(100); // initial feeding of the GPS to make sure we have data

      if ( atoi(gpsFix.value()) > 1 ) {
        const char* report = positionReportWithAltitude();
        if (report[0] != '\0') {
          // Position Report available, lets transmit to APRS-IS
          cur_speed = gps.speed.kmph();
          cur_heading = gps.course.deg();
          //
          // SmartBeacon (http://www.hamhud.net/hh2/smartbeacon.html)
          //
          // Slow Speed = Speed below which I consider myself "stopped" 10 m.p.h.
          // Slow Rate = Beacon rate while speed below stopped threshold (1750s = ~29mins)
          // Fast Speed = Speed that I send out beacons at fast rate 100 m.p.h.
          // Fast Rate = Beacon rate at fastest interval (175s ~ 3 mins)
          // Any speed between these limits, the beacon rate is proportional.
          // Min Turn Time = don't beacon any faster than this interval in a turn (40sec)
          // Min Turn Angle = Minimum turn angle to consider beaconing. (20 degrees)
          // Turn Slope = Number when divided by current speed creates an angle that is added to Min Turn Angle to trigger a beacon.

          // Stopped - slow rate beacon
          if (cur_speed < low_speed) {
            beacon_rate = low_rate;
          } else {
            // Adjust beacon rate according to speed
            if (cur_speed > high_speed) {
              beacon_rate = high_rate;
            } else {
              beacon_rate = high_rate * high_speed / cur_speed;
              if (beacon_rate > low_rate) {
                beacon_rate = low_rate;
              }
              if (beacon_rate < high_rate) {
                beacon_rate = high_rate;
              }
            }
            // Corner pegging - ALWAYS occurs if not "stopped"
            // - turn threshold is speed-dependent
            turn_threshold = turn_min + turn_slope / cur_speed;
            if (prev_heading > cur_heading) {
              heading_change_since_beacon = ((prev_heading - cur_heading + 360) % 360);
            } else {
              heading_change_since_beacon = ((cur_heading - prev_heading + 360) % 360);
            }
            if ((heading_change_since_beacon > turn_threshold) && (secs_since_beacon > turn_time)) {
              send_now = true;
            }
          }

          // Send beacon if SmartBeacon interval (beacon_rate) is reached
          if (secs_since_beacon > beacon_rate || send_now) {
            lastBeaconMillis = currentMillis;
            // APRS-IS
            if (httpclient.connect(aprshost, aprsport)) {
              httpclient.printf("user %s pass %s\r\n", mycall, aprspass);
              delay(100);
              httpclient.printf("%s%s\r\n", report, comment);
              httpclient.stop();
              Serial.printf("OK: %s\n", report);
              prev_heading = cur_heading;
              aprs_ok = true;
            } else {
              Serial.printf("Failed to connect to %s:%u as %s %s\n", aprshost, aprsport, mycall, aprspass);
              aprs_ok = false;
            }
            // If url is configured, transmit data to InfluxDB
            if (url[0] == 'h') {
              int httpcode = sendInfluxData(influxPositionReport());
              if(httpcode >= 200 and httpcode < 300) {
                influx_ok = true;
                distance_travelled = 0; // Reset also distance counter, we only report distance travelled from last successfull
              } else {
                influx_ok = false;
              }
            }
            send_now = false;
          }
        }
      }

      if (millis() > 5000 && gps.charsProcessed() < 10) {
        Serial.println(F("No GPS detected: check wiring."));
        while (true);
      }
#ifndef SSD1306_128_64
      digitalWrite(PIN_D4, HIGH); // led off
#endif
      smartDelay(1000);
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
    while (gpsSerial.available())
      gps.encode(gpsSerial.read());
  } while (millis() - start < ms);
}

// Function to read APRS configuration from file.
static void readCfgAPRS()
{
  if (SPIFFS.exists("/aprs.txt")) {
    file = SPIFFS.open("/aprs.txt", "r");
    file.readBytesUntil('\n', mycall, 10);
    if (mycall[strlen(mycall) - 1] == 13) {
      mycall[strlen(mycall) - 1] = 0;
    }

    file.readBytesUntil('\n', aprspass, 7);
    if (aprspass[strlen(aprspass) - 1] == 13) {
      aprspass[strlen(aprspass) - 1] = 0;
    }

    file.readBytesUntil('\n', comment, 32);
    if (comment[strlen(comment) - 1] == 13) {
      comment[strlen(comment) - 1] = 0;
    }

    file.readBytesUntil('\n', aprshost, 255);
    if (aprshost[strlen(aprshost) - 1] == 13) {
      aprshost[strlen(aprshost) - 1] = 0;
    }

    file.readBytesUntil('\n', symbol_str, 8);
    if (symbol_str[strlen(symbol_str) - 1] == 13) {
      symbol_str[strlen(symbol_str) - 1] = 0;
    }

    file.readBytesUntil('\n', low_speed_str, 8);
    if (low_speed_str[strlen(low_speed_str) - 1] == 13) {
      low_speed_str[strlen(low_speed_str) - 1] = 0;
    }
    low_speed = atoi(low_speed_str);

    file.readBytesUntil('\n', low_rate_str, 8);
    if (low_rate_str[strlen(low_rate_str) - 1] == 13) {
      low_rate_str[strlen(low_rate_str) - 1] = 0;
    }
    low_rate = atoi(low_rate_str);

    file.readBytesUntil('\n', high_speed_str, 8);
    if (high_speed_str[strlen(high_speed_str) - 1] == 13) {
      high_speed_str[strlen(high_speed_str) - 1] = 0;
    }
    high_speed = atoi(high_speed_str);

    file.readBytesUntil('\n', high_rate_str, 8);
    if (high_rate_str[strlen(high_rate_str) - 1] == 13) {
      high_rate_str[strlen(high_rate_str) - 1] = 0;
    }
    high_rate = atoi(high_rate_str);

    file.readBytesUntil('\n', turn_min_str, 8);
    if (turn_min_str[strlen(turn_min_str) - 1] == 13) {
      turn_min_str[strlen(turn_min_str) - 1] = 0;
    }
    turn_min = atoi(turn_min_str);

    file.readBytesUntil('\n', turn_slope_str, 8);
    if (turn_slope_str[strlen(turn_slope_str) - 1] == 13) {
      turn_slope_str[strlen(turn_slope_str) - 1] = 0;
    }
    turn_slope = atoi(turn_slope_str);

    file.readBytesUntil('\n', turn_time_str, 8);
    if (turn_time_str[strlen(turn_time_str) - 1] == 13) {
      turn_time_str[strlen(turn_time_str) - 1] = 0;
    }
    turn_time = atoi(turn_time_str);

    file.close();
  }
}

// Function to read InfluxDB configuration from file.
static void readCfgInfluxDB()
{
  if (SPIFFS.exists("/influxdb.txt")) {
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
    file.close();
    b64pass = String(userpass);
    b64pass.trim();
    b64pass = base64::encode(b64pass, 0);
  }
  // handle the InfluxDB url
  if (url[0] == 'h') {
    split_url(&urlp, url);

    Serial.printf("scheme %s\nhost %s\nport %d\npath %s\n\n", urlp.scheme, urlp.hostn, urlp.port, urlp.path);

    strcpy(scheme, urlp.scheme);
    strcpy(host, urlp.hostn);
    port = urlp.port;
    strcpy(path, urlp.path);
    sprintf(url, "%s://%s:%d%s\0", scheme, host, port, path);
  }
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
    sprintf(report, "%s>%s,TCPIP*:!%02.0f%05.2f%s%c%03.0f%05.2f%s%c%03.0f/%03.0f/A=%06.0f",
            mycall, APRSSOFTWARE,
            (float)gps.location.rawLat().deg, (float)gps.location.rawLat().billionths / 1000000000 * 60, (gps.location.rawLat().negative ? "S" : "N"), symbol_str[0],
            (float)gps.location.rawLng().deg, (float)gps.location.rawLng().billionths / 1000000000 * 60, (gps.location.rawLng().negative ? "W" : "E"), symbol_str[1],
            (float)gps.course.deg(), (float)gps.speed.knots(), (float)gps.altitude.feet());
  }
  return (report);
}

// -------------------------------------------------------------------------------
// Report data for sending to InfluxDB where fields:
// lat = latitude in decimal degrees
// lng = longitude in decimal degrees
// cse = heading degrees from north
// dst = distance travelled since previous report
// spd = speed m/s
// alt = altitude in decimal meters
// mod = NMEA mode 1, 2 or 3
// -------------------------------------------------------------------------------
char* influxPositionReport() {
  static char report [256] = "";
  memset (report, '\0' , sizeof(report));
  double curr_lat = gps.location.lat();
  double curr_lng = gps.location.lng();
  // To calculate distance travelled, we use current position initially if we did not have a previous position.
  if(influx_prev_lat == 0 && influx_prev_lng == 0) {
    influx_prev_lat = curr_lat;
    influx_prev_lng = curr_lng;
  }

  distance_travelled += gps.distanceBetween(curr_lat, curr_lng, influx_prev_lat, influx_prev_lng);      

  if (gps.location.isValid()) {
    sprintf(report, "%s,call=%s,tocall=%s lat=%s%f,lon=%s%f,cse=%0.0f,spd=%0.1f,alt=%0.1f,mod=%s",
            measurement, mycall, APRSSOFTWARE,
            (gps.location.rawLat().negative ? "-" : ""), (float)curr_lat,
            (gps.location.rawLng().negative ? "-" : ""), (float)curr_lng,
            (float)gps.course.deg(), (float)gps.speed.mps(), (float)gps.altitude.meters(), gpsFix.value());
  }
  return (report);
}

/* ------------------------------------------------------------------------------- */
/* Connect to InfluxDB and send the data                                           */
/* ------------------------------------------------------------------------------- */

int sendInfluxData(char *influx_report) {
  static int httpcode = 0;
  String httpResponse = "";
  // Send only of URL is set in configuration
  if (url[0] == 'h' && influx_report[0] != '\0') {
    // HTTPS client
    if (strcmp(scheme, "https") == 0) {
      if (httpsclient.connect(host, port)) {
        httpsclient.printf("POST %s HTTP/1.1\nHost: %s\n", path, host);
        httpsclient.printf("Content-Length: %d\nAuthorization: Basic ", strlen(influx_report));
        httpsclient.print(b64pass);
        httpsclient.print("\nConnection: close\n\n");
        httpsclient.print(influx_report);

        while (httpsclient.connected()) {
          httpResponse = httpsclient.readStringUntil('\n');
          if(httpResponse.startsWith("HTTP")) {
            int firstDelim = httpResponse.indexOf(" ");
            int secondDelim = httpResponse.indexOf(" ", firstDelim + 1 );
            httpcode = httpResponse.substring(firstDelim, secondDelim).toInt();
            break;
          }
        }
        httpsclient.stop();
      } else {
        Serial.printf("%s connect failed.\n", scheme);
      }
    }
    // HTTP client
    if (strcmp(scheme, "http") == 0) {
      if (httpclient.connect(host, port)) {
        httpclient.printf("POST %s HTTP/1.1\nHost: %s\n", path, host);
        httpclient.printf("Content-Length: %d\nAuthorization: Basic ", strlen(influx_report));
        httpclient.print(b64pass);
        httpclient.print("\nConnection: close\n\n");
        httpclient.print(influx_report);

        while (httpclient.connected()) {
          httpResponse = httpsclient.readStringUntil('\n');
          if(httpResponse.startsWith("HTTP")) {
            int firstDelim = httpResponse.indexOf(" ");
            int secondDelim = httpResponse.indexOf(" ", firstDelim + 1 );
            httpcode = httpResponse.substring(firstDelim, secondDelim).toInt();
            break;
          }
        }
        httpclient.stop();
      } else {
        Serial.printf("%s connect failed.\n", scheme);
      }
    }
  }
  return httpcode;
}
/* ------------------------------------------------------------------------------- */
/* updaeteDisplay()                                                         */
/* ------------------------------------------------------------------------------- */

#ifdef SSD1306_128_64
void updateDisplay() {
  char tmp[22]; // buffer that fits a 21 character string with null, the length of line.
  memset (tmp, '\0' , sizeof(tmp));

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.clearDisplay();

  // First line: Connected wifi and then my call right aligned.
  display.drawPixel(7, 1, WHITE);
  display.drawPixel(5, 2, WHITE); display.drawPixel(8, 2, WHITE);
  display.drawPixel(6, 3, WHITE); display.drawPixel(9, 3, WHITE);
  display.drawPixel(3, 4, WHITE); display.drawPixel(6, 4, WHITE); display.drawPixel(9, 4, WHITE);
  display.drawPixel(6, 5, WHITE); display.drawPixel(9, 5, WHITE);
  display.drawPixel(5, 6, WHITE); display.drawPixel(8, 6, WHITE);
  display.drawPixel(7, 7, WHITE);
  // then Current SSID we are connected to
  display.setCursor(13, 1);
  display.print(currssid);
  // and right align mycall based on the screen width and leave 3 chars space for APRS and INFLUX status
  display.setCursor(OLED_WIDTH - (strlen(mycall) + 3) * 6, 1);
  display.print(mycall);
  display.print((aprs_ok ? " A" : " -"));
  display.print((influx_ok ? "I" : "-"));

  // Second line: Beacon comment
  display.setCursor(1, 9);
  strncat(tmp, comment, 21); // Copy content of comment up-to 21 characters
  display.print(tmp); // and display it
  memset (tmp, '\0' , sizeof(tmp));

  // Sixth line: GPS Satellites and HDOP
  display.setCursor(1, 25);
  switch (atoi(gpsFix.value())) {
    case 3:
      strncat(tmp, "3D", 2);
      break;
    case 2:
      strncat(tmp, "2D", 2);
      break;
    default:
      strncat(tmp, "--", 2);
  }
  display.printf("SATS %2d %s HDOP %5.1f", (int)gps.satellites.value(), tmp, (float)gps.hdop.value() / 100);
  memset (tmp, '\0' , sizeof(tmp));

  // Fourth line: Speed and Course
  display.setCursor(1, 33);
  display.printf("SPD %6.0f CSE    %03.0f", (float)gps.speed.kmph(), (float)gps.course.deg());

  // Fifth line: Latitude and longitude
  display.setCursor(1, 41);
  display.printf("%s %02.0f %05.2f %s%03.0f %05.2f",
                 (gps.location.rawLat().negative ? "S" : "N"), (float)gps.location.rawLat().deg, (float)gps.location.rawLat().billionths / 1000000000 * 60,
                 (gps.location.rawLng().negative ? "W" : "E"), (float)gps.location.rawLng().deg, (float)gps.location.rawLng().billionths / 1000000000 * 60);
  display.drawPixel(27, 41, WHITE);
  display.drawPixel(26, 42, WHITE); display.drawPixel(28, 42, WHITE);
  display.drawPixel(27, 43, WHITE);
  display.drawPixel(93, 41, WHITE);
  display.drawPixel(92, 42, WHITE); display.drawPixel(94, 42, WHITE);
  display.drawPixel(93, 43, WHITE);

  // Seventh line: not used
  // display.setCursor(11, 49);

  // Eighth line: Date and time
  display.setCursor(1, 57);
  display.printf("%4.0f-%02.0f-%02.0f  z%02.0f:%02.0f:%02.0f",
                 (float)gps.date.year(), (float)gps.date.month(), (float)gps.date.day(),
                 (float)gps.time.hour(), (float)gps.time.minute(), (float)gps.time.second());

  // update display with all of the above graphics
  display.display();
}
#endif

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

  // reread config from file
  readCfgAPRS();

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

void httpDownload() {
  String str = "";
  file = SPIFFS.open(server.arg(0), "r");
  if (!file) {
    Serial.println("Can't open SPIFFS file !\r\n");
  }
  else {
    char buf[1024];
    int siz = file.size();
    while (siz > 0) {
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
