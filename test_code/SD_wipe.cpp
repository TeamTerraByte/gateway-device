/*
   wipe_sd_recursive.ino  –  Purge an SD card using only SD.h
   Board tested : Maduino Zero 4G LTE   (CS pin D4)
   Library      : SD.h (ships with most cores)
   DANGER       : This erases ALL data.  There is NO recovery.
*/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

const uint8_t SD_CS_PIN = 4;        // Chip-select for your socket

void listDir(File dir, uint8_t depth);        // forward decl.
void wipeDir(File dir);                       // forward decl.

void setup()
{
  SerialUSB.begin(115200);
  while (!SerialUSB) ;                        // wait for CDC

  SerialUSB.println(F("\n=== SD wipe (SD.h only) ==="));

  if (!SD.begin(SD_CS_PIN)) {
    SerialUSB.println(F("SD initialisation failed."));
    while (1) yield();
  }

  // Show what’s on the card now
  SerialUSB.println(F("\nContents BEFORE wipe:"));
  listDir(SD.open("/"), 0);

  // ---- PERFORM THE WIPE --------------------------------------------
  File root = SD.open("/");
  if (!root) {
    SerialUSB.println(F("Cannot open root directory!"));
    while (1) yield();
  }

  wipeDir(root);      // ← irreversible
  root.close();

  // Verify card is empty
  SerialUSB.println(F("\nContents AFTER wipe:"));
  listDir(SD.open("/"), 0);

  SerialUSB.println(F("\nCard successfully wiped."));
}

void loop() { /* nothing */ }

/* ------------------------------------------------------------------ */
/* RECURSIVE DELETE: remove files first, then empty sub-directories   */
void wipeDir(File dir)
{
  if (!dir) return;

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;                 // no more entries

    String path = entry.name();        // keep name before close

    if (entry.isDirectory()) {
      wipeDir(entry);                  // depth-first
      entry.close();                   // must close before rmdir
      SD.rmdir(path.c_str());          // remove now-empty dir
    } else {
      entry.close();
      SD.remove(path.c_str());         // delete file
    }
  }
  // DO NOT call SD.rmdir("/") – root itself must remain
}

/* ------------------------------------------------------------------ */
/* SIMPLE DIRECTORY LIST (for user feedback)                          */
void listDir(File dir, uint8_t depth)
{
  if (!dir) return;

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    for (uint8_t i = 0; i < depth; ++i) SerialUSB.print(' ');
    SerialUSB.print(entry.name());

    if (entry.isDirectory()) {
      SerialUSB.println('/');
      listDir(entry, depth + 2);
    } else {
      SerialUSB.print(F(" (")); SerialUSB.print(entry.size());
      SerialUSB.println(F(" bytes)"));
    }
    entry.close();
  }
  dir.close();
}
