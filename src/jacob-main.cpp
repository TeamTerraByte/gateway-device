#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <RTCZero.h>
#include <ArduinoLowPower.h>
#include <algorithm>
#include <Wire.h>
#include <secrets.h>

#define BAUD 115200
#define DEBUG true

/* --- PINS --- */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7
#define relayPin 3

#define SLAVE_ADDRESS 0x08

/* --- SENSOR DATA --- */
String moistBuf = "";
String tempBuf  = "";
String curType  = "";
bool   assembling = false;          // true while a block is still arriving


/* --- CONSTANTS --- */
/* ---------- THINGSPEAK --------------------------------- */
const char* TS_API_KEY   = API_WRITE_KEY;
const char TS_BASE_URL[]  = "http://api.thingspeak.com/update";
const uint8_t TS_MAX_FLD  = 8;

const int PIN_SD_SELECT = 4;

/* --- STATE MACHINE POINTER --- */
uint8_t state = 0;

/* --- DEEP SLEEP TIME VARIABLES --- */
uint32_t heartBeatInterval = 3600000; // 1 hour in milliseconds
uint8_t hoursInDay = 0; // Counter for hours in a day


struct GNSS {
    float latitude;
    float longitude;
    float altitude;
};
GNSS location{0.0, 0.0, 0.0};


/* --- FUNCTION DECLARATIONS --- */
void ltePowerSequence();
void modemOff();
String sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
void enableTimeUpdates();
String getTime();
bool uploadData(const String& payload);
bool sdInit();
bool sdHasCsvFiles();
bool sdUploadChrono();
bool sdDeleteCsv(const char* name);
void processChunk(const String& data);
void sampleData();
String tsvToFieldString(const String &tsvLine);


/* ======================================================== */
/* |----------------------- SETUP ------------------------| */
/* ======================================================== */
void setup(){
    SerialUSB.begin(BAUD);
    if (DEBUG) while(!SerialUSB);  // wait
    delay(1000);
    Serial1.begin(BAUD);
    while (!Serial1);

    /* --- INITIALIZE SD CARD --- */
    if (!sdInit()) {
        SerialUSB.println(F("SD CARD NOT READY!"));
        SerialUSB.println(F("Please check the SD card connection."));
        while (true) {
            delay(1000);
            SerialUSB.println(F("Waiting for SD card..."));
        }   
    }

    /* --- INITIATE I2C FOR ENVIROPRO --- */
    Wire.begin(SLAVE_ADDRESS);
    Wire.onReceive([](int /*n*/) {
        String chunk;
        while (Wire.available())           // read all bytes of this I²C packet
            chunk += char(Wire.read());
        processChunk(chunk);               // assemble
    });

    state = 0;
    SerialUSB.println("Setup complete!");
}

/* ======================================================== */
/* |----------------- MAIN STATE MACHINE -----------------| */
/* ======================================================== */
void loop(){
    switch(state) {
        /* --- GATEWAY AND TRANSMIT --- */
        case 0:
            if (DEBUG) SerialUSB.println("State 0");
            /* --- Boot SIM7600 Modem --- */
            // ltePowerSequence();  // this is called in uploadData which is called by sdUploadChrono
            /* --- Sync System Time and Location --- */
            enableTimeUpdates();

            /* --- Upload Data via HTTPS --- */
            if (sdHasCsvFiles()) {
                SerialUSB.println(F("Uploading saved data..."));
                if (sdUploadChrono()) {
                    SerialUSB.println(F("Data upload successful."));
                    sdDeleteCsv("data.csv"); // needs to be modified to delete all files
                }
                else {
                    SerialUSB.println("Data upload unsuccessful");
                }
            }
            delay(1000); // Allow time for the sensor to power up
            /* --- Sample Data from Sensors --- */
            sampleData();

            hoursInDay = 0;
            state = 1;
            break;
        /* --- WAITING MODE --- */
        case 1:
            if (DEBUG) SerialUSB.println("State 1");
            
            if ( hoursInDay < 1 ) {
                delay(15000); // simulate 15 seconds of Low power mode
                state = 2;
                hoursInDay++;
                break;
            } else {
                state = 0;
                hoursInDay = 0; // Reset the counter after 24 hours
                break;
            }
            break;
        /* --- COLLECTION MODE --- */
        case 2:
            // if (DEBUG) SerialUSB.println("State 2");
            state = 1; // Return to low power mode
            break;
        default:
            SerialUSB.println(F("Invalid state!"));
            state = 0;
    }
}



