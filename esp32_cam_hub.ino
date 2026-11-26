#include "esp_camera.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "mbedtls/base64.h"

// 1. ตั้งค่า Wi-Fi ของคุณ (ต้องเป็นคลื่น 2.4GHz)
const char* ssid = "ชื่อ_WIFI_ของคุณ";
const char* password = "รหัสผ่าน_WIFI_ของคุณ";

// 2. ตั้งค่า MQTT และชื่อช่องสัญญาณ (ต้องตรงกับหน้าเว็บ)
const char* mqtt_server = "broker.emqx.io";
const char* topic = "universal-p2p-app/my-secret-room-101";

// ตั้งค่าขาพินสำหรับบอร์ด AI-Thinker ESP32-CAM
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

WiFiClient espClient;
PubSubClient client(espClient);
bool isStreaming = false;
unsigned long lastStreamTime = 0;

void setupCamera() {
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // ใช้ความละเอียด QVGA (320x240) เพื่อให้ส่งผ่าน MQTT ได้เร็วและไม่เกินขนาด Buffer
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 15; // 10-63 (เลขยิ่งน้อยยิ่งชัด แต่ไฟล์ใหญ่)
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
}

void captureAndSend() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("ถ่ายภาพไม่สำเร็จ");
    return;
  }

  // คำนวณขนาด Base64
  size_t out_len = 0;
  mbedtls_base64_encode(NULL, 0, &out_len, fb->buf, fb->len);
  
  unsigned char * b64_buf = (unsigned char *)malloc(out_len + 1);
  if (b64_buf != NULL) {
    if (mbedtls_base64_encode(b64_buf, out_len + 1, &out_len, fb->buf, fb->len) == 0) {
      b64_buf[out_len] = '\0';
      String payload = "{"type":"IOT_CAM","image":"" + String((char*)b64_buf) + ""}";
      client.publish(topic, payload.c_str());
    }
    free(b64_buf);
  }
  esp_camera_fb_return(fb);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (message.indexOf("SNAP") > 0) {
    captureAndSend();
  } else if (message.indexOf("STREAM_ON") > 0) {
    isStreaming = true;
    client.publish(topic, "{"type":"IOT_DATA","payload":"ESP32-CAM: เริ่มส่งภาพสดแล้ว!"}");
  } else if (message.indexOf("STREAM_OFF") > 0) {
    isStreaming = false;
    client.publish(topic, "{"type":"IOT_DATA","payload":"ESP32-CAM: หยุดส่งภาพสดแล้ว"}");
  } else if (message.indexOf("FLASH_ON") > 0) {
    digitalWrite(FLASH_GPIO_NUM, HIGH);
  } else if (message.indexOf("FLASH_OFF") > 0) {
    digitalWrite(FLASH_GPIO_NUM, LOW);
  }
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP32CAM-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      client.subscribe(topic);
      client.publish(topic, "{"type":"IOT_DATA","payload":"กล้อง ESP32-CAM ออนไลน์พร้อมใช้งาน!"}");
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  setupCamera();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("
WiFi Connected!");

  client.setServer(mqtt_server, 1883);
  client.setBufferSize(32768); // ขยาย Buffer เป็น 32KB เพื่อรองรับไฟล์ภาพ JPEG
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // ส่งภาพต่อเนื่องทุกๆ 500 มิลลิวินาที (2 เฟรมต่อวินาที) เมื่อเปิดโหมดภาพสด
  if (isStreaming && (millis() - lastStreamTime > 500)) {
    lastStreamTime = millis();
    captureAndSend();
  }
}
