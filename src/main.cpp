#include <Arduino.h>                      // Grundfunktionen für Arduino
#include <WiFi.h>                         // WLAN-Verbindung
#include <WiFiClientSecure.h>            // HTTPS-Kommunikation (Telegram API)
#include <WiFiUdp.h>                      // UDP-Protokoll für Zeitabfrage (NTP)
#include <NTPClient.h>                    // NTP-Zeitclient
#include <UniversalTelegramBot.h>        // Telegram-Bot-Bibliothek
#include <ArduinoJson.h>                 // JSON-Verarbeitung (für Telegram)
#include <LiquidCrystal.h>               // Steuerung eines LCD-Displays

// Einbinden eigener Header-Dateien
#include "config.h"                       // WLAN- und Telegram-Zugangsdaten
#include "reed_sensor.h"                  // Funktionen für Reed-Kontaktsensor

// ---------------- Hardware Pins ----------------
const int LED_ROT = 2;                    // Rote LED – Post im Briefkasten
const int LED_GRUEN = 16;                 // Grüne LED – Entnahme geöffnet
const int BUTTON_PIN = 4;                 // Taster zum Zurücksetzen
const int SENSOR_EINWURF = 15;            // Einwurfklappe – Sensor
const int SENSOR_ENTNAHME = 32;           // Entnahmeklappe – Sensor

// ---------------- LCD Pins ----------------
const int LCD_RS = 13;
const int LCD_E = 12;
const int LCD_D4 = 14;
const int LCD_D5 = 27;
const int LCD_D6 = 26;
const int LCD_D7 = 25;

// Initialisierung des LCDs mit den definierten Pins
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// ---------------- Telegram + Zeit ----------------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);  // UTC+1, Update alle 60 Sek.
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// ---------------- Statusvariablen ----------------
bool postImBriefkasten = false;
bool einwurfGeoeffnet = false;
bool nachrichtGesendet = false;
bool entnahmeVorherGeschlossen = true;

unsigned long einwurfOffenSeit = 0;
int oeffnungsZaehler = 0;
int letzterTag = -1;

// ---------------- Hilfsfunktionen ----------------

// LCD-Text anzeigen
void zeigeLCD(String zeile1, String zeile2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(zeile1);
  lcd.setCursor(0, 1);
  lcd.print(zeile2);
}

// LEDs entsprechend dem Status setzen
void setLedRot(bool an) {
  digitalWrite(LED_ROT, an ? HIGH : LOW);
}

void setLedGruen(bool an) {
  digitalWrite(LED_GRUEN, an ? HIGH : LOW);
}

// Telegram-Nachricht mit Uhrzeit und Zähler senden
void sendeTelegramMitZeit(String text) {
  timeClient.update();  // Zeit aktualisieren
  String zeit = timeClient.getFormattedTime();
  int aktuellerTag = timeClient.getDay();

  // Tageswechsel: Zähler zurücksetzen
  if (aktuellerTag != letzterTag) {
    oeffnungsZaehler = 0;
    letzterTag = aktuellerTag;
  }

  oeffnungsZaehler++;

  // Nachricht formatieren
  String nachricht = text + "\n";
  nachricht += "🕒 Zeit: " + zeit + "\n";
  nachricht += "📈 Öffnungen heute: " + String(oeffnungsZaehler);

  // Nachricht senden
  if (bot.sendMessage(CHAT_ID, nachricht, "Markdown")) {
    Serial.println("✅ Telegram gesendet.");
  } else {
    Serial.println("❌ Fehler beim Senden.");
  }
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Pins konfigurieren
  pinMode(LED_ROT, OUTPUT);
  pinMode(LED_GRUEN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(SENSOR_ENTNAHME, INPUT_PULLUP);

  setLedRot(false);
  setLedGruen(false); // Anfangszustand: beide aus

  // Sensor initialisieren
  initReedSensor(SENSOR_EINWURF);

  // LCD initialisieren
  lcd.begin(16, 2);
  zeigeLCD("Briefkasten...", "Verbinde WLAN...");

  // WLAN verbinden
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WLAN verbunden.");
  client.setInsecure();

  // NTP starten
  timeClient.begin();
  timeClient.setTimeOffset(7200);  // Sommerzeit (UTC+2)
  timeClient.update();
  letzterTag = timeClient.getDay();

  zeigeLCD("System bereit", "Warte auf Post...");
  Serial.println("📬 System bereit.");
}

// ---------------- Loop ----------------
void loop() {
  timeClient.update();

  // Sensorstatus auslesen
  bool entnahmeGeschlossen = digitalRead(SENSOR_ENTNAHME) == LOW;
  bool einwurfAktuellOffen = isMailboxOpened();

  // LEDs setzen
  // Rote LED an, wenn Post drin oder Einwurf offen
  setLedRot(postImBriefkasten || einwurfAktuellOffen);
  // Grüne LED an, sobald Entnahmeklappe geöffnet wurde (bleibt an)
  static bool entnahmeWurdeGeoeffnet = false;
  if (!entnahmeGeschlossen) {
    entnahmeWurdeGeoeffnet = true;
  }
  setLedGruen(entnahmeWurdeGeoeffnet);

  // Button gedrückt → manuelle Postentnahme
  if (digitalRead(BUTTON_PIN) == LOW && postImBriefkasten) {
    postImBriefkasten = false;
    entnahmeWurdeGeoeffnet = false; // Reset der grünen LED
    setLedRot(false);
    setLedGruen(false);
    zeigeLCD("Briefkasten leer", "                ");
    bot.sendMessage(CHAT_ID, "📤 *Post manuell entnommen (Button)*", "Markdown");
    Serial.println("📤 Post entnommen (manuell)");
    delay(300);
  }

  // Automatische Entnahme
  if (!entnahmeGeschlossen && entnahmeVorherGeschlossen && postImBriefkasten) {
    postImBriefkasten = false;
    zeigeLCD("Entnommen", "Briefkasten leer");
    bot.sendMessage(CHAT_ID, "📤 *Post wurde entnommen!*", "Markdown");
    Serial.println("📤 Post wurde entnommen!");
  }
  entnahmeVorherGeschlossen = entnahmeGeschlossen;

  // Einwurfklappe wird geöffnet (nur dann Post melden!)
  if (einwurfAktuellOffen && !einwurfGeoeffnet) {
    einwurfGeoeffnet = true;
    einwurfOffenSeit = millis();
    nachrichtGesendet = false;

    if (!postImBriefkasten) {
      postImBriefkasten = true;
      entnahmeWurdeGeoeffnet = false;  // 🟢 Grüne LED zurücksetzen
      sendeTelegramMitZeit("📬 *Neue Post eingeworfen!*");
      Serial.println("🔴 Neue Post erkannt");
      zeigeLCD("Neue Post!", timeClient.getFormattedTime());
    }
  }

  // Einwurfklappe wieder zu
  if (!einwurfAktuellOffen && einwurfGeoeffnet) {
    einwurfGeoeffnet = false;
    einwurfOffenSeit = 0;
    Serial.println("🟢 Einwurfklappe geschlossen");
  }

  // Klappe seit >5 Minuten offen → Warnung
  if (einwurfGeoeffnet && !nachrichtGesendet && millis() - einwurfOffenSeit > 5 * 60 * 1000) {
    bot.sendMessage(CHAT_ID, "⚠️ Die Einwurfklappe ist seit über 5 Minuten offen! Möglicherweise große Post.", "");
    Serial.println("⚠️ Warnung: Klappe >5min offen");
    nachrichtGesendet = true;
  }

  delay(100);  // kurze Pause
}
