#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ============================================================================
// DISPLAY & HARDWARE CONFIGURATION
// ============================================================================

// 0,42" OLED Display (72x40) an ESP32-C3 (SCL = GPIO 6, SDA = GPIO 5)
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);

// GPIO-Pins auf dem ESP32-C3
#define IR_TX_PIN       4     // GPIO für IR-Sendediode (Lichtimpulse)
#define IR_RX_PIN       3     // GPIO für RX vom SML-Lesekopf
#define SML_BAUDRATE    9600  // Baudrate für SML D0-Schnittstelle

// Timing-Parameter in Millisekunden (optimiert für ED300L mit 10 Pulsen für 0)
#define PULSE_DURATION    80  // Dauer eines Lichtimpulses
#define PULSE_PAUSE       80  // Pause zwischen zwei Impulsen derselben Ziffer
#define DIGIT_PAUSE     3300  // Warten bis Zähler zur nächsten Ziffer springt
#define RESET_WAIT      2500  // Warten auf SML-Auswertung nach PIN-Eingabe

// Start- und End-PIN für den Brute-Force-Lauf
int currentPin = 0;
const int maxPin = 9999;
bool pinFound = false;

// HardwareSerial-Instanz für den SML-Empfang (Serial1 des ESP32-C3)
HardwareSerial SMLSerial(1);

// ============================================================================
// SERIELLES LOGGING MIT ZEITSTEMPEL
// ============================================================================

void logMsg(const char* level, const char* format, ...) {
  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  unsigned long ms = millis();
  unsigned long sec = ms / 1000;
  unsigned long min = sec / 60;
  unsigned long hr = min / 60;

  Serial.printf("[%02lu:%02lu:%02lu.%03lu] [%s] %s\n", 
                hr % 24, min % 60, sec % 60, ms % 1000, level, buffer);
}

// ============================================================================
// DISPLAY FUNCTIONS (72x40 Pixel)
// ============================================================================

void showSplashScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_9x15_tf);
  u8g2.drawStr(18, 16, "ED300L");
  u8g2.sendBuffer();
}

void updateDisplayPinOnly(const char* pinStr) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_logisoso22_tn);
  
  int textWidth = u8g2.getStrWidth(pinStr);
  int x = (72 - textWidth) / 2;
  if (x < 0) x = 0;
  
  u8g2.drawStr(x, 26, pinStr);
  u8g2.sendBuffer();
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void sendPulses(int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(IR_TX_PIN, HIGH);
    delay(PULSE_DURATION);
    digitalWrite(IR_TX_PIN, LOW);
    delay(PULSE_PAUSE);
  }
}

void enterPin(int pin) {
  char pinStr[5];
  snprintf(pinStr, sizeof(pinStr), "%04d", pin);
  
  logMsg("TEST", "Starte PIN-Eingabe: %s", pinStr);
  updateDisplayPinOnly(pinStr);

  // 1. Zähler aufwecken / PIN-Maske aufrufen (auf 3 Impulsen erhöht)
  logMsg("DEBUG", "3x Aufweck-Impulse senden...");
  sendPulses(3);
  delay(1000);

  // 2. Jede der 4 Ziffern einzeln eingeben
  for (int i = 0; i < 4; i++) {
    int digit = pinStr[i] - '0';
    int pulseCount = (digit == 0) ? 10 : digit;
    
    logMsg("DEBUG", "Stelle %d: Ziffer %d -> %d Pulse", i + 1, digit, pulseCount);
    sendPulses(pulseCount);
    
    // 3,3 Sekunden warten, damit der Cursor zur nächsten Stelle springt
    delay(DIGIT_PAUSE);
  }
  logMsg("DEBUG", "PIN-Eingabe abgeschlossen. Warte auf SML-Datenstrom...");
}

bool checkSMLSuccess() {
  const uint8_t obisWirkleistung[] = {0x01, 0x00, 0x10, 0x07, 0x00, 0xFF};
  size_t matchIndex = 0;
  uint32_t bytesRead = 0;
  
  unsigned long startWait = millis();
  
  while (millis() - startWait < RESET_WAIT) {
    while (SMLSerial.available() > 0) {
      uint8_t b = SMLSerial.read();
      bytesRead++;
      
      if (b == obisWirkleistung[matchIndex]) {
        matchIndex++;
        if (matchIndex == sizeof(obisWirkleistung)) {
          logMsg("SUCCESS", "OBIS-Kennzahl 16.7.0 (Wirkleistung) im SML-Stream erkannt!");
          return true;
        }
      } else {
        matchIndex = (b == obisWirkleistung[0]) ? 1 : 0;
      }
    }
  }
  
  logMsg("RESULT", "PIN falsch. Empfangene Bytes: %lu", bytesRead);
  return false;
}

// ============================================================================
// MAIN SETUP & LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n===============================================");
  Serial.println("   EMH ED300L PIN BRUTE-FORCE TOOL v1.1        ");
  Serial.println("   Board: ESP32-C3 | Display: OLED 0.42\" (72x40)");
  Serial.println("===============================================");

  Wire.begin(5, 6);

  u8g2.begin();
  logMsg("INIT", "OLED Display initialisiert.");
  
  showSplashScreen();
  delay(1500);

  pinMode(IR_TX_PIN, OUTPUT);
  digitalWrite(IR_TX_PIN, LOW);

  SMLSerial.begin(SML_BAUDRATE, SERIAL_8N1, IR_RX_PIN, -1);

  while (SMLSerial.available()) SMLSerial.read();

  updateDisplayPinOnly("0000");
  logMsg("INIT", "Setup beendet. Starte Schleife...");
  
  delay(1000);
}

void loop() {
  if (pinFound) {
    delay(1000);
    return;
  }

  if (currentPin > maxPin) {
    logMsg("ERROR", "Alle PINs (0000-9999) ohne Erfolg getestet!");
    updateDisplayPinOnly("----");
    pinFound = true;
    return;
  }

  while (SMLSerial.available()) SMLSerial.read();

  enterPin(currentPin);

  if (checkSMLSuccess()) {
    char pinStr[5];
    snprintf(pinStr, sizeof(pinStr), "%04d", currentPin);
    
    Serial.println("\n***********************************************");
    logMsg("SUCCESS", ">>> PIN GEFUNDEN: %s <<<", pinStr);
    Serial.println("***********************************************\n");
    
    updateDisplayPinOnly(pinStr);
    pinFound = true;
    return;
  }

  currentPin++;
}
