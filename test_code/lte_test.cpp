#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#define BAUD 115200
#define DEBUG true

/* ---------- PIN MAP ------------------------------------------------ */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7
/* ------------------------------------------------------------------- */

/* ---------- CONSTANTS ---------------------------------------------- */
const char HTTPS_URL[] =
  "https://script.google.com/macros/s/AKfycbybv1kcHIaqrGik924pnxW2a3ZmDXkeCn56Kjliggc3300nkH5x6I6uC7_Eg2qZ_i4F/exec";
/* ------------------------------------------------------------------- */

/* ---------- PROTOTYPES --------------------------------------------- */
String  sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
bool    modemBoot();
bool    networkAttach();
bool    postHTTPS(const char* url, const String& payload);
/* ------------------------------------------------------------------- */

/* ================================ SETUP ============================ */
void setup() {
    SerialUSB.begin(BAUD);
    Serial1.begin(BAUD);

    if (!modemBoot()) { while (1); }               // halt on fail
    if (!networkAttach()) { while (1); }           // halt on fail

    /* ------- TEST PAYLOAD ------------------------------------------- */
    String payload = "hello-world\n1234,T,24,25,26,27,28,29,30,31\n";

    if (postHTTPS(HTTPS_URL, payload))
        SerialUSB.println(F("\nUPLOAD OK"));
    else
        SerialUSB.println(F("\nUPLOAD FAIL"));
}

void loop() { /* nothing */ }

/* =============================== HELPERS =========================== */

/* Boot sequence: pulse PWRKEY 2 s, wait 5 s for modem ready */
bool modemBoot() {
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
    return false;
    }
    SerialUSB.println(F("MODEM READY"));
    return true;
}

/* Attach to T-Mobile LTE (fast.t-mobile.com) */
bool networkAttach() {
    sendAT("AT+CFUN=1");
    sendAT("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"");   // APN
    if (sendAT("AT+CGATT=1", 5000).indexOf("OK") < 0) {
        SerialUSB.println(F("CGATT FAIL")); return false;
    }

    sendAT("AT+CGACT=1,1", 5000);                          // ← new: activate PDP
    String ipResp = sendAT("AT+CGPADDR=1", 3000);          // ← new: get IP
    SerialUSB.print(F("IP RESP: ")); SerialUSB.println(ipResp);

    if (ipResp.indexOf('.') < 0) {                         // no dot → no IP
        SerialUSB.println(F("NO IP")); return false;
    }
    return true;
}


/* General AT wrapper */
String sendAT(const String& cmd, uint32_t to, bool dbg) {
    String resp;
    Serial1.println(cmd);
    unsigned long t0 = millis();
    while (millis() - t0 < to) while (Serial1.available()) resp += (char)Serial1.read();
    if (dbg) SerialUSB.print(resp);
    return resp;
}

/* Post a small text payload via HTTPS */
bool postHTTPS(const char* url, const String& payload) {
    sendAT("AT+HTTPTERM",  1000, false);   // clean slate
    sendAT("AT+HTTPINIT");
    sendAT("AT+HTTPPARA=\"CID\",1");
    sendAT("AT+HTTPPARA=\"URL\",\"" + String(url) + "\"");
    sendAT("AT+HTTPPARA=\"CONTENT\",\"text/plain\"");

    String r = sendAT("AT+HTTPDATA=" + String(payload.length()) + ",10000", 1000);
    if (r.indexOf("DOWNLOAD") < 0) { SerialUSB.println(F("HTTPDATA ERR")); return false; }

    Serial1.print(payload);                     // send body
    delay(100);

    String act = sendAT("AT+HTTPACTION=1", 8000);
    if (act.indexOf(",200,") < 0) { SerialUSB.println(F("HTTPACTION FAIL")); return false; }

    String body = sendAT("AT+HTTPREAD", 3000);
    sendAT("AT+HTTPTERM");

    return (body.indexOf("Success") >= 0);
}
