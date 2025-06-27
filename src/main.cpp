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

/* --- FUNCTION DECLARATION --- */
void modemBoot();
void modemOff();
void networkAttach();
String sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
void syncRTC();
void postHTTPS(const char* url, const String& payload);

void setup() {

}

void loop() {

    /* --- STATE MACHINE --- */
    switch (state) {
        /* --- LoRa Network Set-up --- */
        case 0:
            /* --- Boot 4G LTE Modem --- */
            modemBoot();
            /* --- Attach to LTE Network --- */
            networkAttach();
            /* --- Sync RTC with LTE Modem --- */
            syncRTC();

            /* --- BOOT LoRa MODULE --- */
            /* --- SENDS TIME INFO EVER 5s TO SYNC SLAVE RTC FOR 5 MINS --- */
            /* --- SHUT DOWN LoRa MODULE --- */
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

/* ================================================================ */
/*  ATTACH to LTE NETWORK (T-Mobile)                                */
/* ================================================================ */
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
        SerialUSB.println(F("❌ CGATT FAIL"));
        return;
    }

    /* 3️  activate context & get IP                                */
    sendAT("AT+CGACT=1,1", 5000);
    String ipResp = sendAT("AT+CGPADDR=1", 3000);                  // +CGPADDR: 1,10.x.x.x

    SerialUSB.print(F("IP RESP: ")); 
    SerialUSB.println(ipResp);
    if (ipResp.indexOf('.') < 0) {                                 // no dot → no IP
        SerialUSB.println(F("❌ NO IP"));
        return;
    }
}

/* ================================================================ */
/*  Generic AT helper                                               */
/* ================================================================ */
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

/* --- SYNC RTC CLOCK WITH LTE MODEM --- */
void syncRTC() {
    // 1. Ask the module for its clock:  +CCLK: "yy/MM/dd,hh:mm:ss±zz"
    String resp = sendAT("AT+CCLK?", 1000, false);    // e.g. +CCLK: "25/06/25,14:53:07+32"

    int q1 = resp.indexOf('\"');
    int q2 = resp.indexOf('\"', q1 + 1);
    if (q1 < 0 || q2 < 0) {
        SerialUSB.println(F("❌ CCLK malformed"));
        return;                                       // malformed / timeout
    }

    String ts = resp.substring(q1 + 1, q2);           // 25/06/25,14:53:07+32

    // 2. Split out the fields
    int year   = 2000 + ts.substring(0, 2).toInt();   // 20yy
    int month  =          ts.substring(3, 5).toInt();
    int day    =          ts.substring(6, 8).toInt();
    int hour   =          ts.substring(9,11).toInt();
    int minute =          ts.substring(12,14).toInt();
    int second =          ts.substring(15,17).toInt();
    // (zone ±zz is ignored; rtc stores local time)

    // 3. Load RTCZero
    rtc.setTime(hour, minute, second);
    rtc.setDate(day, month, year);
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