#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <RTCZero.h>
#include <ArduinoLowPower.h>
#include <algorithm>

#define BAUD 115200
#define DEBUG true

/* --- PINS --- */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7

/* --- CONSTANTS --- */
const char HTTPS_URL[] =
  "https://script.google.com/macros/s/AKfycbybv1kcHIaqrGik924pnxW2a3ZmDXkeCn56Kjliggc3300nkH5x6I6uC7_Eg2qZ_i4F/exec";

const int PIN_SD_SELECT = 4;

/* --- STATE MACHINE POINTER --- */
uint8_t state = 0;

/* --- DEEP SLEEP TIME VARIABLES --- */
uint32_t heartBeatInterval = 3600000; // 1 hour in milliseconds
uint8_t hoursInDay = 0; // Counter for hours in a day

/* --- RTC OBJECT --- */
RTCZero rtc;

struct GNSS {
    float latitude;
    float longitude;
    float altitude;
};

GNSS location;

/* --- FUNCTION DECLARATION --- */
void modemBoot();
void modemOff();
void networkAttach();
String sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
void syncSystem();
bool postHTTPS(const char* url, const String& payload);
bool sdInit();
bool sdHasCsvFiles();
bool sdUploadChrono(bool (*up)(const String&));
bool sdDeleteCsv(const char* name);
bool postDataHttps(const String& payload);

void setup() {
    // DEBUG SERIAL PORT
    SerialUSB.begin(BAUD);
    while (!SerialUSB);
    // Initialize Serial1 for 4G LTE modem communication
    Serial1.begin(BAUD);
    while (!Serial1);
    // Initialize Serial2 for LoRa communication
    Serial2.begin(BAUD);
    while (!Serial2);

    /* --- INITIALIZE RTC --- */
    rtc.begin();
    rtc.setTime(0, 0, 0);
    rtc.setDate(1, 1, 2023); // Set initial date (1st Jan 2023)

    /* --- INITIALIZE SD CARD --- */
    if (!sdInit()) {
        SerialUSB.println(F("SD CARD NOT READY!"));
        SerialUSB.println(F("Please check the SD card connection."));
        while (true) {
            delay(1000);
            SerialUSB.println(F("Waiting for SD card..."));
        }
    }

    /* --- Initialize RTC --- */
    rtc.begin();
    rtc.setTime(0, 0, 0); // Set initial time to 00:00:00
    rtc.setDate(1, 1, 2025); // Set initial date (1st Jan 2025)

    /* --- INITIALIZE STATE MACHINE --- */
    state = 0;
}

void loop() {

    /* --- STATE MACHINE --- */
    switch (state) {
        /* --- LoRa Network Set-up --- */
        case 0:
            /* --- Boot SIM7600 Modem --- */
            modemBoot();
            /* --- Sync System Time and Location --- */
            syncSystem();
            /* --- Turn Off 4G LTE Modem --- */
            modemOff();
            /* --- BOOT LoRa MODULE --- */
            /* --- BEGIN LORA COMMUNICATION (Send Network Acknowledgment and RTC INFO) --- */
            /* --- LISTEN FOR LORA RESPONSES and SAVE DATA --- */
            /* --- SHUT DOWN LoRa MODULE --- */
            /* --- BOOT SIM7600 MODEM --- */
            modemBoot();
            /* --- Attach to LTE Network --- */
            networkAttach();
            /* --- Upload Data via HTTPS --- */
            if (sdHasCsvFiles()) {
                SerialUSB.println(F("Uploading saved data..."));
                if (sdUploadChrono(postDataHttps)) {
                    SerialUSB.println(F("Data upload successful."));
                    sdDeleteCsv("data.csv"); // needs to be modified to delete all files
                }
            }
            state = 1;
            break;
        /* --- GATEWAY --- */
        case 1:
            /* --- BOOT LoRa MODULE --- */
            /* --- LISTEN FOR ENDPOINT DEVICE DATA (LoRa) --- */
            /* --- SHUTDOWN LoRa MODULE --- */
            /* --- SAMPLE SENSOR DATA --- */
            /* --- BOOT SIM7500 MODEM --- */
            /* --- Sinc RTC --- */
            /* --- UPLOAD SAVED DATA VIA HTTPS --- */
            /* --- GRACEFULLY SHUTDOWN MODEM --- */
            hoursInDay = 0;
            state = 2;
            break;
        /* --- LOW POWER MODE --- */
        case 2:
            LowPower.deepSleep(heartBeatInterval); // Sleep for 1 hour
            if ( hoursInDay < 24 ) {
                state = 3;
                hoursInDay++;
                break;
            } else {
                state = 1;
                hoursInDay = 0; // Reset the counter after 24 hours
                break;
            }
            break;
        case 3:
            /* --- SAMPLE DATA FROM SENSORS --- */
            /* --- SAVE DATA TO SD CARD --- */
            state = 2; // Return to low power mode
            break;
        default:
            SerialUSB.println(F("Invalid state!"));
            state = 0;
    }
}
/* --------------------------------------------------------------------------- 

                         .-.
                        ( (
                         `-'






                    .   ,- To the Moon !
                   .'.
                   |o|
                  .'o'.
                  |.-.|
                  '   '
                   ( )
                    )
                   ( )

               ____
          .-'""p 8o""`-.
       .-'8888P'Y.`Y[ ' `-.
     ,']88888b.J8oo_      '`.
   ,' ,88888888888["        Y`.
  /   8888888888P            Y8\
 /    Y8888888P'             ]88\
:     `Y88'   P              `888:
:       Y8.oP '- >            Y88:
|          `Yb  __             `'|
:            `'d8888bo.          :
:             d88888888ooo.      ;
 \            Y88888888888P     /
  \            `Y88888888P     /
   `.            d88888P'    ,'
     `.          888PP'    ,'
       `-.      d8P'    ,-'   -- Ur Mom
          `-.,,_'__,,.-'
------------------------------------------------------------------------------
|--- Non est ad astra mollis e terris via. ---|
------------------------------------------------------------------------------ */
/* ---------------------------------- */
/* |================================| */
/* |===== FUNCTION DEFINITIONS =====| */
/* |================================| */
/* ---------------------------------- */

