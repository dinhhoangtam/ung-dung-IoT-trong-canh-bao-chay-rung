#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <FirebaseESP8266.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>

// ======================================================
// FIREBASE CONFIG
// ======================================================
#define FIREBASE_HOST "iot-can-bao-chay-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "nvCKoFQ9KeySWOCag3D7Hngf5OSwHe8QkhO4DAUx"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

float nguong_temp = 40.0;
int nguong_gas = 400;
int manual_buzzer = 0;   
unsigned long lastFetchTime = 0;
const unsigned long fetchInterval = 10000; 

// ======================================================
// TELEGRAM CONFIG & COOLDOWN MANAGEMENT
// ======================================================
const char* telegramToken = "8927725994:AAFZ47AP1RPDy-h6Li1vgZt6stCZ7bG_f18"; 
const char* telegramChatID = "6475611655"; 

bool telegramGasAlertSent = false;   
bool telegramFlameAlertSent = false; 

unsigned long lastTelegramGasTime = 0;
unsigned long lastTelegramFlameTime = 0;
const unsigned long telegramCooldown = 60000; 

// ======================================================
// OLED CONFIG (SSD1306)
// ======================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ======================================================
// LORA & SIM808 (SOFTWARE SERIAL)
// ======================================================
SoftwareSerial lora(D7, D6);   
SoftwareSerial sim(D5, D8);    

String myPhoneNumber = "0932888747";
bool alertTriggered = false;
unsigned long lastGpsFetchTime = 0;
const unsigned long gpsFetchInterval = 15000; 

// ======================================================
// WIFI & SERVER CONFIG
// ======================================================
const char* ssid = "Cua T ";
const char* password = "idontknown";

ESP8266WebServer server(80);
WiFiClient client; 
String apiKey = "3C26QK0TWAA5FXTR";

float temp = 0;
float hum = 0;
int gas = 0;
int soil = 0;
int flame = 0;
String statusText = "SAFE";
float latitude = 0;
float longitude = 0;

// ======================================================
// HÀM GỬI TELEGRAM ĐÃ TỐI ƯU QUẢN LÝ RAM (FIX LỖI -1)
// ======================================================
void sendTelegramMessage(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("-> [Telegram] Không có WiFi, hủy gửi tin nhắn.");
    return;
  }

  WiFiClientSecure tgClient;
  HTTPClient http;

  tgClient.setInsecure();
  tgClient.setBufferSizes(512, 512); 

  // Tạo chuỗi nội dung tạm để hiển thị debug log lên Serial dễ đọc hơn
  String rawLogMsg = message;

  message.replace(" ", "%20");
  message.replace("\n", "%0A");
  
  String url = "/bot" + String(telegramToken) + "/sendMessage?chat_id=" + String(telegramChatID) + "&text=" + message;
  
  Serial.println("-> [Telegram] Đang kết nối đến api.telegram.org...");
  
  if (http.begin(tgClient, "api.telegram.org", 443, url)) {
    http.setTimeout(4000); 
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.printf("-> [Telegram] GỬI THÀNH CÔNG! Mã phản hồi: %d\n", httpCode);
      Serial.println("   Nội dung: " + rawLogMsg);
    } else {
      Serial.printf("-> [Telegram] LỖI GỬI, mã lỗi: %d (Sự cố kết nối/Timeout)\n", httpCode);
    }
    http.end();
  } else {
    Serial.println("-> [Telegram] Hệ thống không thể khởi tạo cấu trúc HTTPS!");
  }
}