/* ======================================================== */
/* |--------------- FUNCTION DEFINITIONS -----------------| */
/* ======================================================== */
void ltePowerSequence(){
    if (DEBUG) SerialUSB.println("Initiating ltePowerSequence");
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
    sendAT("AT+CCID", 3000, DEBUG);
    sendAT("AT+CREG?", 3000, DEBUG);
    sendAT("AT+CGATT=1", 1000, DEBUG);
    sendAT("AT+CGACT=1,1", 1000, DEBUG);
    sendAT("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"", 1000, DEBUG);
    sendAT("AT+CGPADDR=1", 3000, DEBUG);          // show pdp address
}

void modemOff() {
    sendAT("AT+CPOF", 1000, false);  // turn off modem
    digitalWrite(LTE_PWRKEY_PIN, HIGH);
}

/* --- SEND AT COMMAND to 4G LTE MODULE --- */
String sendAT(const String& cmd, uint32_t to, bool dbg ){
    String resp;
    Serial1.println(cmd);                           // sends CR/LF automatically

    unsigned long t0 = millis();
    while (millis() - t0 < to) {
        while (Serial1.available()) resp += (char)Serial1.read();
    }
    if (dbg && resp.length()) SerialUSB.print(resp);
    return resp;
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

String tsvToFieldString(const String &tsvLine)
{
  String out;
  int start = 0, fieldNo = 1;

  while (start < tsvLine.length()) {
    int end = tsvLine.indexOf('\t', start);    // next TAB
    if (end == -1) end = tsvLine.length();     // last value

    out += "field";
    out += fieldNo++;
    out += '=';
    out += tsvLine.substring(start, end);

    if (end < tsvLine.length()) out += '&';    // no '&' after last field
    start = end + 1;                           // jump past TAB
  }
  return out;
}

bool uploadData(const String& payload) {
    if (DEBUG) SerialUSB.println("uploadData payload: " + payload);
	bool success = false;

	// For some reason, I have only observed consistent success using HTTP
	// if I reset LTE before every query
	ltePowerSequence(); 

	/* ---- Build ThingSpeak URL -------------------------------------- */
	String url = "http://api.thingspeak.com/update?api_key=";
	url += API_WRITE_KEY;
    url += "&";
    url += tsvToFieldString(payload);
    

	SerialUSB.println("\n[HTTP] » " + url);

	/* ---- One-shot HTTP session ------------------------------------- */
	if (sendAT("AT+HTTPTERM", 1000).indexOf("ERROR") == -1) {
		// ignore result – module may reply ERROR if not initialised yet
	}
	if (sendAT("AT+HTTPINIT", 5000).indexOf("OK") == -1) {
		SerialUSB.println(F("HTTPINIT failed – aborting"));
		return false;
	}
	sendAT("AT+HTTPPARA=\"CONTENT\",\"application/x-www-form-urlencoded\"", 1000);
	sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 2000);

	/* Start HTTP GET (method 0) */
	String resp = sendAT("AT+HTTPACTION=0", 30000);
	if (resp.indexOf("+HTTPACTION: 0,200") != -1) {
		SerialUSB.println(F("Upload OK"));
		success = true;
	} else {
		SerialUSB.println("Upload failed: " + resp);
	}
	sendAT("AT+HTTPTERM", 1000);
	
	return success;
}


/* --- MOUNT SD CARD --- */
bool sdInit() {
    static bool ready = false;
    if (!ready) ready = SD.begin(PIN_SD_SELECT);
    return ready;
}

/* --- CHECK IF CSV FILES EXIST --- */
bool sdHasCsvFiles() {
    if (!sdInit()) return false;
    File r = SD.open("/");
    while (File f = r.openNextFile()) {
        if (!f.isDirectory() && String(f.name()).endsWith(".CSV")) {
            f.close(); r.close(); return true;
        }
        f.close();
    }
    r.close();
    return false;
}

/* --- UPLOAD ALL CSV FILES CHRONOLOGICALLY --- */
/* ── Upload every *.CSV row-by-row in chronological order ──────────────── */
bool sdUploadChrono()
{
    if (!sdInit()) return false;

    /* 1 ─ collect filenames */
    String list[32]; uint8_t n = 0;
    File dir = SD.open("/");
    while (File f = dir.openNextFile()) {
        if (!f.isDirectory() && String(f.name()).endsWith(".CSV") && n < 32)
            list[n++] = String(f.name());
        f.close();
    }
    dir.close();
    if (!n) return false;

    /* 2 ─ sort alphabetically (lexicographic ≈ chronological) */
    for (uint8_t i = 0; i < n - 1; ++i)
        for (uint8_t j = i + 1; j < n; ++j)
            if (list[j] < list[i]) std::swap(list[i], list[j]);

    /* 3 ─ stream each file row-by-row */
    for (uint8_t i = 0; i < n; ++i) {
        File f = SD.open(list[i], FILE_READ);
        if (!f) continue;

        String row = "";
        while (f.available()) {
            char c = f.read();
            if (c == '\n') {                    // got one complete row
                row.trim();                     // remove CR if any

                /* skip header or empty lines (contains alpha chars) */
                if (!row.length()) { continue; }

                if (!uploadData(row)) { // push to ThingSpeak
                    f.close(); return false;    // abort on first failure
                }
                row = "";
            }
            else if (c != '\r') row += c;       // build row
        }
        f.close();
        SD.remove(list[i]);                     // delete file after upload
    }
    return true;
}

/* --- DELETE --- */
bool sdDeleteCsv(const char* name) {
    if (!sdInit()) return false;
    return SD.exists(name) && SD.remove(name);
}

/* --- PROCESS I²C CHUNK FROM ENVIROPRO --- */
void processChunk(const String& data)
{
    SerialUSB.println("Processing Chunk: " + data);
    /* 1 ── new transmission header ------------------------ */
    if (data.startsWith("Moist,")) {
        curType     = "Moist";
        moistBuf    = data;          // start fresh
        assembling  = true;
        return;
    }
    if (data.startsWith("Temp,")) {
        curType     = "Temp";
        tempBuf     = data;
        assembling  = true;
        return;
    }

    /* 2 ── continuation of current block ----------------- */
    if (assembling) {
        if (curType == "Moist")      moistBuf += data;
        else if (curType == "Temp")  tempBuf  += data;

        /* 3 ── heuristic: a short (<15 B) fragment marks
         *        the end of this transmission ------------- */
        if (data.length() < 15) {
            assembling = false;      // finished – ready for sampleData()
            /*  let the state-machine call sampleData()
                (case 2) to save the finished buffer        */
        }
    }
}

/*-----------------------------------------------------------
 *  SAMPLE DATA  – called once per hour from the state-machine
 * --------------------------------------------------------- */
void sampleData()
{   
    if (DEBUG) SerialUSB.println("Attempting to sample data");
    /* 1 ── still receiving an I²C block? */
    if (assembling) {
        if (DEBUG) SerialUSB.println("Sample cancelled, still assembling");
        return;
    }

    /* 2 ── need BOTH buffers ready */
    if (!moistBuf.length() || !tempBuf.length()) {
        Serial.println("Sample cancelled, one buffer not ready");
        return;
    }

    /* 3 ── strip the labels (“Moist,” / “Temp,”) ------------- */
    String moistValues = moistBuf.substring(6);      // after "Moist,"
    String tempValues  = tempBuf.substring(5);       // after "Temp,"
    if (moistValues.endsWith(",")) moistValues.remove(moistValues.length() - 1);
    if (tempValues.endsWith(","))  tempValues.remove(tempValues.length()  - 1);

    /* 4 ── build timestamp ---------------------------------- */
    String time = getTime();
    uint16_t yr = time.substring(0,2).toInt();
    uint8_t  mon = time.substring(3,5).toInt();
    uint8_t  day = time.substring(6,8).toInt();
    uint8_t  hr = time.substring(9,11).toInt();
    uint8_t  min = time.substring(12,14).toInt();
    uint8_t  sec = time.substring(15,17).toInt();

    char dateStr[11], timeStr[9];
    snprintf(dateStr, sizeof(dateStr), "%02u-%02u-%02u", yr, mon, day);
    snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u", hr, min, sec);

    /* 5 ── compose CSV row ---------------------------------- */
    String row  = String(dateStr) + "\t" + timeStr + "\t";
    row += String(location.latitude , 6) + ",";
    row += String(location.longitude, 6) + ",";
    row += String(location.altitude , 1) + "\t";
    row += tempValues + "\t" + moistValues + "\tNo IR";   // placeholder for IR_Temp

    /* 6 ── ensure SD present -------------------------------- */
    if (!sdInit()) {
        SerialUSB.println("Failed to initialize SD card");
        return;                          // silent if no card
    }

    /* 7 ── open / create daily file ------------------------- */
    char fname[24];
    snprintf(fname, sizeof(fname), "D%02u%02u%02u.CSV", yr, mon, day);
    bool newFile = !SD.exists(fname);

    File f = SD.open(fname, FILE_WRITE);
    if (!f) {
        SerialUSB.println("Failed to open file " + (String)fname);
        SerialUSB.println("New file: " + newFile);
        return;
    }

    /* 9 ── append the data row ------------------------------ */
    row.replace("\n", "");
    if (DEBUG) SerialUSB.println("Writing row to SD: " + row);
    f.println(row);
    f.close();

    /*10 ── clear for next hour ------------------------------ */
    moistBuf  = "";
    tempBuf   = "";
    curType   = "";
}
