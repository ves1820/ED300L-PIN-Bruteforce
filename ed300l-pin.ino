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
#define IR_RX_PIN       3     // GPIO für RX vom SML-Lesekopf (GPIO 3 vermeidet Serial-Konflikte)
#define SML_BAUDRATE    9600  // Baudrate für SML D0-Schnittstelle

// Timing-Parameter in Millisekunden (angepasst an EMH ED300L)
#define PULSE_DURATION   120  // Dauer eines Lichtimpulses
#define PULSE_PAUSE      120  // Pause zwischen zwei Impulsen derselben Ziffer
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
// DISPLAY FUNCTIONS (72x40 Pixel - NUR PIN ANZEIGE)
// ============================================================================

// Kurzer Startbildschirm beim Booten
void showSplashScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_9x15_tf);
  u8g2.drawStr(18, 16, "ED300L");
  //u8g2.drawStr(12, 30, " PIN");
  u8g2.sendBuffer();
}

// Zeigt ausschließlich die 4-stellige PIN maximal groß & zentriert an
void updateDisplayPinOnly(const char* pinStr) {
  u8g2.clearBuffer();
  
  // Sehr große, klar lesbare Zahlenschriftart
  u8g2.setFont(u8g2_font_logisoso22_tn);
  
  // Dynamische Zentrierung auf dem 72 Pixel breiten Display
  int textWidth = u8g2.getStrWidth(pinStr);
  int x = (72 - textWidth) / 2;
  if (x < 0) x = 0;
  
  // Y-Baseline 31 zentriert die 22px hohe Schrift vertikal in 40px Gesamthöhe
  u8g2.drawStr(x, 26, pinStr);
  
  u8g2.sendBuffer();
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Sendet Lichtimpulse an die optische Taste des Zählers
void sendPulses(int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(IR_TX_PIN, HIGH);
    delay(PULSE_DURATION);
    digitalWrite(IR_TX_PIN, LOW);
    delay(PULSE_PAUSE);
  }
}

// Gibt den 4-stelligen PIN ein
void enterPin(int pin) {
  char pinStr[5];
  snprintf(pinStr, sizeof(pinStr), "%04d", pin);
  
  logMsg("TEST", "Starte PIN-Eingabe: %s", pinStr);
  updateDisplayPinOnly(pinStr);

  // 1. Zähler aufwecken / PIN-Maske aufrufen (1x kurzer Impuls)
  logMsg("DEBUG", "Aufweck-Impuls senden...");
  sendPulses(1);
  delay(1500);

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

// Prüft den SML-Datenstrom auf die Wirkleistung (OBIS 1-0:16.7.0 / Hex: 01 00 10 07 00 FF)
bool checkSMLSuccess() {
  const uint8_t obisWirkleistung[] = {0x01, 0x00, 0x10, 0x07, 0x00, 0xFF};
  size_t matchIndex = 0;
  uint32_t bytesRead = 0;
  
  unsigned long startWait = millis();
  
  // Für RESET_WAIT ms eintreffende Bytes lesen und auswerten
  while (millis() - startWait < RESET_WAIT) {
    while (SMLSerial.available() > 0) {
      uint8_t b = SMLSerial.read();
      bytesRead++;
      
      if (b == obisWirkleistung[matchIndex]) {
        matchIndex++;
        if (matchIndex == sizeof(obisWirkleistung)) {
          logMsg("SUCCESS", "OBIS-Kennzahl 16.7.0 (Wirkleistung) im SML-Stream erkannt!");
          return true; // Treffer! Erweitere SML-Daten empfangen.
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
  // USB-Serial für Debugging
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n===============================================");
  Serial.println("   EMH ED300L PIN BRUTE-FORCE TOOL v1.0        ");
  Serial.println("   Board: ESP32-C3 | Display: OLED 0.42\" (72x40)");
  Serial.println("===============================================");

  // I2C Bus mit Ihren funktionierenden Pins starten
  Wire.begin(5, 6);

  // Display initialisieren
  u8g2.begin();
  logMsg("INIT", "OLED Display (0.42\" I2C, SCL=6, SDA=5) initialisiert.");
  
  showSplashScreen();
  delay(1500);

  // Hardware-Pins konfigurieren
  pinMode(IR_TX_PIN, OUTPUT);
  digitalWrite(IR_TX_PIN, LOW);
  logMsg("INIT", "IR-TX Pin auf GPIO %d konfiguriert.", IR_TX_PIN);

  // Hardware-Serial1 für SML-Empfang vom Lesekopf
  SMLSerial.begin(SML_BAUDRATE, SERIAL_8N1, IR_RX_PIN, -1);
  logMsg("INIT", "SML Serial1 gestartet (Baudrate: %d, RX: GPIO %d).", SML_BAUDRATE, IR_RX_PIN);

  // Empfangspuffer leeren
  while (SMLSerial.available()) SMLSerial.read();

  updateDisplayPinOnly("0000");
  logMsg("INIT", "Setup beendet. Starte Brute-Force Schleife...");
  Serial.println("-----------------------------------------------\n");
  
  delay(1000);
}

void loop() {
  if (pinFound) {
    delay(1000);
    return;
  }

  if (currentPin > maxPin) {
    logMsg("ERROR", "Alle PINs (0000-9999) ohne Erfolg durchgetestet!");
    updateDisplayPinOnly("----");
    pinFound = true;
    return;
  }

  // Puffer vor Durchlauf leeren
  while (SMLSerial.available()) SMLSerial.read();

  // PIN am Zähler einblinken
  enterPin(currentPin);

  // SML-Empfang auswerten
  if (checkSMLSuccess()) {
    char pinStr[5];
    snprintf(pinStr, sizeof(pinStr), "%04d", currentPin);
    
    Serial.println("\n***********************************************");
    logMsg("SUCCESS", ">>> PIN ERFOLGREICH GEFUNDEN: %s <<<", pinStr);
    Serial.println("***********************************************\n");
    
    // Gefundene PIN dauerhaft auf dem Display anzeigen
    updateDisplayPinOnly(pinStr);
    pinFound = true;
    return;
  }

  // Nächste PIN
  currentPin++;
}
