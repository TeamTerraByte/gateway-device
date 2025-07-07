#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <secrets.h>

#define DEBUG true

// LTE Control Pins
#define LTE_RESET_PIN 6
#define LTE_PWRKEY_PIN 5
#define LTE_FLIGHT_PIN 7

// #define SD_CS_PIN 4  // Adjust if different
bool first = true;

// String Apikey = API_WRITE_KEY; // ThingSpeak API Key
File logFile;

// function prototypes
void resetLTE();
String sendData(String command, const int timeout, boolean debug);
void flushSerial1Input();
void ltePowerSequence();

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
  sendData("AT+HTTPINIT", 2000, DEBUG);
  // String http_str = "AT+HTTPPARA=\"URL\",\"http://api.thingspeak.com/update?api_key=" + Apikey + "&field1=" + String((int)67) + "\"";
  String http_str = "AT+HTTPPARA=\"URL\",\"http://api.thingspeak.com\"";
  // String http_str = "AT+HTTPPARA=\"URL\",\"https://www.google.com\"";
  sendData(http_str, 2000, DEBUG);
  sendData("AT+HTTPPARA=\"CONTENT\",\"application/x-www-form-urlencoded\"", 1000, DEBUG);
  sendData("AT+HTTPACTION=0", 30000, DEBUG);
  sendData("AT+HTTPTERM", 3000, DEBUG);

  delay(30000);
}

String sendData(String command, const int timeout, boolean debug) {
  String response = "";
  Serial1.println(command);
  long int time = millis();
  while ((time + timeout) > millis()) {
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

void ltePowerSequence(){
  delay(100);
  digitalWrite(LTE_RESET_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_RESET_PIN, LOW);

  
  delay(100);
  digitalWrite(LTE_PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_PWRKEY_PIN, LOW);

  pinMode(LTE_FLIGHT_PIN, OUTPUT);
  digitalWrite(LTE_FLIGHT_PIN, LOW); // Normal mode

  delay(30000); // Wait for LTE module

  // LTE network setup
  sendData("AT+CCID", 3000, DEBUG);
  sendData("AT+CREG?", 3000, DEBUG);
  sendData("AT+CGATT=1", 1000, DEBUG);
  sendData("AT+CGACT=1,1", 1000, DEBUG);
  sendData("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"", 1000, DEBUG);
  sendData("AT+CGPADDR=1", 3000, DEBUG);          // show pdp address
}