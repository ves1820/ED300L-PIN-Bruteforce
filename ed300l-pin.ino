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
#define BOOT_BTN_PIN    9     // BOOT-Taste am ESP32-C3 (GPIO 9, Active LOW)
#define SML_BAUDRATE    9600  // Baudrate für SML D0-Schnittstelle

// Timing-Parameter in Millisekunden (optimiert für ED300L)
#define PULSE_DURATION    80  // Dauer eines Lichtimpulses
#define PULSE_PAUSE       80  // Pause zwischen zwei Impulsen derselben Ziffer
#define DIGIT_PAUSE     3300  // Warten bis Zähler zur nächsten Ziffer springt
#define RESET_WAIT      2500  // Warten auf SML-Auswertung nach PIN-Eingabe

// Y-Positionen für Display-Elemente (2px tiefer ausgerichtet)
#define PIN_TEXT_Y        22  // Baseline der 22px hohen Ziffern
#define UNDERLINE_Y1      25  // Erster Pixelsteg des Unterstrichs
#define UNDERLINE_Y2      26  // Zweiter Pixelsteg des Unterstrichs

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

void updateDisplayPinOnly(const char* pinStr) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_logisoso22_tn);
  
  int textWidth = u8g2.getStrWidth(pinStr);
  int x = (72 - textWidth) / 2;
  if (x < 0) x = 0;
  
  // Y-Baseline auf 22 gesetzt
  u8g2.drawStr(x, PIN_TEXT_Y, pinStr);
  u8g2.sendBuffer();
}

// ============================================================================
// STARTWERT-EINGABE VIA BOOT-TASTE (MIT LAUFSCHRIFT IN GROSSER SCHRIFT)
// ============================================================================

