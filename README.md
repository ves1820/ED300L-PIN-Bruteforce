# ESP32-C3-OLED ED300L PIN Brute-Force / Recovery

Automatisierter PIN-Recovery-Tool für den Drehstromzähler **EMH ED300L** mittels ESP32-C3, optischem IR-Lesekopf, IR-Sendediode und einem 0,42" OLED-Display (72×40).

Dieses Projekt probiert systematisch die Kombinationen `0000` bis `9999` aus, indem es Lichtimpulse an die optische Taste an der Gehäusefront des Stromzählers sendet. Sobald die richtige PIN eingegeben wurde, schaltet der Zähler den erweiterte SML-Datenstrom frei (OBIS `1-0:16.7.0` für die aktuelle Wirkleistung in Watt). Das Skript erkennt dies automatisch im Datenstrom, stoppt die Schleife und zeigt den gefundenen Code dauerhaft und groß auf dem Display an.
inspiriert von https://github.com/philipparndt/smartmeter-pin-brute-force und https://pastebin.com/Rcxix0aX und zu arduino gewandelt

---

## Features

* **Große Display-Anzeige:** PIN-Anzeige auf dem 0,42" OLED (SSD1306, 72×40 Pixel).
* Start PIN Eingabe durch drücken der BOOT Taste möglich
* **Automatische Erfolgs-Erkennung:** Liest das SML-Telegramm über die optische Schnittstelle (RX) ein und verifiziert die Freischaltung der Wirkleistung.
* **Detailliertes Serielles Logging:** Zeitstempel-basiertes Log über den USB-CDC Port bei 115200 Baud.
* **Auf ESP32-C3-OLED optimiert:** Spezifische Hardware-UART- und I2C-Konfiguration zur Vermeidung von Pin-Konflikten.

---

## Hardware & Pinbelegung

| Komponente | ESP32-C3 Pin | Funktion / Anmerkung |
| :--- | :--- | :--- |
| **OLED SDA** | `GPIO 5` | I2C Data (0,42" SSD1306 Display) |
| **OLED SCL** | `GPIO 6` | I2C Clock |
| **IR-TX (Sender)** | `GPIO 4` | Steuerung der IR-Sendediode (Lichtimpulse) |
| **IR-RX (Empfänger)** | `GPIO 3` | D0/TTL SML-Lesekopf (RX) |
| **VCC** | `3.3V` | Stromversorgung für Display & Lesekopf |
| **GND** | `GND` | Gemeinsame Masse |

---

## Software & Einstellungen (Arduino IDE)

### 1. Bibliotheken
* **U8g2** von *Oliver Kraus* (über den Bibliotheksverwalter der Arduino IDE installierbar).

### 2. Board-Einstellungen
* **Board:** `ESP32C3 Dev Module`
* **USB CDC On Boot:** `Enabled` *(Wichtig für die serielle Ausgabe via USB-Kabel)*
* **JTAG Adapter:** `Integrated USB JTAG`
* **Upload Speed:** `921600` (oder `115200`)

---

## Zeitaufwand

Der ED300L benötigt nach jeder Zifferneingabe eine Pause von ca. 3,3 Sekunden, damit der Cursor zur nächsten Stelle weiterspringt.

* **Dauer pro Versuch:** ca. 17–20 Sekunden
* **Durchschnittliche Dauer (5.000 Versuche):** ~24–27 Stunden
* **Maximaldauer (10.000 Versuche):** ~48–55 Stunden (durchgehender Betrieb)
* die meisten PINs liegen oberhalb von 5000

---

## Sicherheitshinweis & Haftungsausschluss

* Das Arbeiten an Zählern darf nur an eigenen oder dekommissionierten Geräten (z. B. gebrauchten Zählern von eBay für Zwischenzählung/Unterverteilung) erfolgen.
* Achte bei Messungen stets auf Berührungsschutz an den spannungsführenden 230/400V-Klemmen.

