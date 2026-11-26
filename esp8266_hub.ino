/*
 * ระบบ Universal P2P & IoT Hub - ฝั่งบอร์ด ESP8266
 * คุณสมบัติ:
 * 1. ตั้งค่า Wi-Fi และ "ชื่อช่องสัญญาณ (Room)" ผ่านหน้าจอมือถือได้เองโดยไม่ต้องแก้โค้ด (WiFiManager)
 * 2. บันทึกชื่อช่องสัญญาณลงหน่วยความจำถาวร (LittleFS) ไม่หายแม้ไฟดับ
 * 3. มีระบบสลับเซิร์ฟเวอร์สำรองอัตโนมัติ (EMQX <-> HiveMQ) ตรงกับหน้าเว็บ
 * 
 * ไลบรารีที่ต้องติดตั้งใน Arduino IDE:
 * - PubSubClient (โดย Nick O'Leary)
 * - WiFiManager (โดย tzapu)
 * - ArduinoJson (โดย Benoit Blanchon)
 */

#include <FS.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ค่าเริ่มต้นของชื่อห้อง (ผู้ใช้สามารถเปลี่ยนผ่านมือถือได้ตอนต่อ WiFi ตั้งค่า)
char room_name[40] = "my-iot-room-8899";
bool shouldSaveConfig = false;

const char* mqtt_servers[] = {"broker.emqx.io", "broker.hivemq.com"};
int serverIndex = 0;
int failCount = 0;

String fullTopic = "";
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void loadConfig() {
  if (LittleFS.begin()) {
    if (LittleFS.exists("/config.json")) {
      File configFile = LittleFS.open("/config.json", "r");
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
  File configFile = LittleFS.open("/config.json", "w");
  if (configFile) {
    serializeJson(doc, configFile);
    configFile.close();
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("ได้รับข้อความ: " + message);

  if (message.indexOf(""ON"") > 0 || message == "ON") {
    digitalWrite(LED_BUILTIN, LOW); // เปิดไฟ LED บนบอร์ด ESP8266
    client.publish(fullTopic.c_str(), "{"type":"IOT_DATA","payload":"ESP8266: เปิดไฟให้แล้วครับ!"}");
  } else if (message.indexOf(""OFF"") > 0 || message == "OFF") {
    digitalWrite(LED_BUILTIN, HIGH); // ปิดไฟ LED บนบอร์ด ESP8266
    client.publish(fullTopic.c_str(), "{"type":"IOT_DATA","payload":"ESP8266: ปิดไฟเรียบร้อยครับ!"}");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  loadConfig();

  WiFiManagerParameter custom_room("room", "ตั้งชื่อช่องสัญญาณ (Room Name)", room_name, 40);
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_room);

  // หากต่อ WiFi เดิมไม่ได้ จะสร้าง Hotspot ชื่อ "ESP8266-Hub-Setup" ให้มือถือเข้าไปตั้งค่า
  if (!wifiManager.autoConnect("ESP8266-Hub-Setup")) {
    delay(3000);
    ESP.restart();
  }

  strcpy(room_name, custom_room.getValue());
  if (shouldSaveConfig) {
    saveConfig();
  }

  fullTopic = "universal-p2p-app/" + String(room_name);
  Serial.println("เชื่อมต่อห้อง: " + fullTopic);

  client.setServer(mqtt_servers[serverIndex], 1883);
  client.setCallback(callback);
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP8266-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      failCount = 0;
      client.subscribe(fullTopic.c_str());
      String welcome = "{"type":"IOT_DATA","payload":"บอร์ด ESP8266 ออนไลน์ในห้อง [" + String(room_name) + "] แล้ว!"}";
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

  unsigned long now = millis();
  if (now - lastMsg > 20000) {
    lastMsg = now;
    String statusMsg = "{"type":"IOT_DATA","payload":"ESP8266 ปกติ (WiFi: " + String(WiFi.RSSI()) + " dBm)"}";
    client.publish(fullTopic.c_str(), statusMsg.c_str());
  }
}
