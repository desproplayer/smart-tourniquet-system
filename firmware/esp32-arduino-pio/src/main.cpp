// Smart Tourniquet System — main.cpp
// Kelompok 19 — Desain Proyek Teknik Elektro, Komputer, Biomedik 2
//
// STATUS (Week 3): baca sensor tekanan MPX5050DP via ADS1115, tampilkan ke OLED.
// TODO Week 4+: state machine penuh (IDLE -> SELF_CHECK -> ARMED -> INFLATING -> ...),
// DSP pipeline Butterworth + peak detection, kontrol pompa/solenoid, logika LOP.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "pressure_sensor.h"

PressureSensor pressureSensor;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

void setup() {
    Serial.begin(115200);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    if (!pressureSensor.begin()) {
        Serial.println("ERROR: ADS1115 tidak terdeteksi. Cek wiring I2C & alamat.");
    }

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("ERROR: OLED tidak terdeteksi.");
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Smart Tourniquet");
    display.println("Self-check...");
    display.display();

    pinMode(BTN_START_PIN, INPUT_PULLUP);
    pinMode(BTN_RELEASE_PIN, INPUT_PULLUP);
    pinMode(PUMP_MOSFET_PIN, OUTPUT);
    pinMode(SOLENOID_LOCK_PIN, OUTPUT);
    pinMode(SOLENOID_RELEASE_PIN, OUTPUT);

    delay(500); // simulasi self-check 2 detik sesuai V.B Fase 0 di proposal (dipersingkat utk dev)
}

void loop() {
    float pressureMmHg = pressureSensor.readMmHg();

    Serial.print("Pressure: ");
    Serial.print(pressureMmHg, 1);
    Serial.println(" mmHg");

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("TEKANAN:");
    display.setTextSize(2);
    display.setCursor(0, 16);
    display.print(pressureMmHg, 0);
    display.println(" mmHg");
    display.setTextSize(1);
    display.setCursor(0, 48);

    if (pressureMmHg > HARD_PRESSURE_LIMIT_MMHG) {
        display.println("!! OVER LIMIT !!");
    } else {
        display.println("Status: OK");
    }
    display.display();

    delay(200); // ~5 Hz refresh tampilan; sampling DSP sebenarnya di 50 Hz (lihat config.h)
}
