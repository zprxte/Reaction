#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#define SW_RED 32
#define SW_YELLOW 14
#define SW_GREEN 27
#define SW_MODE 26
#define SW_SOUND_MASTER 33 // สวิตช์สำหรับเปิด-ปิดเสียงหลัก

const char* ssid = "IceiPhone";      // ใส่ชื่อ Wi-Fi ของคุณ
const char* password = "Ice22547";   // ใส่รหัสผ่าน Wi-Fi
String serverName = "https://reaction-8ygff7lwv-ice661290s-projects.vercel.app/"; 

// ตั้งค่าจอ LCD I2C Address 0x27 ขนาด 16 ตัวอักษร 2 บรรทัด
LiquidCrystal_I2C lcd(0x27, 16, 2);

// 🔴 ใส่ MAC Address ของตัวรับ
uint8_t receiverAddress[] = {0x8C, 0x4F, 0x00, 0xAB, 0x6C, 0xCC};

typedef struct msg_to_rx {
  bool red_on;
  bool yellow_on;
  bool green_on;
  bool sound_mode;
  bool sound_enabled;
} msg_to_rx;

typedef struct msg_to_tx {
  bool override_red;
  bool override_yellow;
  bool override_green;
  bool override_sound;
} msg_to_tx;

msg_to_rx myData;
msg_to_tx incomingData;
esp_now_peer_info_t peerInfo;

// ตัวแปรเก็บสถานะการล็อค (ถ้า true คือโดนเหยียบให้ดับ และจะล็อคค้างไว้)
bool lock_red = false;
bool lock_yellow = false;
bool lock_green = false;
bool lock_sound = false;

// 🟢 ตัวแปรสำหรับการจับเวลากดปุ่ม (I2C Display)
unsigned long pressStartTime = 0;
unsigned long pressDuration = 0;
bool isPressing = false;
bool hasSentData = false;

// 📡 ฟังก์ชันเมื่อได้รับข้อมูล Override จากตัวรับ
void OnDataRecv(const esp_now_recv_info_t * esp_now_info, const uint8_t *incoming, int len) {
  memcpy(&incomingData, incoming, sizeof(incomingData));
  
  Serial.println("\n[RX] 📥 ได้รับข้อมูล Override จากตัวรับ:");
  if (incomingData.override_red) { lock_red = true; Serial.println(" ⚠️ RED ถูกล็อค (ต้องปิดสวิตช์เพื่อปลด)"); }
  if (incomingData.override_yellow) { lock_yellow = true; Serial.println(" ⚠️ YELLOW ถูกล็อค (ต้องปิดสวิตช์เพื่อปลด)"); }
  if (incomingData.override_green) { lock_green = true; Serial.println(" ⚠️ GREEN ถูกล็อค (ต้องปิดสวิตช์เพื่อปลด)"); }
  if (incomingData.override_sound) { lock_sound = true; Serial.println(" ⚠️ SOUND ถูกล็อค (ต้องปิดสวิตช์เพื่อปลด)"); }
}

// 🌐 ฟังก์ชันสำหรับส่งข้อมูลขึ้นเว็บ (HTTPS)
void sendTimeToWeb(int switchID, float timeSeconds) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client; 
    client.setInsecure(); // ไม่ต้องตรวจสอบ SSL
    
    HTTPClient http;
    String serverPath = serverName + "?switch=" + String(switchID) + "&time=" + String(timeSeconds, 3);
    
    Serial.print("🌐 กำลังส่งข้อมูลขึ้นเว็บ: ");
    Serial.println(serverPath);

    http.begin(client, serverPath.c_str()); 
    int httpResponseCode = http.GET(); 
    
    if (httpResponseCode > 0) {
      Serial.print("✅ ส่งสำเร็จ! HTTP Code: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("❌ ส่งไม่สำเร็จ Error: ");
      Serial.println(httpResponseCode);
    }
    http.end(); 
  } else {
    Serial.println("❌ Wi-Fi หลุดการเชื่อมต่อ");
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System Ready...");
  lcd.clear();

  pinMode(SW_RED, INPUT_PULLUP);
  pinMode(SW_YELLOW, INPUT_PULLUP);
  pinMode(SW_GREEN, INPUT_PULLUP);
  pinMode(SW_MODE, INPUT_PULLUP);
  pinMode(SW_SOUND_MASTER, INPUT_PULLUP);

  // 1. 🌐 เชื่อมต่อ Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("\nConnecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // 2. ดึงค่า Channel ของเร้าเตอร์
  int32_t wifiChannel = WiFi.channel();
  Serial.printf("WiFi Channel: %d\n", wifiChannel);

  // 3. เริ่มต้น ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  // 4. บันทึกข้อมูล Peer
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = wifiChannel;  
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❌ Failed to add peer");
    return;
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Standby...");
}

