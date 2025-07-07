#include <Arduino.h>
#include "wiring_private.h"
#include <SoftwareSerial.h>

// Create a new UART instance using SERCOM3 on PADs 2 (RX) and 3 (TX)
Uart LORA (&sercom3, 2, 3, SERCOM_RX_PAD_2, UART_TX_PAD_2);
void SERCOM3_Handler() { LORA.IrqHandler(); }
void beginLora() {
    pinPeripheral(2, PIO_SERCOM);
    pinPeripheral(3, PIO_SERCOM);
    LORA.begin(9600);
}


void setup() {
    SerialUSB.begin(115200);
    while (!SerialUSB);                 // wait for PC
    beginLora();
    SerialUSB.println(F("\n=== RYLR993 bridge ready ==="));
    SerialUSB.println(F("Type AT then <Enter>.  "
                        "Set terminal to send “Both NL & CR”."));
}

void loop() {
    /* PC → LoRa */
    while (SerialUSB.available())
        LORA.write(SerialUSB.read());

    /* LoRa → PC (raw) */
    while (LORA.available())
        SerialUSB.write(LORA.read());
}



/* ---------- tiny util identical to LTE sendAT() style ----- */
String loraAT(const String& cmd, uint32_t to = 2000, bool dbg = true)
{
    String resp;
    Serial2.println(cmd);                 // AT … \r\n

    unsigned long t0 = millis();
    while (millis() - t0 < to) {
        while (Serial2.available())
            resp += char(Serial2.read());
    }
    if (dbg && resp.length()) SerialUSB.print(resp);
    return resp;
}

/* ---------- 1. Wake / turn ON (exit sleep mode) ------------ */
bool loraOn()
{
    /* The first byte on UART already wakes the chip from MODE 1
       and the module answers +READY\r\n                       */
    Serial2.write('\r');                      // dummy wake pulse
    delay(10);

    // Tell it explicitly to stay in MODE 0 (active)
    String r = loraAT("AT+MODE=0");
    if (r.indexOf("OK") >= 0 || r.indexOf("+READY") >= 0) {
        SerialUSB.println(F("[LoRa] ACTIVE"));
        return true;
    }
    SerialUSB.println(F("[LoRa] Wake-up FAILED"));
    return false;
}

/* ---------- 2. Put module to SLEEP (µA) -------------------- */
void loraOff()
{
    loraAT("AT+MODE=1");                     // replies OK then sleeps
    SerialUSB.println(F("[LoRa] SLEEP"));
}

/* ---------- 3. Send a WAKE-UP frame to another node --------
 * addr  : 16-bit node address (your network uses AT+ADDRESS)
 * window: how long peer should stay awake (seconds) – encode any
 *         literal you want; here we just send the ASCII string
 */
bool loraSendWake(uint16_t addr, uint8_t window = 10)
{
    char payload[8];
    snprintf(payload, sizeof(payload), "WU%02u", window); // "WU10" etc.

    String cmd = "AT+SEND=" + String(addr) + "," +
                 String(strlen(payload)) + "," + payload;

    String r = loraAT(cmd, 5000);
    bool ok = r.indexOf("OK") >= 0;
    char msg[64];
    snprintf(msg, sizeof(msg), "[LoRa] WAKE(%u) %s", addr, ok ? "sent" : "fail");
    SerialUSB.println(msg);
    return ok;
}

/* ---------- 4. Non-blocking listener for +RCV= (...) ------- */
struct LoRaFrame {
    uint16_t from;
    int      rssi;
    int      snr;
    String   payload;    // raw ASCII payload
};
*/
bool loraPoll(LoRaFrame &f)
{
    static String buf;                       // accumulate UART bytes
    while (Serial2.available())
        buf += char(Serial2.read());

    int p = buf.indexOf("+RCV=");
    if (p < 0) return false;                 // nothing yet

    int ln = buf.indexOf('\n', p);
    if (ln < 0) return false;                // line not complete

    String line = buf.substring(p + 5, ln);  // strip "+RCV="
    buf.remove(0, ln + 1);                   // keep rest for next poll

    // Format: addr,len,data,rssi,snr
    int addr, len, rssi, snr;
    char data[256] = {0};
    if (sscanf(line.c_str(), "%d,%d,%255[^,],%d,%d",
               &addr, &len, data, &rssi, &snr) != 5)
        return false;                        // parse error

    f.from    = addr;
    f.rssi    = rssi;
    f.snr     = snr;
    f.payload = String(data).substring(0, len);
    return true;
}

/* --- PROCESS CHUNK --- */
void processChunk(const String& s)
{
    if (s.startsWith("Moist,")) {          // new Moist block
        curType    = "Moist";
        moistBuf   = s;
        assembling = true;
        return;
    }
    if (s.startsWith("Temp,")) {           // new Temp block
        curType    = "Temp";
        tempBuf    = s;
        assembling = true;
        return;
    }

    if (!assembling) return;               // stray fragment?

    if (curType == "Moist") moistBuf += s; // extend current buffer
    else                    tempBuf  += s;

    /* Heuristic: last packet is normally short (<15 chars)      */
    if (s.length() < 15) assembling = false;
}
