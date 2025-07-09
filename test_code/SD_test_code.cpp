#include <stdio.h>
#include <string.h>
#include <Arduino.h>

#define DEBUG true


#include <SPI.h>
#include <SD.h>

#define LTE_RESET_PIN 6

#define LTE_PWRKEY_PIN 5

#define LTE_FLIGHT_PIN 7

// set up variables using the SD utility library functions:
Sd2Card card;
SdVolume volume;
SdFile root;

// change this to match your SD shield or module;
// Maduino zero 4G LTE: pin 4
const int PIN_SD_SELECT = 4;

#define UART_BAUD           115200

#define MODEM_RXD          0
#define MODEM_TXD          1

File myFile;

void setup() {
  SerialUSB.begin(115200);
  while (!SerialUSB);

  SerialUSB.println("Maduino Zero 4G _SD_Test Start!");

  // Init SD
  SerialUSB.print("Initializing SD card...");
  if (!SD.begin(PIN_SD_SELECT)) {
    SerialUSB.println("initialization failed!");
    while (1);
  }
  SerialUSB.println("initialization done.");

  // Write header if needed…
  bool needHeader = true;
  if (SD.exists("test.csv")) {
    File tmp = SD.open("test.csv", FILE_READ);
    if (tmp && tmp.size() > 0) needHeader = false;
    if (tmp) tmp.close();
  }
  if (needHeader) {
    File hdr = SD.open("test.csv", FILE_WRITE);
    if (hdr) {
      SerialUSB.print("Writing header… ");
      hdr.println("time, temperature, humidity, pressure, altitude, latitude, longitude");
      hdr.close();
      SerialUSB.println("done.");
    }
  }

  // ─── Write one line of test data ───────────────────────────────────
  {
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

  // ─── Read back the file contents ──────────────────────────────────
  {
    SerialUSB.println("Reading back test.csv contents:");
    File readFile = SD.open("test.csv", FILE_READ);
    if (!readFile) {
      SerialUSB.println("Error opening test.csv for read");
    } else {
      while (readFile.available()) {
        SerialUSB.write(readFile.read());
      }
      readFile.close();
    }
    SerialUSB.println("\n<End of file>");
  }
}


void loop()
{
  //SerialUSB.println("echo test!");
  while (Serial1.available() > 0) {
    SerialUSB.write(Serial1.read());
    yield();
  }
  while (SerialUSB.available() > 0) {
    Serial1.write(SerialUSB.read());
    yield();
  }
}


String sendData(String command, const int timeout, boolean debug)
{
  String response = "";
  Serial1.println(command);
  long int time = millis();
  while ( (time + timeout) > millis())
  {
    while (Serial1.available())
    {
      char c = Serial1.read();
      response += c;
    }
  }
  if (debug)
  {
    SerialUSB.print(response);
  }
  return response;
}