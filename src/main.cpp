#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <RTCZero.h>
#include <ArduinoLowPower.h>

#define BAUD 115200
#define DEBUG true

/* --- PINS --- */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7

/* --- CONSTANTS --- */
const char HTTPS_URL[] =
  "https://script.google.com/macros/s/AKfycbybv1kcHIaqrGik924pnxW2a3ZmDXkeCn56Kjliggc3300nkH5x6I6uC7_Eg2qZ_i4F/exec";

/* --- STATE MACHINE POINTER --- */
uint8_t state = 0;

/* --- DEEP SLEEP TIME VARIABLES --- */
uint32_t heartBeatInterval = 3600000; // 1 hour in milliseconds
uint8_t hoursInDay = 0; // Counter for hours in a day

/* --- RTC OBJECT --- */
RTCZero rtc;

void setup() {

}

void loop() {

    /* --- STATE MACHINE --- */
    switch (state) {
        /* --- LoRa Network Set-up --- */
        case 0:
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

/* --- TURN OFF MODEM --- */
void modemOff() {
    sendAT("AT+CPOF", 1000, false);  // turn off modem
    digitalWrite(LTE_PWRKEY_PIN, HIGH);
}

/* --- ATTACH to LTE NETWORK --- */
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


/* --- AT WRAPPER --- */
String sendAT(const String& cmd, uint32_t to, bool dbg) {
    String resp;
    Serial1.println(cmd);
    unsigned long t0 = millis();
    while (millis() - t0 < to) while (Serial1.available()) resp += (char)Serial1.read();
    if (dbg) SerialUSB.print(resp);
    return resp;
}

/* --- POST via HTTPS --- */
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