// ======================================================
// HÀM LẤY TỌA ĐỘ GPS TỪ MODULE SIM808
// ======================================================
void getGPS() {
  sim.println("AT+CGNSINF");
  delay(500); 

  String data = "";
  while (sim.available()) {
    char c = sim.read();
    data += c;
  }

  Serial.println("\n--- SIM808 GPS RAW DATA ---");
  Serial.println(data);
  Serial.println("---------------------------");

  int index = data.indexOf("+CGNSINF:");
  if (index == -1) {
    Serial.println("-> [GPS] Lỗi lệnh hoặc Module chưa phản hồi.");
    return;
  }

  String gps = data.substring(index);
  int c1 = gps.indexOf(',');
  int c2 = gps.indexOf(',', c1 + 1);
  int c3 = gps.indexOf(',', c2 + 1);
  int c4 = gps.indexOf(',', c3 + 1);
  int c5 = gps.indexOf(',', c4 + 1);
  
  String fixStatus = gps.substring(c2 + 1, c3);
  if (fixStatus != "1") {
    Serial.println("-> [GPS] Đang quét vệ tinh... Trạng thái: CHƯA FIX TỌA ĐỘ");
    return;
  }

  latitude = gps.substring(c3 + 1, c4).toFloat();
  longitude = gps.substring(c4 + 1, c5).toFloat();

  Serial.print("-> [GPS] ĐÃ CẬP NHẬT TỌA ĐỘ THÀNH CÔNG -> ");
  Serial.printf("Vĩ độ (Lat): %.6f | Kinh độ (Lon): %.6f\n", latitude, longitude);
}

// ======================================================
// CẬP NHẬT TRẠNG THÁI HIỂN THỊ MÀN HÌNH OLED
// ======================================================
void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("FIRE MONITOR");
  display.print("IP:"); display.println(WiFi.localIP());
  display.print("T:"); display.print(temp, 1); display.print("C H:"); display.print(hum, 0); display.println("%");
  display.print("Gas:"); display.println(gas);
  display.print("Soil:"); display.println(soil);
  display.print("Flame:");
  display.println(flame == 1 ? "FIRE" : "SAFE");
  display.print("Status:"); display.println(statusText);
  display.display();
}

// ======================================================
// TIẾN TRÌNH KHẨN CẤP: GỬI SMS & GỌI ĐIỆN BÁO ĐỘNG
// ======================================================
void triggerEmergencyAlert(String message) {
  if (alertTriggered) return;
  Serial.println("\n⚠️ >>> PHÁT HIỆN HỎA HOẠN! KÍCH HOẠT SIM808 GỬI KHẨN CẤP... <<<");
  
  sim.listen(); 
  sim.println("AT+CMGF=1");
  delay(500);
  sim.print("AT+CMGS=\"");
  sim.print(myPhoneNumber);
  sim.println("\"");
  delay(500);
  sim.print(message);
  delay(500);
  sim.write(26);
  delay(3000);
  Serial.println("-> [SIM808] Đã hoàn tất lệnh gửi SMS báo cháy!");

  Serial.println("-> [SIM808] Đang thực hiện quay số gọi điện khẩn cấp...");
  sim.print("ATD");
  sim.print(myPhoneNumber);
  sim.println(";");
  delay(10000); 
  sim.println("ATH");
  Serial.println("-> [SIM808] Kết thúc cuộc gọi báo động.");

  lora.listen();
  alertTriggered = true;
}

