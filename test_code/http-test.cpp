#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "secrets.h"

#define DEBUG true

// LTE Control Pins
#define LTE_RESET_PIN 6
#define LTE_PWRKEY_PIN 5
#define LTE_FLIGHT_PIN 7

// #define SD_CS_PIN 4  // Adjust if different
bool first = true;

String Apikey = API_WRITE_KEY; // ThingSpeak API Key

// function prototypes
void resetLTE();
String sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
void flushSerial1Input();
void ltePowerSequence();
void enableTimeUpdates();
String getTime();

void setup() {
  SerialUSB.begin(115200);
  Serial1.begin(115200);

  // LTE module power sequence
  pinMode(LTE_RESET_PIN, OUTPUT);
  pinMode(LTE_PWRKEY_PIN, OUTPUT);
  ltePowerSequence();

  SerialUSB.println("Soil Sensor 4G LTE Ready!");
}

void loop() {
  if(!first){  // avoid running this part for a second time on the first loop
    ltePowerSequence();
  } else {
    first = false;
  }

  // Send to ThingSpeak
  sendAT("AT+HTTPINIT", 2000, DEBUG);
  String http_str = "AT+HTTPPARA=\"URL\",\"http://api.thingspeak.com/update?api_key=" + Apikey + "&field1=" + "Goofy" + "\"";
  sendAT(http_str, 2000, DEBUG);
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/x-www-form-urlencoded\"", 1000, DEBUG);
  sendAT("AT+HTTPACTION=0", 30000, DEBUG);
  sendAT("AT+HTTPTERM", 3000, DEBUG);

  delay(15000);
}

String sendAT(const String& command,
              uint32_t timeout,
            bool dbg){
  String response = "";
  Serial1.println(command);
  long int time = millis();
  while ((time + timeout) > millis()) {
    while (Serial1.available()) {
      char c = Serial1.read();
      response += c;
    }
  }
  if (DEBUG) {
    SerialUSB.print(response);
  }
  return response;
}

void ltePowerSequence() {
    if (DEBUG) SerialUSB.println(F(">> LTE Power Sequence Start"));

    // 1. Hard reset module
    digitalWrite(LTE_RESET_PIN, HIGH);
    delay(100); // assert reset
    digitalWrite(LTE_RESET_PIN, LOW);

    // 2. Power on via PWRKEY toggle
    digitalWrite(LTE_PWRKEY_PIN, HIGH);
    delay(1500); // hold HIGH for power-on trigger
    digitalWrite(LTE_PWRKEY_PIN, LOW);

    // 3. Exit flight mode (enter normal mode)
    digitalWrite(LTE_FLIGHT_PIN, LOW);

    // 4. Wait and check for modem readiness
    delay(2000);
    unsigned long start = millis();
    while (millis() - start < 30000) {  // 30 seconds max wait
        String resp = sendAT("AT", 1000, false);
        if (resp.indexOf("OK") != -1) break;
        delay(1000);
        if (DEBUG) SerialUSB.println(F("Waiting for modem..."));
    }

    // 5. SIM check
    String simStatus = sendAT("AT+CPIN?", 2000);
    if (simStatus.indexOf("READY") == -1) {
        SerialUSB.println(F("SIM not ready - aborting setup."));
        return;
    }

    // 6. Get SIM CCID
    sendAT("AT+CCID", 2000);

    // 7. Network registration
    sendAT("AT+CREG=1", 1000);  // enable unsolicited network status
    for (int i = 0; i < 10; i++) {
        String reg = sendAT("AT+CREG?", 2000);
        if (reg.indexOf(",1") != -1 || reg.indexOf(",5") != -1) break;  // home/roaming
        delay(2000);
        if (DEBUG) SerialUSB.println(F("Waiting for network registration..."));
    }

    // 8. Attach to packet domain
    sendAT("AT+CGATT=1", 2000);

    // 9. Define PDP context (APN first!)
    sendAT("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"", 2000);

    // 10. Activate PDP context
    sendAT("AT+CGACT=1,1", 2000);

    // 11. Verify the PDP address (get IP)
    sendAT("AT+CGPADDR=1", 3000);

    // 12. Enable time synchronization from network
    enableTimeUpdates();

    if (DEBUG) SerialUSB.println(F("<< LTE Power Sequence Complete"));
}

void enableTimeUpdates(){
  String r = sendAT("AT+CTZU=1");
}

String getTime(){
  String time = sendAT("AT+CCLK?");
  int q_index = time.indexOf("\"");
  time = time.substring(q_index + 1, q_index + 21);

  if (DEBUG) SerialUSB.println("getTime() response:"+time);

  return time;
}
