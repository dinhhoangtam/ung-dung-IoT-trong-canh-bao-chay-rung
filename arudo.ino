#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


#define DHTPIN A2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define FLAME1 3
#define GAS A0
#define SOIL A1
#define BUZZER 8

// Ngưỡng cứng tại chỗ (Dự phòng an toàn khi mất kết nối LoRa)
#define TEMP_THRESHOLD 45
#define HUM_THRESHOLD 35
#define GAS_THRESHOLD 400
#define SOIL_THRESHOLD 600

#define LORA Serial1 // Dùng cổng Serial1 phần cứng (Pin 19 RX1, Pin 18 TX1)

// Các biến phục vụ còi kêu tít tít không chặn mạch
unsigned long lastBeep = 0;
bool buzzerState = false;

// Các biến định thời không chặn mạch (Non-blocking Millis)
unsigned long lastLoraSend = 0;
const unsigned long loraInterval = 2000; // Gửi dữ liệu mỗi 2 giây

unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 1000; // Đọc cảm biến mỗi 1 giây

// Biến lưu trạng thái ép còi từ ESP8266 truyền về
bool espForceBuzzer = false; 

// Lưu trữ giá trị cảm biến
float temp = 0;
float hum = 0;
int gas = 0;
int soil = 0;
int flame1 = HIGH;
bool flameStable = false;
String statusText = "SAFE";

void setup() {
  Serial.begin(9600);
  LORA.begin(9600); 
  
  lcd.init();
  lcd.backlight();
  dht.begin();

  pinMode(FLAME1, INPUT_PULLUP); // Dùng chân kéo lên cố định để tín hiệu lửa cực kỳ ổn định
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    display.clearDisplay();
    display.display();
  }

  lcd.print("System Start...");
  delay(2000);
  lcd.clear();
  Serial.println("MEGA INITIALIZED - READY FOR OPERATION.");
}

void loop() {
  // ======================================================
  // 1. LẮNG NGHE LỆNH ĐIỀU KHIỂN TỪ ESP8266 QUA LORA
  // ======================================================
  while (LORA.available()) {
    String cmd = LORA.readStringUntil('\n');
    cmd.trim();
    Serial.print("NHAN LENH TU ESP: "); Serial.println(cmd);
    
    if (cmd.indexOf("ON") >= 0) {
      espForceBuzzer = true;   
      Serial.println("-> BỘ LỌC CHỐNG NHIỄU: PHÁT HIỆN LỆNH BẬT CÒI OK!");
    } 
    else if (cmd.indexOf("OFF") >= 0) {
      espForceBuzzer = false;  
      Serial.println("-> BỘ LỌC CHỐNG NHIỄU: PHÁT HIỆN LỆNH TẤT CÒI OK!");
    }
  }

  // ======================================================
  // 2. ĐỌC CẢM BIẾN ĐỊNH THỜI VÀ XỬ LÝ LOGIC TRẠNG THÁI (MỖI 1 GIÂY)
  // ======================================================
  if (millis() - lastSensorRead >= sensorInterval) {
    lastSensorRead = millis();
    
    temp = dht.readTemperature();
    hum = dht.readHumidity();
    gas = analogRead(GAS);
    soil = analogRead(SOIL);
    flame1 = digitalRead(FLAME1);

    if (isnan(temp) || isnan(hum)) {
      temp = 0.0;
      hum = 0.0;
      Serial.println("LỖI: Không đọc được cảm biến DHT11!");
    }

    // ĐÃ SỬA: Phản hồi TỨC THÌ không đợi trễ 3 giây để thuận tiện test cảm biến
    flameStable = (flame1 == LOW); 

    bool highTemp = (temp > TEMP_THRESHOLD);
    bool gasHigh = (gas > GAS_THRESHOLD);
    bool soilDry = (soil > SOIL_THRESHOLD);

    // ======================================================
    // ĐÃ ĐỒNG BỘ LOGIC MỚI TOÀN DIỆN THEO YÊU CẦU TEST
    // ======================================================
    if (flameStable) {
      statusText = "FIRE";    // CÓ LỬA -> Báo cháy khẩn cấp lập tức (Hiện FIRE / CHÁY trên App)
    } 
    else if (gasHigh || highTemp || espForceBuzzer) { 
      statusText = "WARNING"; // CÓ KHÓI HOẶC NHIỆT CAO -> Cảnh báo sớm (Hiện WARNING trên App)
    } 
    else {
      statusText = "SAFE";    // Hệ thống an toàn
    }

    // Cập nhật thông số lên màn hình OLED hình ảnh độc lập tại trạm Mega
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(25, 0); display.print("FIRE MONITOR");
    display.drawLine(0, 10, 128, 10, WHITE);
    display.setCursor(0, 15); display.print("T:"); display.print(temp, 1); display.print("C");
    display.setCursor(64, 15); display.print("H:"); display.print(hum, 0); display.print("%");
    display.setCursor(0, 25); display.print("Gas:"); display.print(gas);
    display.setCursor(0, 35); display.print("Soil:"); display.print(soil);
    display.setCursor(0, 45); display.print("Flame Sus:"); display.print(flameStable ? "YES" : "NO");
    display.drawLine(0, 55, 128, 55, WHITE);
    display.setCursor(0, 57); display.print("STATUS: "); display.print(statusText);
    display.display();
  }

  // ======================================================
  // 3. ĐIỀU KHIỂN CÒI VÀ MÀN HÌNH LCD 16x2
  // ======================================================
  if (statusText == "FIRE") {
    lcd.setCursor(0, 0); lcd.print("!!! FIRE !!!    ");
    lcd.setCursor(0, 1); lcd.print("CHAY KHAN CAP   ");
    digitalWrite(BUZZER, HIGH); // Hú còi liên tục báo động cháy lớn
  }
  else if (statusText == "WARNING") {
    lcd.setCursor(0, 0); lcd.print("CANH BAO SOM    ");
    lcd.setCursor(0, 1); lcd.print("KIEM TRA!       ");
    
    if (millis() - lastBeep > 300) {
      buzzerState = !buzzerState;
      digitalWrite(BUZZER, buzzerState);
      lastBeep = millis();
    }
  }
  else {
    lcd.setCursor(0, 0); lcd.print("HE THONG OK     ");
    lcd.setCursor(0, 1); lcd.print("AN TOAN         ");
    digitalWrite(BUZZER, LOW); 
  }

  // ======================================================
  // 4. GỬI DỮ LIỆU SANG ESP8266 QUA LORA ĐỊNH THỜI (MỖI 2 GIÂY)
  // ======================================================
  if (millis() - lastLoraSend >= loraInterval) {
    lastLoraSend = millis();
    
    String data = String(temp, 1) + "," + String(hum, 0) + "," + String(gas) + "," + String(soil) + "," + String(flameStable) + "," + statusText + ",0.0,0.0";
    LORA.println("DATA:" + data);
    
    Serial.print("-> GUI LORA: "); Serial.println("DATA:" + data);
  }
}