// ======================================================
// CÁC HÀM XỬ LÝ GIAO DIỆN LOCAL WEB SERVER
// ======================================================
void handleRoot() {
  String html = "<!DOCTYPE html><html lang='vi'><head>";
  html += "<meta charset='UTF-8'>";
  // Bổ sung thẻ viewport để giao diện tự co giãn trên điện thoại
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='2'>";
  html += "<title>Fire Monitor System</title>";
  html += "<style>";
  // Cấu hình lại màu nền sáng và font chữ hiện đại
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f0f4f8; color: #333; text-align: center; margin: 0; padding: 20px; }";
  html += "h1 { color: #102a43; margin-bottom: 25px; font-weight: 700; text-transform: uppercase; letter-spacing: 1px; font-size: 24px; }";
  html += ".container { max-width: 650px; margin: 0 auto; }";
  // Thiết kế dạng thẻ (card) trắng, đổ bóng
  html += ".card { background: #ffffff; border-radius: 16px; box-shadow: 0 4px 20px rgba(0,0,0,0.05); margin-bottom: 20px; padding: 25px; transition: transform 0.2s ease-in-out; }";
  html += ".card:hover { transform: translateY(-3px); }";
  html += "h2 { margin-top: 0; color: #334e68; font-size: 18px; border-bottom: 2px solid #bcccdc; padding-bottom: 12px; margin-bottom: 15px; }";
  // Dàn thông tin cảm biến dàn đều 2 bên
  html += "p { font-size: 16px; margin: 12px 0; color: #486581; display: flex; justify-content: space-between; padding: 0 10%; }";
  html += ".bold { font-weight: 700; color: #102a43; }";
  // Thiết kế nút bấm 3D đẹp mắt
  html += "button { background: linear-gradient(135deg, #3498db, #2980b9); color: #fff; padding: 12px 28px; font-size: 16px; font-weight: bold; border: none; border-radius: 10px; cursor: pointer; text-decoration: none; display: inline-block; margin-top: 15px; box-shadow: 0 4px 15px rgba(52, 152, 219, 0.4); transition: all 0.3s; }";
  html += "button:hover { background: linear-gradient(135deg, #2980b9, #2471a3); transform: scale(1.05); box-shadow: 0 6px 20px rgba(52, 152, 219, 0.6); }";
  html += ".status-card { padding: 35px; }";
  // Màu sắc và animation trạng thái
  html += ".fire { color: #d64545; font-size: 32px; font-weight: 900; animation: blink 1s infinite; margin: 0; text-transform: uppercase; text-align: center; justify-content: center; display: block; }";
  html += ".warn { color: #f59f00; font-size: 30px; font-weight: 800; margin: 0; text-transform: uppercase; text-align: center; justify-content: center; display: block; }";
  html += ".ok { color: #0ca678; font-size: 30px; font-weight: 800; margin: 0; text-transform: uppercase; text-align: center; justify-content: center; display: block; }";
  html += "@keyframes blink { 0% { opacity: 1; text-shadow: 0 0 10px #d64545; } 50% { opacity: 0.6; text-shadow: none; } 100% { opacity: 1; text-shadow: 0 0 10px #d64545; } }";
  html += "@media (max-width: 500px) { p { flex-direction: column; text-align: center; gap: 5px; } }";
  html += "</style></head><body>";

  html += "<div class='container'>";
  html += "<h1>FIRE MONITOR SYSTEM</h1>";

  // Khối WIFI
  html += "<div class='card'>";
  html += "<h2>🌐 WIFI INFO</h2>";
  html += "<p><span>IP ESP8266:</span> <span class='bold'>" + WiFi.localIP().toString() + "</span></p>";
  html += "</div>";

  // Khối CẢM BIẾN
  html += "<div class='card'>";
  html += "<h2>📊 SENSOR DATA</h2>";
  html += "<p><span>Nhiệt độ:</span> <span class='bold'>" + String(temp, 2) + " &deg;C</span></p>";
  html += "<p><span>Độ ẩm:</span> <span class='bold'>" + String(hum, 2) + " %</span></p>";
  html += "<p><span>Khí Gas:</span> <span class='bold'>" + String(gas) + "</span></p>";
  html += "<p><span>Độ ẩm đất:</span> <span class='bold'>" + String(soil) + "</span></p>";
  html += "<p><span>Cảm biến Lửa:</span> <span class='bold' style='color:" + String(flame == 1 ? "#d64545" : "#0ca678") + "'>" + (flame == 1 ? "PHÁT HIỆN CHÁY 🔥" : "AN TOÀN 🟢") + "</span></p>";
  html += "</div>";

  // Khối GPS
  html += "<div class='card'>";
  html += "<h2>📍 GPS LOCATION</h2>";
  html += "<p><span>Vĩ độ (Lat):</span> <span class='bold'>" + String(latitude, 6) + "</span></p>";
  html += "<p><span>Kinh độ (Lon):</span> <span class='bold'>" + String(longitude, 6) + "</span></p>";
  // Đã sửa lại URL bản đồ chuẩn của Google Maps để nhảy tọa độ chính xác hơn
  html += "<a href='https://maps.google.com/?q=" + String(latitude, 6) + "," + String(longitude, 6) + "' target='_blank' style='text-decoration:none;'><button>OPEN MAP</button></a>";
  html += "</div>";

  // Khối TRẠNG THÁI (SAFE/FIRE/WARNING)
  html += "<div class='card status-card'>";
  if (statusText == "FIRE") html += "<p class='fire'>🔥 FIRE ALERT 🔥</p>";
  else if (statusText == "WARNING") html += "<p class='warn'>⚠️ WARNING ⚠️</p>";
  else html += "<p class='ok'>✅ SAFE ✅</p>";
  html += "</div>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleData() {
  String json = "{";
  json += "\"temp\":" + String(temp) + ",\"hum\":" + String(hum) + ",\"gas\":" + String(gas) + ",";
  json += "\"soil\":" + String(soil) + ",\"flame\":\"" + String(flame == 1 ? "CHÁY" : "KHÔNG CHÁY") + "\",";
  json += "\"lat\":" + String(latitude, 6) + ",\"lon\":" + String(longitude, 6) + ",\"status\":\"" + statusText + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(9600);
  lora.begin(9600);
  sim.begin(9600);
  delay(2000);

  Serial.println("\n=================================");
  Serial.println("  HỆ THỐNG CẢNH BÁO CHÁY KHỞI ĐỘNG ");
  Serial.println("=================================");

  sim.listen();
  sim.println("AT");
  delay(500);
  sim.println("AT+CMGF=1"); delay(500);
  sim.println("AT+CGNSPWR=1"); delay(500);
  sim.println("AT+CGNSSEQ=\"RMC\""); delay(500);
  Serial.println("-> [Mạch SIM808] Đã cấu hình khởi tạo.");
  
  lora.listen();
  Serial.println("-> [Mạch LoRa] Đang chờ gói tin đồng bộ từ Arduino Mega...");

  Wire.begin(D2, D1);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  WiFi.begin(ssid, password);
  Serial.print("-> [WiFi] Đang kết nối mạng");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n-> [WiFi] KẾT NỐI THÀNH CÔNG!");
  Serial.print("   Địa chỉ IP cục bộ của ESP8266: ");
  Serial.println(WiFi.localIP());

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("-> [Firebase] Đã kết nối cơ sở dữ liệu Cloud.");

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  
  updateOLED();
}

