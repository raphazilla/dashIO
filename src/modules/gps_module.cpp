#include "gps_module.h"
#include "../core/sensor_data.h"
#include "../config.h"

#include <HardwareSerial.h>

// Use UART1 for GPS (UART0 = USB CDC)
static HardwareSerial gpsSerial(1);

// ---------------------------------------------------------------------------
// Minimal NMEA parser — parses $GPRMC and $GPGGA sentences
// ---------------------------------------------------------------------------

static double parseLatLon(const String& val, const String& dir) {
    if (val.length() < 3) return 0.0;
    int dotPos = val.indexOf('.');
    if (dotPos < 3) return 0.0;
    double deg = val.substring(0, dotPos - 2).toDouble();
    double min = val.substring(dotPos - 2).toDouble();
    double result = deg + min / 60.0;
    if (dir == "S" || dir == "W") result = -result;
    return result;
}

static String nmeaField(const String& sentence, int idx) {
    int start = 0, count = 0;
    for (int i = 0; i < (int)sentence.length(); i++) {
        if (sentence[i] == ',') {
            if (count == idx) return sentence.substring(start, i);
            start = i + 1;
            count++;
        }
    }
    if (count == idx) return sentence.substring(start);
    return "";
}

static void parseGPRMC(const String& sentence) {
    // $GPRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,ddmmyy,x.x,a*hh
    String status = nmeaField(sentence, 2);
    bool fix = (status == "A");

    if (!fix) {
        SensorLock lock;
        g_sensors.gps_fix = false;
        return;
    }

    double lat = parseLatLon(nmeaField(sentence, 3), nmeaField(sentence, 4));
    double lon = parseLatLon(nmeaField(sentence, 5), nmeaField(sentence, 6));

    SensorLock lock;
    g_sensors.gps_fix = true;
    g_sensors.gps_lat = lat;
    g_sensors.gps_lon = lon;
}

static void parseGPGGA(const String& sentence) {
    // $GPGGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh
    int sats = nmeaField(sentence, 7).toInt();
    float hdop = nmeaField(sentence, 8).toFloat();

    SensorLock lock;
    g_sensors.gps_satellites = sats;
    g_sensors.gps_hdop       = hdop;
}

static String s_line;

static void processLine(const String& line) {
    if (!line.startsWith("$")) return;

    // Verify checksum (last 2 hex chars after '*')
    int star = line.lastIndexOf('*');
    if (star < 0 || star + 2 >= (int)line.length()) return;

    uint8_t calc = 0;
    for (int i = 1; i < star; i++) calc ^= line[i];
    uint8_t expected = strtoul(line.substring(star + 1, star + 3).c_str(), nullptr, 16);
    if (calc != expected) return;

    if (line.startsWith("$GPRMC") || line.startsWith("$GNRMC")) {
        parseGPRMC(line);
    } else if (line.startsWith("$GPGGA") || line.startsWith("$GNGGA")) {
        parseGPGGA(line);
    }
}

bool gps_init() {
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.printf("[GPS] UART1 started (RX=%d TX=%d @%d baud)\n",
                  GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
    return true;
}

void gps_update() {
    while (gpsSerial.available()) {
        char c = gpsSerial.read();
        if (c == '\n') {
            s_line.trim();
            if (s_line.length() > 0) processLine(s_line);
            s_line = "";
        } else if (c != '\r') {
            s_line += c;
        }
    }
}
