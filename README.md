# ESP SmartBeacon APRS-IS Client
GPS receiver that can send APRS position reports to an APRS-IS server. This client has also SmartBeacon(TM) capability.

## Hardware prerequisities
- An esp8266 board
- One GPS Sensor that can output NMEA

## Software prerequisities
- [InfluxDB](https://www.influxdata.com/) running somewhere in the internet
(or another software that can handle similar POST request).
- [Arduino IDE](https://www.arduino.cc/en/main/software)
- [Arduino ESP8266 filesystem uploader](https://github.com/esp8266/arduino-esp8266fs-plugin)
- Download and install [TinyGPS++ library](http://arduiniana.org/libraries/tinygpsplus/)

Required libraries:

ESP8266WiFi, ESP8266WiFiMulti,  ESP8266HTTPClient, WiFiClientSecure, WiFiClient, DNSServer, ESP8266WebServer, SoftwareSerial, FS

Optional libraries:

Wire, Adafruit_SSD1306, Adafruit_GFX

Use the filesystem uploader tool to upload the contents of data library. It contains the files for
the configuration portal.

## Connections
Connect your GPS receiver to pins D7 (RXD2) and D8 (TXD2). 

You can connect a switch between D1 and GND. If D1 is grounded, the esp8266 starts portal mode. The pin can be
also changed from the code, see row `#define APREQUEST PIN_D5`.

Optionally you can connect an I2C OLED Display to D1 (SCL) and D2 (SDA) for live information display. 
You should change `#define SSD1306_128_64` to `#undef SSD1306_128_64` to disable the display code. If the display is not used,
the onboard LEDs will show the board operational status.
- Portal mode - alternating
- Operational - one is steady, the other one when sending to APRS-IS

# Operating the device

## Portal mode
When your board is in portal mode, it blinks both onboard LEDs. Take your phone and connect to WiFi network 
**ESP8266 APRS** and accept that there's no internet connection etc.

Open your browser and go to `http://192.168.4.1/`

The web GUI should be self explanatory.

There's almost no sanity checks for the data sent from the forms. This is not a public web service and if you
want to mess up your board or make a denial of service to it using eg. buffer overflows, feel free to do so.

Credits for the configuration portal mode goes to [Mikko](https://github.com/oh2mp/).

## APRS configuration
Use the Portal mode to configure the APRS settings:
- Callsign to transmit as
- APRS passcode
- Comment text to send with beacon
- APRS-IS Server Hostname, use one of the rotated names for redundancy and balance on the hosts
- APRS Symbol, reference the APRS symbols table for the two character string
- [SmartBeacon(TM)](http://www.hamhud.net/hh2/smartbeacon.html) parameters

For the SmartBeacon(TM) parameters there are a number of sites explaining the algorythm and gives some hints on good sets depending if your are walking, on the bike or driving a car. 

## InfluxDB
The device can also send position update to an InfluxDB. Enter portal mode to configure:
- The write URL to your InfluxDB database instance, leave empty or prefix any other character than 'h' to disable updates
- username:password for the database
- and the measurement name to store the data in

The data is update using fields:
- lat = latitude in decimal degrees
- log = longitude in decimal degrees
- cse = heading degrees from north
- spd = speed m/s
- alt = altitude in decimal meters
- mod = NMEA mode 1 - no fix, 2 - 2D fix or 3 - 3D fix

Update interval is tied to APRS SmartBeacon(TM). You should receive something like this in the database:

```
> select * from gps
name: gps
---------
time                    alt     call    cse     lat             lon             mod     spd     tocall
1589362483404306849     19      OH2TH-8 187     60.123456       24.543210       3       0.7     APESPG
1589364288772944141     10.7    OH2TH-8 169     60.123456       24.543210       3       0       APESPG
1589364516196077483     20.1    OH2TH-8 45      60.123456       24.543210       3       0.6     APESPG
```

## Display
The optional display shows current information such as configuration, connectivity and runtime position.
![ESPGPS Display](s/ESPGPS-display.jpg)

The fields in display are these, and of course you can customize it in the code.
```
Wifi AP                 mycall(*)
APRS comment

Num sats and Fix             HDOP
Speed (km/h)               Course
Latitude                Longitude 

date                      time UT
```

The (*) next to "mycall" shows if the last beacon was successfully sent to the selected APRS-IS server.

# Mode pictures and screenshots

## Some pictures of the device
![ESPGPS Proto](s/ESPGPS-proto.jpg)
![ESPGPS Almost there](s/ESPGPS-almost.jpg)
![ESPGPS Final casing](s/ESPGPS-final.jpg)

## Sample pictures of the portal
![screenshot](s/screenshot.jpg)

# Disclaimer
The device was built to my own needs, and has only been tested in the northern hemisphere east of Greenwich, so negative coordinates may or may not work properly. This has also not been checked for memory leaks. Feel free to create an issue and I'll try to fix the code at some time, or not. Even better if you have a fix, let me know and I can import the fix.