void loop() {
  // 1. อ่านสถานะสวิตช์แบบ Real-time
  bool current_red = !digitalRead(SW_RED);
  bool current_yellow = !digitalRead(SW_YELLOW);
  bool current_green = !digitalRead(SW_GREEN);
  bool current_sound_switch = !digitalRead(SW_MODE);
  bool master_sound_on = !digitalRead(SW_SOUND_MASTER);

  // --- 🔍 พิมพ์ค่าดิบ ---
  Serial.print("Raw -> R:"); Serial.print(current_red);
  Serial.print(" Y:"); Serial.print(current_yellow);
  Serial.print(" G:"); Serial.print(current_green);
  Serial.print(" Mode:"); Serial.print(current_sound_switch);
  Serial.print(" Master:"); Serial.print(master_sound_on);
  Serial.print(" || ");

  // ควบคุมสถานะเสียง
  myData.sound_mode = current_sound_switch;
  myData.sound_enabled = master_sound_on && !lock_sound;

  // 3. จัดลำดับความสำคัญของหน้าจอและสวิตช์
  int activeSwitch = 0;
  bool isCurrentlyLocked = false;

  if (current_sound_switch) {
    // 🔊 --- อยู่ใน "โหมดเสียง" --- (เมินปุ่มไฟทั้งหมด)
    if (master_sound_on) {
      activeSwitch = 4;
      isCurrentlyLocked = lock_sound;
    }
  } else {
    // 💡 --- อยู่ใน "โหมดไฟ" --- (เมินสวิตช์เสียงทั้งหมด)
    if (current_red) {
      activeSwitch = 1;
      isCurrentlyLocked = lock_red;
    } else if (current_yellow) {
      activeSwitch = 2;
      isCurrentlyLocked = lock_yellow;
    } else if (current_green) {
      activeSwitch = 3; 
      isCurrentlyLocked = lock_green;
    }
  }

  // --- ⏳ ระบบจับเวลาบนจอ LCD ---
  if (activeSwitch > 0) {
    if (!isPressing) {
      isPressing = true;
      hasSentData = false; // 👈 1. สำคัญ: รีเซ็ตสถานะเพื่อให้พร้อมส่งข้อมูลรอบใหม่
      pressStartTime = millis(); 
      lcd.clear();
    }
    if (!isCurrentlyLocked) {
      pressDuration = millis() - pressStartTime; 
    }
    lcd.setCursor(0, 0);
    if(activeSwitch == 1) lcd.print("RED Active    ");
    else if(activeSwitch == 2) lcd.print("YELLOW Active ");
    else if(activeSwitch == 3) lcd.print("GREEN Active  ");
    else if(activeSwitch == 4) lcd.print("SOUND Active  ");
    
    lcd.setCursor(0, 1);
    if (isCurrentlyLocked) {
      lcd.print("STOP! "); // โดนเหยียบแล้ว ล็อคเวลาไว้
      
      // 👇 2. จุดสำคัญ: เรียกใช้ฟังก์ชันส่งข้อมูลขึ้นเว็บตรงนี้!
      if (!hasSentData) {
        // ส่ง (หมายเลขปุ่ม, เวลาที่ใช้ไป)
        sendTimeToWeb(activeSwitch, pressDuration / 1000.0);
        hasSentData = true; // ตั้งค่าเป็น true เพื่อไม่ให้ส่งซ้ำจนกว่าจะกดปุ่มรอบใหม่
      }

    } else {
      lcd.print("Time: "); 
    }
    lcd.print(pressDuration / 1000.0, 3); 
    lcd.print(" s    ");

  } else {
    if (isPressing) {
      isPressing = false;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Standby...");
      lcd.setCursor(0, 1);
      lcd.print("Last: ");
      lcd.print(pressDuration / 1000.0, 3); 
      lcd.print(" s");
    }
  }

  // ====================================================================
  // 4. 🔓 ระบบปลดล็อค: จะยอมปลดล็อคก็ต่อเมื่อสวิตช์ตัวนั้นถูก "ปิด" เท่านั้น
  // ====================================================================
  if (!current_red && lock_red) { lock_red = false; Serial.print("[UNLOCK] 🔓 ปลดล็อค Red "); }
  if (!current_yellow && lock_yellow) { lock_yellow = false; Serial.print("[UNLOCK] 🔓 ปลดล็อค Yellow "); }
  if (!current_green && lock_green) { lock_green = false; Serial.print("[UNLOCK] 🔓 ปลดล็อค Green "); }
  if (!master_sound_on && lock_sound) { lock_sound = false; Serial.print("[UNLOCK] 🔓 ปลดล็อค Sound "); }

  myData.red_on = false;
  myData.yellow_on = false;
  myData.green_on = false;

  // 5. เตรียมแพ็กเก็ตข้อมูลสี
  switch (activeSwitch) {
    case 1: 
      if (!lock_red) { myData.red_on = true; Serial.print(">> ส่ง 🔴RED "); }
      else { Serial.print(">> 🔴RED ถูกล็อคอยู่ (รอการปิด-เปิดใหม่)! "); }
      break;
    case 2: 
      if (!lock_yellow) { myData.yellow_on = true; Serial.print(">> ส่ง 🟡YELLOW "); }
      else { Serial.print(">> 🟡YELLOW ถูกล็อคอยู่ (รอการปิด-เปิดใหม่)! "); }
      break;
    case 3: 
      if (!lock_green) { myData.green_on = true; Serial.print(">> ส่ง 🟢GREEN "); }
      else { Serial.print(">> 🟢GREEN ถูกล็อคอยู่ (รอการปิด-เปิดใหม่)! "); }
      break;
  }

  Serial.println();

  // ส่งข้อมูลผ่าน ESP-NOW
  esp_now_send(receiverAddress, (uint8_t *) &myData, sizeof(myData));
  Serial.printf("Mode: %s | Master: %s | Active: %d\n", 
                myData.sound_mode ? "SOUND" : "LIGHT", 
                myData.sound_enabled ? "ON" : "OFF", 
                activeSwitch);
  
  delay(100);
}
