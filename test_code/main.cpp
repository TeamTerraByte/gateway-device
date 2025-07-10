#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <RTCZero.h>
#include <ArduinoLowPower.h>
#include <algorithm>
#include <Wire.h>
#include <secrets.h>

#define BAUD 115200
#define DEBUG true

/* --- PINS --- */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7
#define relayPin 3

#define SLAVE_ADDRESS 0x08

/* --- SENSOR DATA --- */
String moistBuf = "";
String tempBuf  = "";
String curType  = "";
bool   assembling = false;          // true while a block is still arriving

/* --- CONSTANTS --- */
/* ---------- THINGSPEAK --------------------------------- */
const char* TS_API_KEY   = API_WRITE_KEY;
const char TS_BASE_URL[]  = "http://api.thingspeak.com/update";
const uint8_t TS_MAX_FLD  = 8;

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
bool sdUploadChrono();
bool sdDeleteCsv(const char* name);
//bool postDataHttps(const String& payload);
void processChunk(const String& s);
void sampleData();
void sampleDataTest();

void setup() {
    // DEBUG SERIAL PORT
    SerialUSB.begin(BAUD);
    while (!SerialUSB);
    // Initialize Serial1 for 4G LTE modem communication
    Serial1.begin(BAUD);
    while (!Serial1);

    // No LoRa for Version 1.0

    /* --- INITIALIZE RTC --- */
    rtc.begin();
    rtc.setTime(0, 0, 0);
    rtc.setDate(1, 1, 25); // 1, 1, 2025

    /* --- INITIALIZE SD CARD --- */
    if (!sdInit()) {
        SerialUSB.println(F("SD CARD NOT READY!"));
        SerialUSB.println(F("Please check the SD card connection."));
        while (true) {
            delay(1000);
            SerialUSB.println(F("Waiting for SD card..."));
        }
    }

    /* --- INITIATE I2C FOR ENVIROPRO --- */
    Wire.begin(SLAVE_ADDRESS);
    Wire.onReceive([](int /*n*/) {
        String chunk;
        while (Wire.available())           // read all bytes of this I²C packet
            chunk += char(Wire.read());
        processChunk(chunk);               // assemble
    });

    /* --- Initialize RTC --- */
    rtc.begin();
    rtc.setTime(0, 0, 0); // Set initial time to 00:00:00
    rtc.setDate(1, 1, 25); // Set initial date (1st Jan 2025)

    /* --- INITIALIZE STATE MACHINE --- */
    state = 0;
}

