#include <Arduino.h>

// LTE Control Pin Definitions
#define LTE_RESET_PIN     6
#define LTE_PWRKEY_PIN    5
#define LTE_FLIGHT_PIN    7

#define SIM7600_BAUD      115200

// Forward Declarations
void sendAT(const char* cmd);
String parseCoordinates(const String& nmeaLine);
void initLTE();

// Initialize Serial ports and power up the module
void setup() {
  SerialUSB.begin(115200);
  Serial1.begin(SIM7600_BAUD);

  // Set control pin modes
  pinMode(LTE_PWRKEY_PIN, OUTPUT);
  pinMode(LTE_RESET_PIN, OUTPUT);
  pinMode(LTE_FLIGHT_PIN, OUTPUT);

  // Optional: Set all LTE control pins LOW initially
  digitalWrite(LTE_PWRKEY_PIN, LOW);
  digitalWrite(LTE_RESET_PIN, LOW);
  digitalWrite(LTE_FLIGHT_PIN, LOW);

  initLTE(); // Power up SIM7600 module

  delay(5000);  // Wait for module to initialize

  SerialUSB.println("SIM7600 GPS Test Begin...");

  sendAT("AT");              // Basic check
  sendAT("ATE0");            // Disable echo
  sendAT("AT+CGPS=1,1");     // Power on GPS in standalone mode
}

void loop() {
  sendAT("AT+CGPSINFO");     // Request GPS data

  if (Serial1.available()) {
    String gpsInfo = Serial1.readStringUntil('\n');
    if (gpsInfo.indexOf("+CGPSINFO:") >= 0) {
      gpsInfo.trim();
      SerialUSB.print("GPS Raw: ");
      SerialUSB.println(gpsInfo);

      String coords = parseCoordinates(gpsInfo);
      if (coords.length() > 0)
        SerialUSB.println(coords);
      else
        SerialUSB.println("Waiting for GPS fix...");
    }
  }

  delay(2000);
}

// Function to send AT commands to SIM7600
void sendAT(const char* cmd) {
  Serial1.println(cmd);
  delay(500); // Allow time for response
  while (Serial1.available())
    SerialUSB.write(Serial1.read());
}

// Extracts latitude and longitude from +CGPSINFO response
String parseCoordinates(const String& nmeaLine) {
  int idx = nmeaLine.indexOf(":");
  if (idx < 0) return "";

  String data = nmeaLine.substring(idx + 1);
  data.trim();

  // Data format: <lat>,<N/S>,<long>,<E/W>,<date>,<utc>,<alt>,<speed>
  int firstComma = data.indexOf(',');
  if (firstComma < 0 || data[firstComma + 1] == ',') return ""; // No GPS fix

  String lat = data.substring(0, firstComma);
  int secondComma = data.indexOf(',', firstComma + 1);
  String ns = data.substring(firstComma + 1, secondComma);

  int thirdComma = data.indexOf(',', secondComma + 1);
  String lon = data.substring(secondComma + 1, thirdComma);
  int fourthComma = data.indexOf(',', thirdComma + 1);
  String ew = data.substring(thirdComma + 1, fourthComma);

  if (lat.length() == 0 || lon.length() == 0)
    return "";

  return "Latitude: " + lat + " " + ns + ", Longitude: " + lon + " " + ew;
}

// Power-up sequence for SIM7600 module
void initLTE() {
  SerialUSB.println("Powering on LTE module...");

  // Pull PWRKEY low for at least 1 second to turn on the module
  digitalWrite(LTE_PWRKEY_PIN, HIGH);
  delay(1000);
  digitalWrite(LTE_PWRKEY_PIN, LOW);

  // Optional: Set FLIGHT mode to 0 (if used)
  digitalWrite(LTE_FLIGHT_PIN, LOW);
}
