#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#define DEBUG                   true
#define SAMPLE_INTERVAL_MS      (10000)
#define BAUD                    115200

/* --- SIM7600 PINS --- */
#define LTE_RESET_PIN           6
#define LTE_PWRKEY_PIN          5
#define LTE_FLIGHT_PIN          7
/* --- SD CARD PIN --- */
#define PIN_SD                  4
/* --- HTTPS WEB SERVER URL --- */
const char HTTPS_URL[]          = "https://script.google.com/macros/s/AKfycbxPDgN6Pmbi4LG1VH8K5ErdkMbWP5-nEJSJA-VsOoK8_l-gyn2nMPR9i8G2-jwduUnt-A/exec";

Sd2Card                         card;
File                            myFile;

/* --- Function Declarations --- */
String sendData(String command, const int timeout = 2000, boolean debug = DEBUG);
String getTimeStr();
void initSIM();
void appendSample();
void postBatch();
void initSD();


/* --- Setup --- */
void setup() {
    SerialUSB.begin(BAUD);
    Serial1.begin(BAUD);
    // init SIM card module
    initSIM();
    // Init SD card module
    initSD();
    

}
/* --- MAIN FUNCTION --- */
void loop() {
    ;
}

/* --- Initiates SIM7600 Module --- */
void initSIM() {
    pinMode(LTE_RESET_PIN, OUTPUT);
    digitalWrite(LTE_RESET_PIN, LOW);

    pinMode(LTE_PWRKEY_PIN, OUTPUT);
    digitalWrite(LTE_RESET_PIN, LOW);
    delay(100);
    digitalWrite(LTE_PWRKEY_PIN, HIGH);
    delay(2000);
    digitalWrite(LTE_PWRKEY_PIN, LOW);
  
    pinMode(LTE_FLIGHT_PIN, OUTPUT);
    digitalWrite(LTE_FLIGHT_PIN, LOW);//Normal Mode
  
    delay(5000);
}
/* --- SEND DATA HELPER --- */
String sendData(String command, const int timeout, boolean debug) {
    String response = "";
    Serial1.println(command);
  
    unsigned long start = millis();
    while (millis() - start < timeout) {
        while (Serial1.available()) {
            char c = Serial1.read();
            response += c;
        }
    }
    if (debug) {
        SerialUSB.print(response);
    }
    return response;
}

/* --- POST TO HTTPS LOGIC --- */
void postHTTPS(const char* https_url) {
    sendData("AT+HTTPINIT", 1000, DEBUG);
    sendData("AT+HTTPPARA=\"CID\",1", 1000, DEBUG);
    sendData("AT+HTTPPARA=\"URL\",\"" + String(https_url) + "\"", 1000, DEBUG);
    sendData("AT+HTTPPARA=\"CONTENT\",\"text/plain\"", 1000, DEBUG);

    // prepare payload
    String payload = "hello-world,1234,T,24.1,24.2,24.3,24.4,24.5,24.6,24.7,24.8\n";

    // wait for DOWNLOAD prompt
    String resp = sendData("AT+HTTPDATA=" + String(payload.length()) + ",10000", 1000, DEBUG);
    if (resp.indexOf("DOWNLOAD") >= 0) {
        Serial1.print(payload);
        delay(100);
    } else {
        SerialUSB.println("ERROR: HTTPDATA rejected");
        return;
    }

    String actionResp = sendData("AT+HTTPACTION=1", 8000, DEBUG);
    SerialUSB.println("ACTION: " + actionResp);

    String reply = sendData("AT+HTTPREAD", 3000, DEBUG);
    SerialUSB.println("REPLY: " + reply);

    sendData("AT+HTTPTERM", 1000, DEBUG);
}

/* --- INITIATE SD CARD --- */
void initSD() {
    if (!SD.begin(PIN_SD) || !card.init(SPI_HALF_SPEED, PIN_SD)) {
        SerialUSB.println("SD fail"); while (1);
    }
}

/* --- GET TIME --- */
String getTimeStr() {                     // returns "25/06/12,05:16:12+00"
    String r = sendData("AT+CCLK?", 1000, false);
    int q1=r.indexOf('\"'), q2=r.indexOf('\"',q1+1);
    return (q1>=0 && q2>q1) ? r.substring(q1+1,q2) : "00/00/00,00:00:00+00";
}

void initNetwork() {
    sendData("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"");
    sendData("AT+CGATT=1");
}