/* ======================================================== */
/* |----------------- MAIN STATE MACHINE -----------------| */
/* ======================================================== */
void loop() {

    /* --- STATE MACHINE --- */
    switch (state) {
        /* --- GATEWAY --- */
        case 0:
            /* --- Boot SIM7600 Modem --- */
            modemBoot();
            /* --- Sync System Time and Location --- */
            syncSystem();

            /* --- Attach to LTE Network --- */
            networkAttach();
            /* --- Upload Data via HTTPS --- */
            if (sdHasCsvFiles()) {
                SerialUSB.println(F("Uploading saved data..."));
                if (sdUploadChrono()) {
                    SerialUSB.println(F("Data upload successful."));
                    sdDeleteCsv("data.csv"); // needs to be modified to delete all files
                }
            }
            /* --- Turn on Relay --- */
            digitalWrite(relayPin, HIGH);
            delay(1000); // Allow time for the sensor to power up
            /* --- Sample Data from Sensors --- */
            sampleData();
            /* --- Turn off Relay --- */
            digitalWrite(relayPin, LOW);
            hoursInDay = 0;
            state = 1;
            break;
        /* --- LOW POWER MODE --- */
        case 1:
            //LowPower.deepSleep();
            delay(5000); // simulate 5 seconds of Low power mode

            if ( hoursInDay < 24 ) {
                state = 2;
                hoursInDay++;
                break;
            } else {
                state = 0;
                hoursInDay = 0; // Reset the counter after 24 hours
                break;
            }
            break;
        /* --- LOW POWER MODE --- */
        case 2:
            /* --- Turn on Relay --- */
            digitalWrite(relayPin, HIGH); // Turn on the relay to power the sensor
            delay(1000); // Allow time for the sensor to power up
            /* --- SAMPLE DATA FROM SENSORS --- */
            //sampleData();
            sampleDataTest(); // Test function to simulate data sampling
            /* --- Turn off Relay --- */
            digitalWrite(relayPin, LOW);
            state = 1; // Return to low power mode
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
String sendAT(const String& cmd, uint32_t to, bool dbg ){
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
bool postHTTPS(const char*, const String& payload)
{
    /* 0 ─ build form-encoded body: api_key & 8 fields (trimmed) */
    String tokens[TS_MAX_FLD];
    uint8_t n = 0, p = 0;
    while (n < TS_MAX_FLD) {
        int c = payload.indexOf(',', p);
        if (c < 0) c = payload.length();
        tokens[n++] = payload.substring(p, c);  tokens[n-1].trim();
        p = c + 1;
        if (c >= payload.length()) break;
    }
    String body = "api_key=" + String(TS_API_KEY);
    for (uint8_t i = 0; i < n; ++i)
        body += "&field" + String(i + 1) + "=" + tokens[i];

    /* 1 ─ HTTP stack ---------------------------------------------- */
    sendAT("AT+HTTPTERM", 1000, true);            // clean slate
    sendAT("AT+HTTPSSL=1", 1000, true);           // ► TLS ON  ◄
    // sendAT("AT+SHSSL=1,0,\"\"");               // (disable cert check if FW<2023)
    sendAT("AT+HTTPINIT", 1000, true);
    sendAT("AT+HTTPPARA=\"CID\",1", 1000, true);
    sendAT("AT+HTTPPARA=\"URL\",\"https://api.thingspeak.com/update\"", 1000, true);
    sendAT("AT+HTTPPARA=\"CONTENT\",\"application/x-www-form-urlencoded\"", 1000, true);

    /* 2 ─ load POST body ------------------------------------------ */
    String r = sendAT("AT+HTTPDATA=" + String(body.length()) + ",10000", 2000, true);
    if (r.indexOf("DOWNLOAD") < 0) { sendAT("AT+HTTPTERM"); return false; }
    Serial1.print(body);
    delay(150);

    /* 3 ─ POST ----------------------------------------------------- */
    sendAT("AT+HTTPACTION=1", 500, true);         // 1 = POST

    String act; uint32_t t0 = millis();
    while (millis() - t0 < 25000) {               // wait ≤25 s for unsolicited line
        act += sendAT("", 250, true);
        if (act.indexOf("+HTTPACTION:") >= 0) break;
    }
    sendAT("AT+HTTPTERM", 1000, true);

    bool ok = (act.indexOf(",200,") >= 0);
    SerialUSB.println(ok ? F("ThingSpeak: OK") : F("ThingSpeak: FAIL"));
    return ok;
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
//bool postDataHttps(const String& payload) {
//    return postHTTPS(HTTPS_URL, payload);
//}

/* --- UPLOAD ALL CSV FILES CHRONOLOGICALLY --- */
/* ── Upload every *.CSV row-by-row in chronological order ──────────────── */
bool sdUploadChrono()
{
    if (!sdInit()) return false;

    /* 1 ─ collect filenames */
    String list[32]; uint8_t n = 0;
    File dir = SD.open("/");
    while (File f = dir.openNextFile()) {
        if (!f.isDirectory() && String(f.name()).endsWith(".CSV") && n < 32)
            list[n++] = String(f.name());
        f.close();
    }
    dir.close();
    if (!n) return false;

    /* 2 ─ sort alphabetically (lexicographic ≈ chronological) */
    for (uint8_t i = 0; i < n - 1; ++i)
        for (uint8_t j = i + 1; j < n; ++j)
            if (list[j] < list[i]) std::swap(list[i], list[j]);

    /* 3 ─ stream each file row-by-row */
    for (uint8_t i = 0; i < n; ++i) {
        File f = SD.open(list[i], FILE_READ);
        if (!f) continue;

        String row = "";
        while (f.available()) {
            char c = f.read();
            if (c == '\n') {                    // got one complete row
                row.trim();                     // remove CR if any

                /* skip header or empty lines (contains alpha chars) */
                bool hasAlpha = false;
                for (char ch : row) { if (isalpha(ch)) { hasAlpha = true; break; } }
                if (!row.length() || hasAlpha) { row = ""; continue; }

                if (!postHTTPS(nullptr, row)) { // push to ThingSpeak
                    f.close(); return false;    // abort on first failure
                }
                row = "";
            }
            else if (c != '\r') row += c;       // build row
        }
        f.close();
        SD.remove(list[i]);                     // delete file after upload
    }
    return true;
}



/* --- DELETE --- */
bool sdDeleteCsv(const char* name) {
    if (!sdInit()) return false;
    return SD.exists(name) && SD.remove(name);
}

/* --- System Validation --- */
void writeTestCsvToSd(const char* filename) {
    if (!SD.begin(PIN_SD_SELECT)) {
        SerialUSB.println("SD card init failed");
        return;
    }

    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        SerialUSB.println("Failed to open file");
        return;
    }

    file.println("Type,Value1,Value2,Value3,Value4,Value5,Value6,Value7,Value8,Value9");
    file.println("Sensor,12.3,45.6,78.9,10.1,11.2,13.3,14.4,15.5,16.6");

    file.close();
    SerialUSB.println("Test CSV written to SD");
}

/* --- PROCESS I²C CHUNK FROM ENVIROPRO --- */
void processChunk(const String& data){
    /* 1 ── new transmission header ------------------------ */
    if (data.startsWith("Moist,")) {
        curType     = "Moist";
        moistBuf    = data;          // start fresh
        assembling  = true;
        return;
    }
    if (data.startsWith("Temp,")) {
        curType     = "Temp";
        tempBuf     = data;
        assembling  = true;
        return;
    }

    /* 2 ── continuation of current block ----------------- */
    if (assembling) {
        if (curType == "Moist")      moistBuf += data;
        else if (curType == "Temp")  tempBuf  += data;

        /* 3 ── heuristic: a short (<15 B) fragment marks
         *        the end of this transmission ------------- */
        if (data.length() < 15) {
            assembling = false;      // finished – ready for sampleData()
            /*  let the state-machine call sampleData()
                (case 2) to save the finished buffer        */
        }
    }
}

/*-----------------------------------------------------------
 *  SAMPLE DATA  – called once per hour from the state-machine
 * --------------------------------------------------------- */
void sampleData()
{
    /* 1 ── still receiving an I²C block? */
    if (assembling) return;

    /* 2 ── need BOTH buffers ready */
    if (!moistBuf.length() || !tempBuf.length()) return;

    /* 3 ── strip the labels (“Moist,” / “Temp,”) ------------- */
    String moistValues = moistBuf.substring(6);      // after "Moist,"
    String tempValues  = tempBuf.substring(5);       // after "Temp,"
    if (moistValues.endsWith(",")) moistValues.remove(moistValues.length() - 1);
    if (tempValues.endsWith(","))  tempValues.remove(tempValues.length()  - 1);

    /* 4 ── build timestamp ---------------------------------- */
    uint16_t yr  = rtc.getYear() + 2000;
    uint8_t  mon = rtc.getMonth();
    uint8_t  day = rtc.getDay();
    uint8_t  hr  = rtc.getHours();
    uint8_t  min = rtc.getMinutes();
    uint8_t  sec = rtc.getSeconds();

    char dateStr[11], timeStr[9];
    snprintf(dateStr, sizeof(dateStr), "%04u-%02u-%02u", yr, mon, day);
    snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u", hr, min, sec);

    /* 5 ── compose CSV row ---------------------------------- */
    String row  = String(dateStr) + "," + timeStr + ",";
    row += String(location.latitude , 6) + ",";
    row += String(location.longitude, 6) + ",";
    row += String(location.altitude , 1) + ",";
    row += tempValues + "," + moistValues + ",";   // placeholder for IR_Temp

    /* 6 ── ensure SD present -------------------------------- */
    if (!sdInit()) return;                          // silent if no card

    /* 7 ── open / create daily file ------------------------- */
    char fname[24];
    snprintf(fname, sizeof(fname), "DATA_%04u%02u%02u.CSV", yr, mon, day);
    bool newFile = !SD.exists(fname);

    File f = SD.open(fname, FILE_WRITE);
    if (!f) return;

    /* 8 ── add header once per day -------------------------- */
    if (newFile) {
        f.println(F("date,time,lat,lon,alt,"
                    "Temp10,Temp20,Temp30,Temp40,Temp50,Temp60,Temp70,Temp80,"
                    "Moist10,Moist20,Moist30,Moist40,Moist50,Moist60,Moist70,Moist80,"
                    "IR_Temp"));
    }

    /* 9 ── append the data row ------------------------------ */
    f.println(row);
    f.close();

    /*10 ── clear for next hour ------------------------------ */
    moistBuf  = "";
    tempBuf   = "";
    curType   = "";
}

void sampleDataTest() {
    // Simulate data sampling for testing purposes
    File writeFile = SD.open("test.csv", FILE_WRITE);
    if (!writeFile) {
      SerialUSB.println("Error opening test.csv for data write");
    } else {
      SerialUSB.print("Writing test data… ");
      writeFile.println("12345, 67.8, 90.1, 234.5, 12.3456, -98.7654");
      writeFile.close();
      SerialUSB.println("done.");
    }
}
