/**********************************************************************
 *  Maduino Zero 4G  — weekly batch uploader (demo)
 *********************************************************************/
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

/* -------- user config ------------------------------------------- */
#define DEBUG                true
const  int  PIN_SD_SELECT    = 4;
const  char DATA_FILE[]      = "data.csv";
const  char WEB_URL[]        = "https://script.google.com/macros/s/AKfycbybv1kcHIaqrGik924pnxW2a3ZmDXkeCn56Kjliggc3300nkH5x6I6uC7_Eg2qZ_i4F/exec";
#define SAMPLE_INTERVAL_MS   (10000)   // 10 sec   (set to 10000 for quick test)
/* ---------------------------------------------------------------- */

/* -------- SIM7600 pins ------------------------------------------ */
#define LTE_RESET_PIN  6
#define LTE_PWRKEY_PIN 5
#define LTE_FLIGHT_PIN 7
/* ---------------------------------------------------------------- */

Sd2Card card;
File    myFile;

/* ---------------------------------------------------------------- */
String sendData(const String& cmd, uint32_t to=2000, bool dbg=DEBUG);
String getTimeStr();
void   appendSample();
bool   postBatch();

/* ----------------------------- */
uint32_t lastSample = 0;
void setup()
{
  SerialUSB.begin(115200);
  Serial1.begin(115200);

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

  /* SD init */
  if (!SD.begin(PIN_SD_SELECT) || !card.init(SPI_HALF_SPEED, PIN_SD_SELECT)) {
    SerialUSB.println("SD fail"); while (1);
  }

  /* NTP time sync (T-Mobile APN) */
  sendData("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"");
  sendData("AT+CGATT=1");
  sendData("AT+CNTP=\"pool.ntp.org\",0");
  sendData("AT+CNTP"); delay(6000);
  SerialUSB.println("Time synced: " + getTimeStr());

  SerialUSB.println("\n> 'u' = upload batch, ENTER = AT pass-through");
}

/* ---------------------------------------------------------------- */
void loop()
{
  // --- HTTPS --- //
  
  /* 30-min sampling */
  if (millis() - lastSample >= SAMPLE_INTERVAL_MS) {
    appendSample();
    lastSample = millis();
  }

  /* serial bridge & command */
  static String buf;
  while (SerialUSB.available()) {
    char c = SerialUSB.read();
    if (c == 'u') {                         // user-triggered upload
      if (postBatch()) SerialUSB.println("Upload OK");
      else             SerialUSB.println("Upload FAIL");
    } else if (c=='\r'||c=='\n') {
      if (buf.length()) { sendData(buf); buf=""; }
    } else buf += c;
  }

  while (Serial1.available()) SerialUSB.write(Serial1.read());
}

/* ================= helper functions ============================== */
String sendData(const String& cmd, uint32_t to, bool dbg)
{
  String rsp; Serial1.println(cmd);
  uint32_t t0 = millis();
  while (millis() - t0 < to) while (Serial1.available()) rsp += (char)Serial1.read();
  if (dbg && rsp.length()) SerialUSB.print(rsp);
  return rsp;
}

String getTimeStr() {                     // returns "25/06/12,05:16:12+00"
  String r = sendData("AT+CCLK?", 1000, false);
  int q1=r.indexOf('\"'), q2=r.indexOf('\"',q1+1);
  return (q1>=0 && q2>q1) ? r.substring(q1+1,q2) : "00/00/00,00:00:00+00";
}

/* ------- generate & store one pair of rows ---------------------- */
void appendSample()
{
  uint32_t ts = millis() / 1000UL;        // crude: seconds since boot; replace with real Unix if needed
  float t[8], m[8];
  for (int i=0;i<8;i++){ t[i]=random(200,300)/10.0; m[i]=random(400,600)/10.0; }

  myFile = SD.open(DATA_FILE, FILE_WRITE);
  if (!myFile) { SerialUSB.println("SD write err"); return; }

  myFile.print(ts); myFile.print(",T");
  for (float v: t) { myFile.print(','); myFile.print(v,1); } myFile.println();

  myFile.print(ts); myFile.print(",M");
  for (float v: m) { myFile.print(','); myFile.print(v,1); } myFile.println();

  myFile.close();
  SerialUSB.println("Logged sample @" + String(ts));
}

/* ------- POST the whole file ------------------------------------ */
bool postBatch()
{
  myFile = SD.open(DATA_FILE, FILE_READ);
  if (!myFile) { SerialUSB.println("no batch"); return false; }

  String payload="";
  while (myFile.available()) payload += (char)myFile.read();
  myFile.close();

  /* HTTP POST */
  sendData("AT+HTTPTERM",1000,false);           // clean slate
  sendData("AT+HTTPINIT");
  sendData("AT+HTTPPARA=\"CID\",1");
  sendData("AT+HTTPPARA=\"URL\",\""+String(WEB_URL)+"\"");
  sendData("AT+HTTPPARA=\"CONTENT\",\"text/plain\"");
  sendData("AT+HTTPDATA="+String(payload.length())+",10000");
  delay(100); Serial1.print(payload); delay(200);
  sendData("AT+HTTPACTION=1", 8000);
  String r = sendData("AT+HTTPREAD", 3000);
  sendData("AT+HTTPTERM");

  if (r.indexOf("Success") >= 0) {
    SD.remove(DATA_FILE);                 // clear after successful upload
    return true;
  }
  return false;
}