int inputStartPin() {
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  
  const char* marqueeText = "ED300L - BOOT TASTE FUER START PIN   ";
  
  u8g2.setFont(u8g2_font_fub20_tf); // Große Fettschrift
  int textWidth = u8g2.getStrWidth(marqueeText);

  logMsg("INIT", "Starte Laufschrift (10s). Warte auf BOOT-Taste fuer Start-PIN...");

  bool enterSetup = false;
  int xPos = 72; // Start außerhalb des rechten Bildschirmrands

  // Laufschrift für ca. 10 Sekunden durchlaufen lassen (2 vollwertige Runden)
  unsigned long startMarquee = millis();
  while (millis() - startMarquee < 15000) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_fub20_tf);
    u8g2.drawStr(xPos, PIN_TEXT_Y + 2, marqueeText);
    u8g2.sendBuffer();

    xPos -= 2; // Pixel-Schrittweite der Laufschrift
    if (xPos < -textWidth) {
      xPos = 72; // Bei Ende neu von rechts anfangen
    }

    // BOOT-Taste abfragen (Active LOW)
    if (digitalRead(BOOT_BTN_PIN) == LOW) {
      delay(40); // Entprellen
      if (digitalRead(BOOT_BTN_PIN) == LOW) {
        enterSetup = true;
        break;
      }
    }
    delay(25); // Geschwindigkeit der Laufschrift
  }

  // Keine Taste gedrückt -> Standardmäßig bei 0000 starten
  if (!enterSetup) {
    logMsg("INIT", "Keine Taste gedrueckt -> Starte automatisch bei 0000.");
    return 0;
  }

  // Warten bis Taste losgelassen wird
  while (digitalRead(BOOT_BTN_PIN) == LOW) delay(10);
  delay(200);

  logMsg("SETUP", "Start-PIN Modus aktiv. Kurz = +1, 2.5s Warten = Naechste Stelle.");

  int pinDigits[4] = {0, 0, 0, 0};

  for (int digitIdx = 0; digitIdx < 4; digitIdx++) {
    unsigned long lastAction = millis();
    unsigned long lastBlinkToggle = millis();
    bool digitVisible = true;
    bool digitActive = true;

    while (digitActive) {
      // Blink-Timer (alle 300ms umschalten)
      if (millis() - lastBlinkToggle > 300) {
        lastBlinkToggle = millis();
        digitVisible = !digitVisible;
      }

      // Display-String mit blinkender Ziffer aufbauen
      char displayBuf[5];
      for (int k = 0; k < 4; k++) {
        if (k == digitIdx && !digitVisible) {
          displayBuf[k] = ' '; // In der Aus-Phase der aktiven Ziffer ein Leerzeichen anzeigen
        } else {
          displayBuf[k] = '0' + pinDigits[k];
        }
      }
      displayBuf[4] = '\0';
      
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_logisoso22_tn);
      
      // Zentrierung anhand des vollständigen Strings berechnen
      char fullBuf[5];
      snprintf(fullBuf, sizeof(fullBuf), "%d%d%d%d", pinDigits[0], pinDigits[1], pinDigits[2], pinDigits[3]);
      int fullWidth = u8g2.getStrWidth(fullBuf);
      int startX = (72 - fullWidth) / 2;
      if (startX < 0) startX = 0;

      // Ziffern bei Y = 22 zeichnen
      u8g2.drawStr(startX, PIN_TEXT_Y, displayBuf);
      
      // Unterstrich unter der aktiven Ziffer (auf Y = 25 und 26 verschoben)
      int underlineX = startX + (digitIdx * 14);
      u8g2.drawHLine(underlineX, UNDERLINE_Y1, 12);
      u8g2.drawHLine(underlineX, UNDERLINE_Y2, 12);
      
      u8g2.sendBuffer();

      // Tastendruck abfragen (Active LOW)
      if (digitalRead(BOOT_BTN_PIN) == LOW) {
        delay(40); // Entprellen
        if (digitalRead(BOOT_BTN_PIN) == LOW) {
          pinDigits[digitIdx] = (pinDigits[digitIdx] + 1) % 10;
          lastAction = millis();
          
          // Bei Druck Ziffer sofort wieder sichtbar schalten & Blink-Timer zurücksetzen
          digitVisible = true;
          lastBlinkToggle = millis();
          
          logMsg("SETUP", "Stelle %d = %d", digitIdx + 1, pinDigits[digitIdx]);
          
          // Warten bis Taste wieder losgelassen wird
          while (digitalRead(BOOT_BTN_PIN) == LOW) delay(10);
          delay(100);
        }
      }

      // Timeout nach 2,5 Sekunden Inaktivität -> Nächste Stelle übernehmen
      if (millis() - lastAction > 2500) {
        digitActive = false;
        logMsg("SETUP", "Stelle %d uebernommen: %d", digitIdx + 1, pinDigits[digitIdx]);
      }
      delay(20);
    }
  }

  int calculatedStartPin = pinDigits[0] * 1000 + pinDigits[1] * 100 + pinDigits[2] * 10 + pinDigits[3];
  logMsg("SETUP", "Start-PIN erfolgreich eingestellt auf: %04d", calculatedStartPin);
  
  char finalBuf[5];
  snprintf(finalBuf, sizeof(finalBuf), "%04d", calculatedStartPin);
  updateDisplayPinOnly(finalBuf);
  delay(1200);

  return calculatedStartPin;
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

  // 1. Zähler aufwecken / PIN-Maske aufrufen (3x Impuls)
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
  Serial.println("   EMH ED300L PIN BRUTE-FORCE TOOL v1.6        ");
  Serial.println("   Board: ESP32-C3 | Display: OLED 0.42\" (72x40)");
  Serial.println("===============================================");

  Wire.begin(5, 6);

  u8g2.begin();
  logMsg("INIT", "OLED Display initialisiert.");

  pinMode(IR_TX_PIN, OUTPUT);
  digitalWrite(IR_TX_PIN, LOW);

  SMLSerial.begin(SML_BAUDRATE, SERIAL_8N1, IR_RX_PIN, -1);

  // Start-PIN-Eingabe über BOOT-Taste aufrufen (10s Laufschrift)
  currentPin = inputStartPin();

  while (SMLSerial.available()) SMLSerial.read();

  logMsg("INIT", "Setup beendet. Starte Schleife ab PIN %04d...", currentPin);
  delay(500);
}

void loop() {
  // Wenn PIN gefunden: Display im 500-ms-Takt komplett blinken lassen
  if (pinFound) {
    static unsigned long lastBlink = 0;
    static bool displayState = true;
    
    if (millis() - lastBlink > 500) {
      lastBlink = millis();
      displayState = !displayState;
      
      if (displayState) {
        char pinStr[5];
        snprintf(pinStr, sizeof(pinStr), "%04d", currentPin);
        updateDisplayPinOnly(pinStr);
      } else {
        u8g2.clearBuffer();
        u8g2.sendBuffer();
      }
    }
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
    
    pinFound = true;
    return;
  }

  currentPin++;
}
