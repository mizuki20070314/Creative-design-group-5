/*
統合スケッチ: 腕振り検知 (ADXL345) と指定BLEデバイスのRSSIを WiFi 経由で TCP 送信します。
- BLEスキャンは FreeRTOSタスク でバックグラウンド実行します。
- 腕振りを検知したら TCP送信します。
*/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

// -------------------- ユーザー設定 --------------------
const char* WIFI_SSID = "group5";
const char* WIFI_PASS = "12345678";

const char* SERVER_IP = "192.168.137.1";
const uint16_t SERVER_PORT = 12345;

const char* targetAddress = "58:8c:81:9d:b8:de"; // 小文字で統一
// -----------------------------------------------------

Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

Adafruit_NeoPixel pixels(10, D10, NEO_GRB + NEO_KHZ800);

// 加速度関連
bool isWaving = false;
bool flag = true;

// BLE スキャン結果
volatile bool foundTarget = false;
volatile int lastRSSI = -127;

// スキャン設定
const uint32_t SCAN_DURATION = 4294967295;
const uint32_t SCAN_INTERVAL = 100;
BLEScan* scan;

// BLE スキャンタスクの関数プロトタイプ
void bleScanTask(void *pvParameters);


// --------------------------------------------------
// BLE スキャン結果コールバック
// --------------------------------------------------
class ScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {

    std::string advAddr = dev.getAddress().toString();

    if (advAddr == targetAddress) {
      foundTarget = true;
      lastRSSI = dev.getRSSI();
      // シリアル出力はCPU時間を消費するのでタスク内での頻繁な使用は注意が必要
      Serial.printf("[BLE] Found %s, RSSI=%d\n", advAddr.c_str(), lastRSSI);
    }
  }
};

// --------------------------------------------------
// BLE スキャンタスク (FreeRTOSタスク)
// --------------------------------------------------
void bleScanTask(void *pvParameters) {
    for (;;) {
        // スキャン前に結果をリセット
        foundTarget = false;

        // 1. スキャンを開始 (非ブロッキング)
        Serial.println("[BLE Task] Start scan...");
        // 4秒間スキャン。第2引数に false を指定しても、タスク内で遅延を入れることが重要。
        scan->start(SCAN_DURATION / 1000, false); 

        // 2. スキャン実行中、タスクを待機状態にする
        // vTaskDelay()を使用することで、メインの loop() にCPU時間を明け渡します。
        vTaskDelay(pdMS_TO_TICKS(SCAN_DURATION)); 

        // 3. スキャン停止 (自動タイムアウトするが、明示的に止める)
        scan->stop();
        
        Serial.printf("[BLE Task] Scan stopped. Last RSSI: %d, Found: %s\n", lastRSSI, foundTarget ? "Yes" : "No");

        // 4. スキャンとスキャンの間に休止時間を設ける
        vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL));
    }
}


// --------------------------------------------------
// 腕振り判定（簡易 Z軸 方式）
// --------------------------------------------------
bool CheckMotion() {
  sensors_event_t event;
  accel.getEvent(&event);

  isWaving = false;
  float z = fabs(event.acceleration.z);

  // Serial.print("Z:"); 
  // Serial.println(z);

  if (z > 14) {
    isWaving = true;
  }
  return isWaving;
}

// --------------------------------------------------
// TCP送信
// --------------------------------------------------
void sendTCP(bool swing, int rssi, bool found) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, retrying...");
    WiFi.reconnect();
    return;
  }

  WiFiClient client;
  Serial.printf("Connecting to %s:%u...\n", SERVER_IP, SERVER_PORT);
  if (!client.connect(SERVER_IP, SERVER_PORT)) {
    Serial.println("TCP connect failed");
    return;
  }

  String json = String("{\"swing\":") + (swing?1:0)
              + ",\"rssi\":" + rssi
              + ",\"found\":" + (found?1:0)
              + "}";

  client.println(json);
  Serial.print("Sent: ");
  Serial.println(json);

  unsigned long start = millis();
  String response = "";

  while (millis() - start < 500) { // 500ms 以内に返答が来る
    if (client.available()) {
      response = client.readStringUntil('\n');
      break;
    }
  }

  int led = response.toInt();
  Serial.printf("Server returned: %d\n", led);

  for(int i = 0;i < 10;i++) {
    if (led == 1) {
      pixels.setPixelColor(i, pixels.Color(0, 255, 0));
    } else if (led == 2) {
      pixels.setPixelColor(i, pixels.Color(255, 255, 0));
    } else if (led == 3){
      pixels.setPixelColor(i, pixels.Color(255, 0, 0));
    }
  }

  if (led == 4) {
    pixels.clear();
  } else if (led == 5) {
    flag = !flag;   // ← ここで1回だけ反転
  } else if (led != 0 && led != 1 && led != 2){
    Serial.printf("Failed to parse JSON\n");
  }

  pixels.show();

  client.stop();
}

// --------------------------------------------------
// setup()
// --------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  pixels.begin();

  // ADXL345 初期化
  if (!accel.begin()) {
    Serial.println("ADXL345 not detected!");
    while (1);
  }
  accel.setRange(ADXL345_RANGE_16_G);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Connecting WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  // BLE 初期化
  BLEDevice::init("");
  scan = BLEDevice::getScan();
  // 第2引数を true に設定することを推奨
  scan->setAdvertisedDeviceCallbacks(new ScanCallback(), true); 
  scan->setActiveScan(true);
  scan->setInterval(90);
  scan->setWindow(45);

  // ********** FreeRTOS タスクの作成 **********
  xTaskCreatePinnedToCore(
      bleScanTask,       // タスクを実行する関数
      "BLEScanTask",     // タスク名
      4096,              // スタックサイズ (必要に応じて調整)
      NULL,              // パラメータ
      1,                 // 優先度
      NULL,              // タスクハンドル
      0                  // ESP32-C3はシングルコア
  );
  // **********************************************
}

// --------------------------------------------------
// loop()
// --------------------------------------------------
void loop() {
  // BLEスキャンはバックグラウンドタスクが処理
  
  // 腕振り判定
  CheckMotion();

  if (isWaving || flag == true) {
    // BLEタスクがバックグラウンドで更新した最新のRSSI値を送信
    sendTCP(isWaving, lastRSSI, foundTarget);
    Serial.println(flag);
    // 連続送信を防ぐためのディレイ
    delay(250);
  }

  // loop()の実行間隔を調整
  delay(10);
}