void loop() {
  server.handleClient();

  // Kiểm tra GPS định kỳ
  if (millis() - lastGpsFetchTime >= gpsFetchInterval) {
    lastGpsFetchTime = millis();
    sim.listen();
    getGPS();
    lora.listen(); 
  }

  // Đồng bộ cấu hình từ Firebase
  if (millis() - lastFetchTime >= fetchInterval) {
    lastFetchTime = millis();
    if (Firebase.getString(fbdo, "/FireMonitor/Config/nguong_temp")) {
      String strTemp = fbdo.stringData();
      if(strTemp.length() > 0) nguong_temp = strTemp.toFloat();
    }
    if (Firebase.getString(fbdo, "/FireMonitor/Config/nguong_gas")) {
      String strGas = fbdo.stringData();
      if(strGas.length() > 0) nguong_gas = strGas.toInt();
    }
    if (Firebase.getInt(fbdo, "/FireMonitor/Config/manual_buzzer")) {
      manual_buzzer = fbdo.intData();
    }
    Serial.printf("\n[Firebase Sync] Ngưỡng nhiệt: %.1f°C | Ngưỡng Gas: %d ppm | Ép còi từ App: %s\n", 
                  nguong_temp, nguong_gas, manual_buzzer == 1 ? "BẬT" : "TẮT");
  }

  // Xử lý dữ liệu nhận từ LoRa
  if (lora.available()) {
    String data = lora.readStringUntil('\n');
    data.trim();

    if (data.startsWith("DATA:")) {
      Serial.println("\n--- NHẬN DỮ LIỆU MỚI TỪ LORA ---");
      Serial.println("Chuỗi Raw: " + data);
      
      data.remove(0, 5);
      char status[20];
      sscanf(data.c_str(), "%f,%f,%d,%d,%d,%[^,]", &temp, &hum, &gas, &soil, &flame, status);

      // In dữ liệu cảm biến tường minh ra Serial Monitor
      Serial.printf("Nhiệt độ: %.2f °C | Bằng trắc độ ẩm: %.2f %%\n", temp, hum);
      Serial.printf("Khí Gas: %d ppm | Độ ẩm đất: %d\n", gas, soil);
      Serial.printf("Cảm biến Lửa: %s\n", flame == 1 ? "PHÁT HIỆN CÓ LỬA 🔥" : "AN TOÀN 🟢");

      if (flame == 1) {
        statusText = "FIRE";
      } else if (temp >= nguong_temp || gas >= nguong_gas) {
        statusText = "WARNING";
      } else {
        statusText = "SAFE";
      }
      Serial.println("Trạng thái hệ thống: " + statusText);

      // Phản hồi lệnh điều khiển ngược về Mega qua LoRa
      if (statusText == "FIRE" || statusText == "WARNING" || manual_buzzer == 1) {
        noInterrupts(); lora.println("[ON]"); interrupts();
        Serial.println("-> Phản hồi LoRa: [ON] (Bật còi tại chỗ)");
      } else {
        noInterrupts(); lora.println("[OFF]"); interrupts();
        Serial.println("-> Phản hồi LoRa: [OFF] (Tắt còi tại chỗ)");
      }

      updateOLED();

      // ======================================================
      // TIẾN TRÌNH GỬI TELEGRAM ĐƯỢC GIÃN CÁCH COOLDOWN
      // ======================================================
      if (flame == 1) { 
        if (!telegramFlameAlertSent || (millis() - lastTelegramFlameTime >= telegramCooldown)) {
          lastTelegramFlameTime = millis(); 
          sendTelegramMessage("KHAN CAP!!!\nPhat hien LUA\nHay kiem tra ngay");
          telegramFlameAlertSent = true;
        }
      } else {
        telegramFlameAlertSent = false; 
      }

      if (gas >= nguong_gas) {
        if (!telegramGasAlertSent || (millis() - lastTelegramGasTime >= telegramCooldown)) {
          lastTelegramGasTime = millis();
          sendTelegramMessage("CANH BAO!!!\nPhat hien khi GAS nguy hiem\nNong do: " + String(gas) + " ppm");
          telegramGasAlertSent = true;
        }
      } else {
        telegramGasAlertSent = false; 
      }

      // Xử lý còi phụ khẩn cấp và SMS cuộc gọi từ SIM808
      if (statusText == "FIRE" || statusText == "WARNING") {
        if (!alertTriggered) {
          String smsPayload = "CANH BAO NGUY HIEM!\nNhiet do: " + String(temp) + " C\nGas: " + String(gas);
          triggerEmergencyAlert(smsPayload);
        }
      } else {
        alertTriggered = false;
      }

      // Đẩy dữ liệu lên Firebase
      FirebaseJson fbJson;
      fbJson.set("temp", temp); fbJson.set("hum", hum); fbJson.set("gas", gas);
      fbJson.set("soil", soil); fbJson.set("flame", flame == 1 ? "CHÁY" : "KHÔNG CHÁY");
      fbJson.set("status", statusText); fbJson.set("latitude", latitude); fbJson.set("longitude", longitude);
      if(Firebase.updateNode(fbdo, "/FireMonitor/CurrentData", fbJson)) {
         Serial.println("-> [Cloud] Đồng bộ Firebase: THÀNH CÔNG");
      }

      // Đẩy dữ liệu lên ThingSpeak
      if (client.connect("api.thingspeak.com", 80)) {
        String url = "/update?api_key=" + apiKey + "&field1=" + String(temp) + "&field2=" + String(hum) + "&field3=" + String(gas) + "&field4=" + String(soil) + "&field5=" + String(flame);
        client.print(String("GET ") + url + " HTTP/1.1\r\nHost: api.thingspeak.com\r\nConnection: close\r\n\r\n");
        Serial.println("-> [ThingSpeak] Đẩy dữ liệu đồ thị: THÀNH CÔNG");
      }
      client.stop();
      Serial.println("--------------------------------");
    }
  }
  delay(10); 
}