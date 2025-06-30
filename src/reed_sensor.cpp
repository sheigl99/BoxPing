#include "reed_sensor.h"
#include <Arduino.h>

static int reedPin; // Lokale Variable, speichert den verwendeten Pin

// Initialisiert den Sensor
void initReedSensor(int pin) {
  reedPin = pin;
  pinMode(reedPin, INPUT_PULLUP); // Reed-Sensor mit Pull-Up aktivieren
}

// Gibt zurück, ob die Klappe geöffnet ist
bool isMailboxOpened() {
  return digitalRead(reedPin) == HIGH;
}
