#include "secrets.h"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

unsigned long lastBlynkUpload = 0;
const unsigned long blynkInterval = 5000;

void initBlynk() {
  Serial.println("Initializing Blynk...");
  Blynk.config(BLYNK_AUTH_TOKEN);
  Serial.println("Blynk Core Initialized.");
}

void runBlynkHandle() {
  Blynk.run();
}

void updateBlynkTelemetry(float temp, float humi, float accX, float accY, float accZ) {
  if (millis() - lastBlynkUpload > blynkInterval) {
    lastBlynkUpload = millis();

    if (!isnan(humi) && !isnan(temp)) {
      Blynk.virtualWrite(V1, temp);
      Blynk.virtualWrite(V2, humi);
      Blynk.virtualWrite(V3, accX);
      Blynk.virtualWrite(V4, accY);
      Blynk.virtualWrite(V5, accZ);
      Serial.println("Blynk Stream Sync Complete!");
    }
  }
}
