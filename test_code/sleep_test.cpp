#include <Arduino.h>
#include <ArduinoLowPower.h>

// ── Pin assignments ─────────────────────────────────────
const uint8_t UNO_START_PIN  = 5;   // Maduino → Uno  (start signal)
const uint8_t UNO_WAKE_PIN   = 2;   // Uno     → Maduino (interrupt)

// ── Flag set in the wake ISR (optional) ────────────────
volatile bool woke = false;
void wakeISR() { woke = true; }

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);

  // handshake lines
  pinMode(UNO_START_PIN, OUTPUT);
  digitalWrite(UNO_START_PIN, LOW);          // idle LOW
  pinMode(UNO_WAKE_PIN, INPUT_PULLUP);       // idle HIGH

  // attach external-interrupt wake (RISING edge on UNO_WAKE_PIN)
  LowPower.attachInterruptWakeup(UNO_WAKE_PIN, wakeISR, RISING);

  SerialUSB.begin(115200);
  while (!SerialUSB);
  SerialUSB.println("Maduino ready (doc-style sleep).");
}

void loop()
{
  // 1. tell the Uno to start its delay
  SerialUSB.println("→ Signalling Uno, then sleeping …");
  digitalWrite(UNO_START_PIN, HIGH);
  delay(50);
  SerialUSB.flush();

  // 2. detach USB and enter infinite standby sleep
  USBDevice.detach();
  LowPower.sleep();                     // wakes only on external IRQ

  // 3. we’re awake again
  USBDevice.attach();
  delay(200);                           // let host re-enumerate
  SerialUSB.println("← Woke from external interrupt!");

  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // visual proof
  digitalWrite(UNO_START_PIN, LOW);      // clear handshake
  delay(500);                            // small gap before next cycle
}