/* --- BOOT MODEM SEQUENCE --- */
void modemBoot() {
    pinMode(LTE_RESET_PIN,  OUTPUT);
    pinMode(LTE_PWRKEY_PIN, OUTPUT);
    pinMode(LTE_FLIGHT_PIN, OUTPUT);

    digitalWrite(LTE_RESET_PIN, LOW);
    digitalWrite(LTE_FLIGHT_PIN, LOW);   // normal mode

    digitalWrite(LTE_PWRKEY_PIN, HIGH);
    delay(2000);
    digitalWrite(LTE_PWRKEY_PIN, LOW);
    delay(5000);

    String ok = sendAT("AT", 1000, false);
    if (ok.indexOf("OK") < 0) {
        SerialUSB.println(F("MODEM NOT RESPONDING"));
        return;
    }
    SerialUSB.println(F("MODEM READY"));
}

/* --- TURN OFF MODEM --- */
void modemOff() {
    sendAT("AT+CPOF", 1000, false);  // turn off modem
    digitalWrite(LTE_PWRKEY_PIN, HIGH);
}

/* --- ATTACH TO LTE NETWORK --- */
void networkAttach() {

    sendAT("AT+CFUN=1", 1000, false);                                           // full-func

    /* 1️  set PDP context with APN */
    sendAT("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"");

    /* 2️  wait until attached instead of one-shot CGATT=1          */
    uint8_t tries = 20;
    while (tries--) {
    String reg = sendAT("AT+CGATT?", 1000, false);               // +CGATT: 1
    if (reg.indexOf(": 1") > 0) break;
    delay(500);
    }
    if (!tries) {
        SerialUSB.println(F("CGATT FAIL"));
        return;
    }

    /* 3️  activate context & get IP                                */
    sendAT("AT+CGACT=1,1", 5000);
    String ipResp = sendAT("AT+CGPADDR=1", 3000);                  // +CGPADDR: 1,10.x.x.x

    SerialUSB.print(F("IP RESP: ")); 
    SerialUSB.println(ipResp);
    if (ipResp.indexOf('.') < 0) {                                 // no dot → no IP
        SerialUSB.println(F("NO IP"));
        return;
    }
}

/* --- SEND AT COMMAND to 4G LTE MODULE --- */
String sendAT(const String& cmd,
              uint32_t      to  = 2000,
              bool          dbg = DEBUG)          // ▸ supply default for dbg
{
    String resp;
    Serial1.println(cmd);                           // sends CR/LF automatically

    unsigned long t0 = millis();
    while (millis() - t0 < to) {
        while (Serial1.available()) resp += (char)Serial1.read();
    }
    if (dbg && resp.length()) SerialUSB.print(resp);
    return resp;
}

/* --- POST a text payload via HTTPS --- */
bool postHTTPS(const char* url, const String& payload) {
    sendAT("AT+HTTPTERM", 1000, false);        // reset
    sendAT("AT+HTTPSSL=1");                    // enable TLS  (add this)
    sendAT("AT+HTTPINIT");

    sendAT("AT+HTTPPARA=\"CID\",1");
    sendAT("AT+HTTPPARA=\"URL\",\"" + String(url) + "\"");
    sendAT("AT+HTTPPARA=\"CONTENT\",\"text/plain\"");

    // upload body
    String r = sendAT("AT+HTTPDATA=" + String(payload.length()) + ",10000", 1000);
    if (r.indexOf("DOWNLOAD") < 0) return false;
    Serial1.print(payload);

    // POST
    String act = sendAT("AT+HTTPACTION=1", 20000);   // allow longer timeout
    if (act.indexOf(",200,") < 0) return false;

    // optional readback
    /* String body = sendAT("AT+HTTPREAD", 3000); */

    sendAT("AT+HTTPTERM");
    return true;
}

/* --- SYNC RTC and Location with GNSS --- */
void syncSystem() {
    sendAT("AT+CGPS=1,1");                   // Start GNSS in standalone mode
    delay(1000);                             // Allow GNSS to initialize

    String resp = sendAT("AT+CGPSINFO");
    sendAT("AT+CGPS=0");                     // Turn GNSS off after use

    int colon = resp.indexOf(':');
    if (colon < 0) {
        SerialUSB.println(F("Malformed CGPSINFO"));
        return;
    }

    String payload = resp.substring(colon + 1);
    payload.trim();

    if (payload.indexOf(",,") >= 0) {
        SerialUSB.println(F("No GPS fix"));
        return;
    }

    // Extract fields: lat, N/S, lon, E/W, date, time, alt
    char latStr[16], ns, lonStr[16], ew;
    char date[7], time[10];
    float alt;

    int parsed = sscanf(payload.c_str(), "%15[^,],%c,%15[^,],%c,%6s,%9s,%f",
                        latStr, &ns, lonStr, &ew, date, time, &alt);
    if (parsed < 7) {
        SerialUSB.println(F("Failed to parse CGPSINFO fields"));
        return;
    }

    // ---- Convert to decimal degrees ----
    float latDeg = atof(latStr);
    float lonDeg = atof(lonStr);

    float lat_dd = int(latDeg / 100);
    float lat_mm = latDeg - lat_dd * 100;
    location.latitude = lat_dd + lat_mm / 60.0;
    if (ns == 'S') location.latitude *= -1;

    float lon_dd = int(lonDeg / 100);
    float lon_mm = lonDeg - lon_dd * 100;
    location.longitude = lon_dd + lon_mm / 60.0;
    if (ew == 'W') location.longitude *= -1;

    location.altitude = alt;

    // ---- Parse UTC date and time ----
    int day    = (date[0] - '0') * 10 + (date[1] - '0');
    int month  = (date[2] - '0') * 10 + (date[3] - '0');
    int year   = 2000 + (date[4] - '0') * 10 + (date[5] - '0');

    int hour   = (time[0] - '0') * 10 + (time[1] - '0');
    int minute = (time[2] - '0') * 10 + (time[3] - '0');
    int second = (time[4] - '0') * 10 + (time[5] - '0');

    // ---- IN UTC ----
    rtc.setTime(hour, minute, second);
    rtc.setDate(day, month, year);

    char gnssMsg[100];
    snprintf(gnssMsg, sizeof(gnssMsg), "GNSS fix:  lat=%.6f  lon=%.6f  alt=%.1fm", location.latitude, location.longitude, location.altitude);
    SerialUSB.println(gnssMsg);

    char timeMsg[60];
    snprintf(timeMsg, sizeof(timeMsg), "UTC time:  %02d/%02d/%04d %02d:%02d:%02d", day, month, year, hour, minute, second);
    SerialUSB.println(timeMsg);
}


/* --- MOUNT SD CARD --- */
bool sdInit() {
    static bool ready = false;
    if (!ready) ready = SD.begin(PIN_SD_SELECT);
    return ready;
}

/* --- CHECK IF CSV FILES EXIST --- */
bool sdHasCsvFiles() {
    if (!sdInit()) return false;
    File r = SD.open("/");
    while (File f = r.openNextFile()) {
        if (!f.isDirectory() && String(f.name()).endsWith(".CSV")) {
            f.close(); r.close(); return true;
        }
        f.close();
    }
    r.close();
    return false;
}

/* --- HTTPS UPLOAD HELPER --- */
bool postDataHttps(const String& payload) {
    return postHTTPS(HTTPS_URL, payload);
}

/* --- UPLOAD ALL CSV FILES CHRONOLOGICALLY --- */
bool sdUploadChrono(bool (*up)(const String&)) {
    if (!sdInit()) return false;
    String list[32]; int n = 0;
    File r = SD.open("/");
    while (File f = r.openNextFile()) {
        if (!f.isDirectory() && String(f.name()).endsWith(".CSV") && n < 32)
            list[n++] = String(f.name());
        f.close();
    }
    r.close();
    if (!n) return false;
    for (int i = 0; i < n - 1; ++i)
        for (int j = i + 1; j < n; ++j)
            if (list[j] < list[i]) std::swap(list[i], list[j]);
    for (int i = 0; i < n; ++i) {
        File f = SD.open(list[i], FILE_READ);
        if (!f) continue;
        String row = "";
        while (f.available()) {
            char c = f.read();
            if (c == '\n') {
                if (!up(row)) { f.close(); return false; }
                row = "";
            } else if (c != '\r') row += c;
        }
        f.close();
        SD.remove(list[i]);
    }
    return true;
}

/* --- DELETE --- */
bool sdDeleteCsv(const char* name) {
    if (!sdInit()) return false;
    return SD.exists(name) && SD.remove(name);
}