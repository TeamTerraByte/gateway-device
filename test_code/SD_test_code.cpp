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

void setup()
{

  SerialUSB.begin(115200);
  //while (!SerialUSB)
  {
    ; // wait for Arduino serial Monitor port to connect
  }
  Serial1.begin(115200);

  //Serial1.begin(UART_BAUD, SERIAL_8N1, MODEM_RXD, MODEM_TXD);

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
 // digitalWrite(LTE_FLIGHT_PIN, HIGH);//Flight Mode
  

  
  
  SerialUSB.println("Maduino Zero 4G _SD_Test Start!");
  
  SerialUSB.print("\nInitializing SD card...");
  if (!SD.begin(PIN_SD_SELECT)) {
    SerialUSB.println("initialization failed!");
    while (1);
  }
  SerialUSB.println("initialization done.");

  // --------------------------------------------------
  // 1)  LOW-LEVEL CARD & VOLUME INFO
  // --------------------------------------------------
  if (!card.init(SPI_HALF_SPEED, PIN_SD_SELECT)) {
    SerialUSB.println("Sd2Card.init() failed");
    while (1);
  }
  
  // only for debugging:
  /*
  uint8_t ctype = card.type();   // SD1, SD2 or SDHC
  SerialUSB.print("Card type   : ");
  if      (ctype == SD_CARD_TYPE_SD1)  SerialUSB.println("SD1");
  else if (ctype == SD_CARD_TYPE_SD2)  SerialUSB.println("SD2");
  else if (ctype == SD_CARD_TYPE_SDHC) SerialUSB.println("SDHC/SDXC");
  else                                 SerialUSB.println("Unknown");

  uint32_t csize = card.cardSize();    // size in 512-byte blocks
  float    gb    = (csize / 2.0) / 1e6;  // 512 B * 2 = 1 KB; /1e6 ⇒ GB
  SerialUSB.print("Card size   : ");
  SerialUSB.print(gb, 2);
  SerialUSB.println(" GB");

  // Optional: check that the whole 64 GB is visible
  if (gb < 60.0)  SerialUSB.println("Card not fully addressable (FAT32 limit?)");
  */

  delay(1000);

  // only write header if file is new or empty
  bool needHeader = true;
  if (SD.exists("test.csv")) {
    File tmp = SD.open("test.csv", FILE_READ);
    if (tmp && tmp.size() > 0) needHeader = false;
    if (tmp) tmp.close();
  }
  if (needHeader) {
    File myFile = SD.open("test.csv", FILE_WRITE);
    if (myFile) {
      SerialUSB.print("Writing header to test.csv...");
      myFile.println("time, temperature humidity, pressure, altitude, latitude, longitude");
      myFile.close();
      SerialUSB.println("done.");
    } else {
      SerialUSB.println("error opening test.csv for header write");
    }
  }
  /*
  myFile = SD.open("test.csv", FILE_WRITE);
  if (!myFile) { SerialUSB.println("error"); while (1); }

  for (uint32_t i = 0; i < 10000; ++i) {
    myFile.print(i);        myFile.print(", ");
    myFile.print(i * 1.5);  myFile.print(", ");
    myFile.print(i * 2);    myFile.print(", ");
    myFile.print(i * 2.5);  myFile.print(", ");
    myFile.print(i * 3.5);  myFile.println();
    if ((i & 0x1F) == 0) myFile.flush();   // sync every 32 rows
  }
  myFile.close();
  */
   
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