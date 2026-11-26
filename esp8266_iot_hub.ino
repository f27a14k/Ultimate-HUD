#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// 1. ใส่ชื่อและรหัสผ่าน Wi-Fi ของคุณ (ต้องเป็นคลื่น 2.4GHz)
const char* ssid = "ชื่อ_WIFI_ของคุณ";
const char* password = "รหัสผ่าน_WIFI_ของคุณ";

// 2. ตั้งชื่อห้องให้ตรงกับหน้าเว็บของคุณ
const char* mqtt_server = "broker.emqx.io";
const char* topic = "universal-p2p-app/my-secret-room-101"; 

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT); // ไฟ LED สีฟ้าบนบอร์ด ESP8266
  digitalWrite(LED_BUILTIN, HIGH); // สั่งปิดไฟไว้ก่อน (ESP8266 ใช้ HIGH คือปิด)

  WiFi.begin(ssid, password);
  Serial.print("กำลังต่อ WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nต่อ WiFi สำเร็จ!");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

// ฟังก์ชันรอรับคำสั่งที่พิมพ์ส่งมาจากหน้าเว็บ
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("ได้รับข้อความ: " + message);

  // ถ้าหน้าเว็บพิมพ์คำว่า ON หรือ OFF เข้ามาในห้อง
  if (message.indexOf("ON") >= 0) {
    digitalWrite(LED_BUILTIN, LOW);  // เปิดไฟ LED บนบอร์ด
    client.publish(topic, "{\"type\":\"IOT_DATA\",\"payload\":\"บอร์ด ESP8266: เปิดไฟให้แล้วครับ!\"}");
  } else if (message.indexOf("OFF") >= 0) {
    digitalWrite(LED_BUILTIN, HIGH); // ปิดไฟ LED บนบอร์ด
    client.publish(topic, "{\"type\":\"IOT_DATA\",\"payload\":\"บอร์ด ESP8266: ปิดไฟเรียบร้อยครับ!\"}");
  }
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP8266-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("เข้าห้องสื่อสารสำเร็จ!");
      client.subscribe(topic);
      client.publish(topic, "{\"type\":\"IOT_DATA\",\"payload\":\"บอร์ด ESP8266 รายงานตัวเข้าห้องแล้ว!\"}");
    } else {
      delay(2000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // ส่งสถานะการทำงานขึ้นหน้าเว็บทุกๆ 15 วินาที
  unsigned long now = millis();
  if (now - lastMsg > 15000) {
    lastMsg = now;
    String jsonPayload = "{\"type\":\"IOT_DATA\",\"payload\":\"ESP8266 ออนไลน์ปกติ (สัญญาณ WiFi: " + String(WiFi.RSSI()) + " dBm)\"}";
    client.publish(topic, jsonPayload.c_str());
  }
}
