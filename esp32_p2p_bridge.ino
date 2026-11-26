/*
 * ESP32 P2P Hardware Bridge Firmware
 * Works with: p2p-core-hub-v3-hardware.html
 * Supports: Bluetooth BLE (Nordic UART) & Wi-Fi WebSocket
 */

#include <WiFi.h>
#include <WebSocketsServer.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ==========================================
// 1. ตั้งค่าโหมดการเชื่อมต่อและ Wi-Fi
// ==========================================
#define USE_BLE true        // ตั้งเป็น true เพื่อเปิด Bluetooth BLE
#define USE_WIFI false      // ตั้งเป็น true เพื่อเปิด Wi-Fi WebSocket

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

#define LED_PIN 2           // ขาไฟ LED บนบอร์ด ESP32 (ส่วนใหญ่คือ GPIO 2)

// ==========================================
// 2. ตั้งค่า Bluetooth BLE (Nordic UART UUID)
// ==========================================
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;

// ==========================================
// 3. ตั้งค่า Wi-Fi WebSocket
// ==========================================
WebSocketsServer webSocket = WebSocketsServer(81);

// ==========================================
// ฟังก์ชันส่งข้อมูลกลับไปยังหน้าเว็บมือถือ
// ==========================================
void sendToWeb(String msg) {
  msg += "\n";
  if (USE_BLE && deviceConnected) {
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
  }
  if (USE_WIFI) {
    webSocket.broadcastTXT(msg);
  }
}

// ==========================================
// ตัวถอดรหัสคำสั่งที่ส่งมาจากหน้าเว็บ
// ==========================================
void processCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.println("RX: " + cmd);

  // 1. คำสั่งเปิด-ปิดไฟ LED ทดสอบ
  if (cmd == "CMD:LED_ON") {
    digitalWrite(LED_PIN, HIGH);
    sendToWeb("STATUS:LED_IS_ON");
  } 
  else if (cmd == "CMD:LED_OFF") {
    digitalWrite(LED_PIN, LOW);
    sendToWeb("STATUS:LED_IS_OFF");
  }
  // 2. คำสั่งจากจอยสติ๊ก: J:lx,ly,rx,ry,btns
  else if (cmd.startsWith("J:")) {
    String data = cmd.substring(2);
    int c1 = data.indexOf(',');
    int c2 = data.indexOf(',', c1 + 1);
    int c3 = data.indexOf(',', c2 + 1);
    int c4 = data.indexOf(',', c3 + 1);

    if (c1 > 0 && c2 > 0 && c3 > 0 && c4 > 0) {
      float lx = data.substring(0, c1).toFloat();
      float ly = data.substring(c1 + 1, c2).toFloat();
      float rx = data.substring(c2 + 1, c3).toFloat();
      float ry = data.substring(c3 + 1, c4).toFloat();
      String btns = data.substring(c4 + 1);

      // --- ใส่โค้ดควบคุมมอเตอร์หรือเซอร์โวตรงนี้ ---
      // ตัวอย่าง: ไฟ LED ติดเมื่อดันแกนซ้ายขึ้นหรือลง
      if (abs(ly) > 0.2 || abs(lx) > 0.2) {
        digitalWrite(LED_PIN, HIGH);
      } else {
        digitalWrite(LED_PIN, LOW);
      }

      // ตัวอย่าง: เช็กการกดปุ่มหมายเลข 0 (ปุ่ม A / X)
      if (btns.indexOf("0") != -1) {
        Serial.println("Button 0 Pressed!");
      }
    }
  }
}

// Callbacks สำหรับ BLE
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("BLE Client Connected");
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("BLE Client Disconnected");
    pServer->getAdvertising()->start(); // เปิดโฆษณาสัญญาณใหม่ทันทีที่หลุด
  }
};

class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue().c_str();
    if (rxValue.length() > 0) {
      processCommand(rxValue);
    }
  }
};

// Event สำหรับ Wi-Fi WebSocket
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    String msg = String((char*)payload);
    processCommand(msg);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // เริ่มต้นระบบ Bluetooth BLE
  if (USE_BLE) {
    BLEDevice::init("ESP32-P2P-Bridge");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());

    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    pRxCharacteristic->setCallbacks(new MyCallbacks());

    pService->start();
    pServer->getAdvertising()->start();
    Serial.println("BLE Ready: ESP32-P2P-Bridge");
  }

  // เริ่มต้นระบบ Wi-Fi WebSocket
  if (USE_WIFI) {
    WiFi.begin(ssid, password);
    Serial.print("Connecting WiFi");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
  }
}

void loop() {
  if (USE_WIFI) {
    webSocket.loop();
  }
  
  // รองรับการทดสอบพิมพ์คำสั่งผ่านสาย USB Serial Monitor ด้วย
  if (Serial.available()) {
    String serialCmd = Serial.readStringUntil('\n');
    processCommand(serialCmd);
  }
}
