/*
 * ระบบ Universal P2P & IoT Hub - ฝั่งบอร์ด ESP32-CAM (AI-Thinker)
 * คุณสมบัติ:
 * 1. ตั้งค่า Wi-Fi และ "ชื่อช่องสัญญาณ (Room)" ผ่านมือถือได้เอง (WiFiManager + SPIFFS)
 * 2. ถ่ายภาพนิ่ง (SNAP) และส่งภาพต่อเนื่อง (STREAM_ON / STREAM_OFF) เข้าหน้าเว็บได้ทันที
 * 3. สั่งเปิด-ปิดไฟแฟลชหน้ากล้อง (FLASH_ON / FLASH_OFF) จากหน้าเว็บได้
 * 
 * ไลบรารีที่ต้องติดตั้งใน Arduino IDE:
 * - PubSubClient (โดย Nick O'Leary)
 * - WiFiManager (โดย tzapu)
 * - ArduinoJson (โดย Benoit Blanchon)
 * 
 * การตั้งค่าบอร์ดใน Arduino IDE:
 * - Board: "AI Thinker ESP32-CAM"
 */

#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "base64.h"

// ขาพินกล้องรุ่น AI-Thinker ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define FLASH_GPIO_NUM     4

char room_name[40] = "my-cam-room-8899";
bool shouldSaveConfig = false;
bool isStreaming = false;

const char* mqtt_servers[] = {"broker.emqx.io", "broker.hivemq.com"};
int serverIndex = 0;
int failCount = 0;

String fullTopic = "";
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastFrameTime = 0;

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void loadConfig() {
  if (SPIFFS.begin(true)) {
    if (SPIFFS.exists("/config.json")) {
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, buf.get()) == DeserializationError::Ok) {
          strcpy(room_name, doc["room_name"]);
        }
        configFile.close();
      }
    }
  }
}

void saveConfig() {
  DynamicJsonDocument doc(512);
  doc["room_name"] = room_name;
  File configFile = SPIFFS.open("/config.json", "w");
  if (configFile) {
    serializeJson(doc, configFile);
    configFile.close();
  }
}

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA; // 320x240 เพื่อความรวดเร็วผ่าน MQTT
  config.jpeg_quality = 14;
  config.fb_count = 1;

  esp_camera_init(&config);
}

void captureAndSend() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return;

  String base64Img = "data:image/jpeg;base64," + base64::encode(fb->buf, fb->len);
  esp_camera_fb_return(fb);

  if (base64Img.length() < 32000) {
    client.publish(fullTopic.c_str(), base64Img.c_str());
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (message.indexOf("SNAP") >= 0) {
    captureAndSend();
  } else if (message.indexOf("STREAM_ON") >= 0) {
    isStreaming = true;
    client.publish(fullTopic.c_str(), "{"type":"IOT_DATA","payload":"ESP32-CAM: เริ่มส่งภาพสดแล้ว"}");
  } else if (message.indexOf("STREAM_OFF") >= 0) {
    isStreaming = false;
    client.publish(fullTopic.c_str(), "{"type":"IOT_DATA","payload":"ESP32-CAM: หยุดส่งภาพสดแล้ว"}");
  } else if (message.indexOf("FLASH_ON") >= 0) {
    digitalWrite(FLASH_GPIO_NUM, HIGH);
  } else if (message.indexOf("FLASH_OFF") >= 0) {
    digitalWrite(FLASH_GPIO_NUM, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  initCamera();
  loadConfig();

  WiFiManagerParameter custom_room("room", "ตั้งชื่อช่องสัญญาณกล้อง (Room Name)", room_name, 40);
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_room);

  // หากต่อ WiFi เดิมไม่ได้ จะสร้าง Hotspot ชื่อ "ESP32-CAM-Setup" ให้มือถือเข้าไปตั้งค่า
  if (!wifiManager.autoConnect("ESP32-CAM-Setup")) {
    delay(3000);
    ESP.restart();
  }

  strcpy(room_name, custom_room.getValue());
  if (shouldSaveConfig) {
    saveConfig();
  }

  fullTopic = "universal-p2p-app/" + String(room_name);
  client.setBufferSize(32768); // ขยาย Buffer รองรับไฟล์ภาพ JPEG
  client.setServer(mqtt_servers[serverIndex], 1883);
  client.setCallback(callback);
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP32CAM-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      failCount = 0;
      client.subscribe(fullTopic.c_str());
      String welcome = "{"type":"IOT_DATA","payload":"กล้อง ESP32-CAM ออนไลน์ในห้อง [" + String(room_name) + "] แล้ว!"}";
      client.publish(fullTopic.c_str(), welcome.c_str());
    } else {
      failCount++;
      if (failCount >= 3) {
        failCount = 0;
        serverIndex = (serverIndex + 1) % 2;
        client.setServer(mqtt_servers[serverIndex], 1883);
      }
      delay(2000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (isStreaming && (millis() - lastFrameTime > 500)) {
    lastFrameTime = millis();
    captureAndSend();
  }
}
