#include <Arduino.h>
#define DEBUG true

String sendAT(const String &cmd, uint32_t timeout = 2000, bool dbg = DEBUG);
String getCoords();

void setup(){
    SerialUSB.begin(115200);
    while(!SerialUSB);
    SerialUSB.println("Setup complete");
}

void loop(){
    SerialUSB.println(getCoords());
    delay(2000);
}


String getCoords(){
  sendAT("AT+CGPS=1,1");                   // Start GNSS in standalone mode
  delay(1000);                             // Allow GNSS to initialize

  String resp = sendAT("AT+CGPSINFO");
  sendAT("AT+CGPS=0");                     // Turn GNSS off after use
  
  int colon = resp.indexOf(':');
  if (colon < 0) {
    SerialUSB.println(F("Malformed CGPSINFO"));
    return "0.0,0.0,0";
  }

  String payload = resp.substring(colon + 1);
  payload.trim();

  if (payload.indexOf(",,") >= 0) {
    SerialUSB.println(F("No GPS fix"));
    return "0.0,0.0,0";
  }

  // Extract fields: lat, N/S, lon, E/W, date, time, alt
  char latStr[16], ns, lonStr[16], ew;
  char date[7], time[10];
  float alt;

  int parsed = sscanf(payload.c_str(), "%15[^,],%c,%15[^,],%c,%6s,%9s,%f",
                      latStr, &ns, lonStr, &ew, date, time, &alt);
  if (parsed < 7) {
    SerialUSB.println(F("Failed to parse CGPSINFO fields"));
    return "0.0,0.0,0";
  }

  // ---- Convert to decimal degrees ----
  float latDeg = atof(latStr);
  float lonDeg = atof(lonStr);

  float lat_dd = int(latDeg / 100);
  float lat_mm = latDeg - lat_dd * 100;
  float latitude = lat_dd + lat_mm / 60.0;
  if (ns == 'S') latitude *= -1;

  float lon_dd = int(lonDeg / 100);
  float lon_mm = lonDeg - lon_dd * 100;
  float longitude = lon_dd + lon_mm / 60.0;
  if (ew == 'W') longitude *= -1;

  String coords = (String) longitude + "," + (String)latitude + "," + (String)alt;

  return coords;
}

String sendAT(const String &cmd, uint32_t timeout, bool dbg) {
  Serial1.println(cmd);
  uint32_t t0 = millis();
  String buffer;
  while (millis() - t0 < timeout) {
    while (Serial1.available()) {
      char c = Serial1.read();
      buffer += c;
    }
  }
  if (dbg) {
    SerialUSB.print(cmd); SerialUSB.print(F(" → ")); SerialUSB.println(buffer);
  }
  return buffer;
}