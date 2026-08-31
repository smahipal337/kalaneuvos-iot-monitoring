#include "secrets.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"

// Forward declarations for external Blynk helper tab functions
void initBlynk();
void runBlynkHandle();
void updateBlynkTelemetry(float temp, float humi, float accX, float accY, float accZ);

// Hardware Pin Configuration Matrix
#define TFT_CS         5
#define TFT_DC         2
#define TFT_RST       14
#define DHTPIN         4
#define DHTTYPE     DHT22

// High-Contrast UI Color Configuration
#define NAVY_BG         0x012B
#define ACCENT_CYAN     0x07FF
#define ACCENT_GREEN    0x07E0
#define ACCENT_YELLOW   0xFFE0

// AWS MQTT Pub/Sub Topics
#define AWS_IOT_PUBLISH_TOPIC   "esp32/telemetry"
#define AWS_IOT_SUBSCRIBE_TOPIC "esp32/commands"

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
DHT dht(DHTPIN, DHTTYPE);
Adafruit_MPU6050 mpu;

WiFiClientSecure net = WiFiClientSecure();
PubSubClient client(net);

unsigned long lastCloudUpload = 0;
const unsigned long uploadInterval = 5000;

void messageHandler(char* topic, byte* payload, unsigned int length) {
  Serial.print("Incoming AWS command topic: ");
  Serial.println(topic);
}

void connectAWS() {
  tft.fillRect(48, 32, 74, 16, NAVY_BG);
  tft.setCursor(50, 36); tft.setTextSize(1); tft.setTextColor(ST7735_ORANGE, NAVY_BG);
  tft.print("CONNECTING");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to Wi-Fi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Connected!");

  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);
  client.setServer(AWS_IOT_ENDPOINT, 8883);
  client.setCallback(messageHandler);

  Serial.println("Establishing Secure TLS Tunnel to AWS IoT Core...");
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection to AWS... ");
    if (client.connect("esp32-kalaneuvos-01")) {
      Serial.println("CONNECTED SUCCESSFULLY!");
    } else {
      Serial.print("FAILED, rc = ");
      Serial.print(client.state());
      Serial.println(" | Retrying in 2 seconds...");
      delay(2000);
    }
  }

  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
  Serial.println("AWS IoT Core Connected Securely!");
}

void publishMessage(float temp, float humi, float accX, float accY, float accZ) {
  JsonDocument doc;
  doc["temperature"] = temp;
  doc["humidity"]    = humi;
  doc["vibration_x"] = accX;
  doc["vibration_y"] = accY;
  doc["vibration_z"] = accZ;

  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
  Serial.println("Telemetry batch securely pushed to AWS!");
}

void setup() {
  Serial.begin(115200);

  dht.begin();
  if (!mpu.begin()) { Serial.println("Failed to find MPU6050 chip"); }
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, LOW); delay(100);
  digitalWrite(TFT_RST, HIGH); delay(200);
  tft.initR(INITR_BLACKTAB);
  tft.fillScreen(ST7735_BLACK);

  tft.fillRect(2, 2, 124, 20, 0x9000);
  tft.setCursor(10, 8); tft.setTextColor(ST7735_WHITE); tft.setTextSize(1);
  tft.println("AWS SMART NODE");

  tft.fillRect(2, 26, 124, 54, NAVY_BG);
  tft.fillRect(2, 84, 124, 74, NAVY_BG);

  connectAWS();
  initBlynk();
}

void loop() {
  runBlynkHandle();

  if (!client.connected()) {
    connectAWS();
  }
  client.loop();

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  if (!isnan(h) && !isnan(t)) {
    tft.fillRect(48, 32, 74, 16, NAVY_BG);
    tft.fillRect(48, 56, 74, 16, NAVY_BG);

    tft.setTextSize(1);
    tft.setTextColor(ST7735_WHITE, NAVY_BG);
    tft.setCursor(10, 36); tft.print("TEMP:");
    tft.setCursor(10, 60); tft.print("HUMI:");

    tft.setTextSize(2); tft.setCursor(48, 32); tft.setTextColor(ACCENT_GREEN, NAVY_BG); tft.print(t, 1);
    tft.setTextSize(1); tft.print(" C");

    tft.setTextSize(2);
    tft.setCursor(48, 56); tft.setTextColor(ACCENT_CYAN, NAVY_BG); tft.print(h, 0);
    tft.setTextSize(1); tft.print(" %");
  }

  tft.fillRect(48, 92, 74, 40, NAVY_BG);
  tft.setTextSize(1); tft.setTextColor(ST7735_WHITE, NAVY_BG);
  tft.setCursor(10, 92);  tft.print("X:");
  tft.setCursor(10, 106); tft.print("Y:");
  tft.setCursor(10, 120); tft.print("Z:");

  tft.setTextColor(ACCENT_YELLOW, NAVY_BG);
  tft.setCursor(24, 92);  tft.print(a.acceleration.x, 2); tft.print(" m/s2");
  tft.setCursor(24, 106);
  tft.print(a.acceleration.y, 2); tft.print(" m/s2");
  tft.setCursor(24, 120); tft.print(a.acceleration.z, 2); tft.print(" m/s2");

  float absoluteMotion = abs(a.acceleration.x) + abs(a.acceleration.y);
  tft.fillRect(6, 138, 116, 16, NAVY_BG);
  tft.setCursor(10, 142);
  if (absoluteMotion > 5.0) {
    tft.setTextColor(ST7735_RED, NAVY_BG); tft.print("ENGINE: VIBRATING");
  } else {
    tft.setTextColor(ACCENT_GREEN, NAVY_BG); tft.print("ENGINE: OK");
  }

  updateBlynkTelemetry(t, h, a.acceleration.x, a.acceleration.y, a.acceleration.z);

  if (millis() - lastCloudUpload > uploadInterval) {
    lastCloudUpload = millis();
    if (!isnan(h) && !isnan(t)) {
      publishMessage(t, h, a.acceleration.x, a.acceleration.y, a.acceleration.z);
    }
  }

  delay(200);
}
