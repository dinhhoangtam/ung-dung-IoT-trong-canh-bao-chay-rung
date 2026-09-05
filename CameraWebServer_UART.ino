#include "esp_camera.h"
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <HTTPClient.h>   // Thư viện gửi HTTP POST lên Cloud AI
#include <ArduinoJson.h>  // Thư viện giải mã chuỗi JSON từ AI trả về

// Các thư viện bổ trợ Token và RTDB của Firebase Client
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// Thư viện mã hóa Base64 phần cứng có sẵn trong ESP32
#include "mbedtls/base64.h"

// Thư viện điều khiển màn hình LCD I2C
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================= CẤU HÌNH MÀN HÌNH LCD I2C (20 CỘT X 4 DÒNG) =================
#define I2C_SDA       15  
#define I2C_SCL       14  
// Khởi tạo LCD với địa chỉ 0x27 (hoặc 0x3F tùy module của bạn), 20 cột, 4 dòng
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ================= CẤU HÌNH CÒI BÁO CHÁY (BUZZER) =================
#define BUZZER_PIN    12  // Chân IO12 điều khiển còi hú

// ================= 1. CẤU HÌNH WIFI THU (STA - WiFi Nhà) =================
const char* ssid = "Cua T ";
const char* password = "idontknown";

// ================= 2. CẤU HÌNH WIFI PHÁT (AP - Mạch tự phát) =================
const char* ap_ssid = "ESP32-CAM";
const char* ap_password = "12345678";

// ================= 3. CẤU HÌNH TÀI KHOẢN FIREBASE =================
#define API_KEY "AIzaSyD-gZPx2DjGes_nu63K1GyU1zo_LVjB0dE" 
#define DATABASE_URL "https://iot-can-bao-chay-default-rtdb.firebaseio.com/" 
#define USER_EMAIL "esp32cam@gmail.com"     
#define USER_PASSWORD "123456"               

// ================= CẤU HÌNH API ROBOFLOW CỦA BẠN =================
const char* rb_api_key = "WnGmHc8X1k0bZmxbltSu"; 
const char* rb_workspace = "dinh-hoang-tam"; 
const char* rb_workflow = "detect-count-and-visualize"; 

// ================= 4. SƠ ĐỒ CHÂN CAMERA (AI-THINKER) =================
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// Khai báo hàm khởi động Web Server (nằm ở tab app_httpd.cpp)
void startCameraServer();

#define FLASH_LED 4

// ================= BIẾN THUẬT TOÁN MA TRẬN MÀU =================
int lastCenter = 0;
int L = 0, C = 0, R = 0;
int flicker = 0;
int whiteLight = 0;
bool fireNow = false;
bool aiConfirmed = false; // Biến lưu kết quả xác nhận từ AI

int currentBrightness = 0;
int brightness_threshold = 40; 

unsigned long lastCaptureTime = 0;
unsigned long captureInterval = 3000; 
unsigned long lastWifiCheck = 0;
const unsigned long wifiCheckInterval = 10000;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig configF;

// ================= HÀM DETECT FIRE GỐC CỦA BẠN (GIỮ NGUYÊN ĐỂ CHẠY RGB565) =================
bool detectFire(camera_fb_t *fb) {
  if (!fb) return false;

  L = C = R = 0;
  whiteLight = 0;
  
  long totalBrightness = 0;
  long sampleCount = 0;
  int step = 6;

  for (int i = 0; i < fb->len; i += step * 2) {
    uint16_t pixel = (fb->buf[i] << 8) | fb->buf[i + 1];

    int r = ((pixel >> 11) & 0x1F) << 3;
    int g = ((pixel >> 5) & 0x3F) << 2;
    int b = (pixel & 0x1F) << 3;

    int brightness = (r + g + b) / 3;
    
    totalBrightness += brightness;
    sampleCount++;

    bool isFireColor = (r > 100 && g > 50 && r > g && (r - b) > 40);
    bool brightFire = (brightness > 180 && (r - b) > 30);
    bool isWhite = (abs(r - g) < 20 && abs(g - b) < 20 && brightness > 160);

    if (isWhite) whiteLight++;

    if (isFireColor || brightFire) {
      int pos = (i / 2) % 320;
      if (pos < 100) L++;
      else if (pos < 200) C++;
      else R++;
    }
  }

  if (sampleCount > 0) {
    currentBrightness = totalBrightness / sampleCount;
  }

  flicker = abs(C - lastCenter);
  lastCenter = C;

  bool centerHot = C > 5;
  bool flickerOK = flicker > 4;
  bool notWhite = whiteLight < 200;
  bool notGlobalLight = !(L > 25 && C > 25 && R > 25);
  bool focusFire = (C > L && C > R);

  if (centerHot && flickerOK && notWhite && notGlobalLight && focusFire){
    return true;
  }
  return false;
}

// ================= HÀM KHÁCH GỬI ẢNH LÊN ROBOFLOW WORKFLOWS CẬP NHẬT TRỰC TIẾP =================
bool verifyWithRoboflowAI(uint8_t* jpg_buf, size_t jpg_len) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("-> [Cloud AI] Chưa kết nối WiFi STA, bỏ qua kiểm tra AI.");
    return false;
  }

  Serial.println("-> [Cloud AI] Thuật toán nghi ngờ cháy! Đang gửi ảnh lên Roboflow để AI xác thực...");
  
  HTTPClient http;
  String url = "https://detect.roboflow.com/workflows/" + String(rb_workspace) + "/" + String(rb_workflow) + "?api_key=" + String(rb_api_key);
  
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");

  int httpResponseCode = http.POST(jpg_buf, jpg_len);
  bool hasFireOrSmoke = false;

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("-> [Cloud AI] Đã nhận dữ liệu phân tích JSON thành công:");
    Serial.println(response); 

    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      JsonArray predictions = doc["outputs"][0]["predictions"].as<JsonArray>();
      for (JsonObject pred : predictions) {
        String label = pred["class"].as<String>();
        float confidence = pred["confidence"].as<float>();

        Serial.printf("   [AI tìm thấy]: %s | Độ chính xác: %.2f%%\n", label.c_str(), confidence * 100);

        if ((label == "fire" || label == "smoke") && confidence >= 0.45) {
          hasFireOrSmoke = true;
          break;
        }
      }
    }
  } else {
    Serial.printf("-> [Cloud AI] Lỗi kết nối Server, mã HTTP: %d\n", httpResponseCode);
  }
  http.end();
  return hasFireOrSmoke;
}

// ================= HÀM DRAW LCD 20X4 TRỰC QUAN TỐI ƯU KHÔNG GIAN =================
void drawLCD() {
  lcd.clear();

  // Dòng 1: Tiêu đề trạng thái hệ thống
  lcd.setCursor(0, 0);
  if (fireNow) {
    if (aiConfirmed) {
      lcd.print("STATUS: DANGER FIRE!");
    } else {
      lcd.print("STATUS: CHECKING AI ");
    }
  } else {
    lcd.print("STATUS: SAFE SYSTEM ");
  }

  // Dòng 2: Địa chỉ IP Local (STA) khi kết nối mạng nhà
  lcd.setCursor(0, 1);
  lcd.print("STA IP:");
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print(WiFi.localIP());
  } else {
    lcd.print("DISCONNECTED ");
  }

  // Dòng 3: Hiển thị các giá trị Ma trận màu L, C, R
  lcd.setCursor(0, 2);
  char lcr_buf[21];
  snprintf(lcr_buf, sizeof(lcr_buf), "L:%-3d  C:%-3d  R:%-3d", L, C, R);
  lcd.print(lcr_buf);

  // Dòng 4: Hiển thị độ nhấp nháy Flicker và Đèn trắng White
  lcd.setCursor(0, 3);
  char flk_wt_buf[21];
  snprintf(flk_wt_buf, sizeof(flk_wt_buf), "Fli:%-4d Whi:%-5d", flicker, whiteLight);
  lcd.print(flk_wt_buf);
}

// ================= HÀM MÃ HÓA BASE64 GỐC =================
String convertToBase64(uint8_t* buf, size_t len) {
  size_t output_len;
  size_t expected_len = 4 * ((len + 2) / 3) + 1;
  unsigned char* output = (unsigned char*)malloc(expected_len);
  
  if (!output) return "";

  mbedtls_base64_encode(output, expected_len, &output_len, buf, len);
  output[output_len] = '\0'; 
  String base64Str = String((char*)output);
  
  free(output); 
  return base64Str;
}

// ================= SETUP NÂNG CẤP BỘ ĐỆM ĐA LUỒNG =================
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); 

  // Khởi tạo giao tiếp I2C cho LCD
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM INITIALIZING");

  pinMode(FLASH_LED, OUTPUT);
  digitalWrite(FLASH_LED, LOW); 

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
  
  config.pixel_format = PIXFORMAT_RGB565; 
  config.frame_size = FRAMESIZE_QVGA;    
  config.jpeg_quality = 12;
  config.fb_count = 2;                  

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera khởi tạo thất bại!");
    lcd.setCursor(0, 1);
    lcd.print("CAMERA INIT FAILED!");
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid, ap_password);
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  
  drawLCD();

  configF.api_key = API_KEY;
  configF.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&configF, &auth);
  Firebase.reconnectWiFi(true);

  fbdo.setBSSLBufferSize(16384, 2048); 
  fbdo.setResponseSize(2048);          

  // Khởi chạy Web Server truyền dữ liệu Stream video trực tiếp
  startCameraServer();
  Serial.println("Hệ thống khởi động hoàn tất! Sẵn sàng vừa Stream vừa chạy AI.");
}

// ================= LOOP KHÔNG NGHẼN (NON-BLOCKING) VỪA STREAM VỪA CHECK AI =================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiCheck > wifiCheckInterval) {
      lastWifiCheck = millis();
      WiFi.begin(ssid, password);
    }
  }

  if (millis() - lastCaptureTime >= captureInterval) {
    lastCaptureTime = millis();

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      drawLCD();
      return; 
    }

    fireNow = detectFire(fb);
    aiConfirmed = false; 

    uint8_t * jpg_buf = NULL;
    size_t jpg_buf_len = 0;
    bool converted = frame2jpg(fb, 12, &jpg_buf, &jpg_buf_len);

    if (converted && jpg_buf != NULL) {
      
      if (fireNow) {
        aiConfirmed = verifyWithRoboflowAI(jpg_buf, jpg_buf_len);
      }

      String statusPayload = "SAFE";

      if (fireNow && aiConfirmed) {
        Serial.println("🔥 AI VÀ PHẦN CỨNG XÁC NHẬN: CHÁY THẬT SỰ! KÍCH HOẠT CÒI HÚ!");
        captureInterval = 1000; 
        digitalWrite(BUZZER_PIN, HIGH); 
        statusPayload = "fire";         
      } else {
        captureInterval = 3000; 
        digitalWrite(BUZZER_PIN, LOW);  
        statusPayload = "SAFE";
      }

      if (currentBrightness < brightness_threshold) {
        digitalWrite(FLASH_LED, HIGH);
      } else {
        digitalWrite(FLASH_LED, LOW);
      }

      // 2. CẬP NHẬT MÀN HÌNH LCD
      drawLCD();

      // 3. ĐỒNG BỘ LÊN FIREBASE ĐỂ CẬP NHẬT GIAO DIỆN APP MIT APP INVENTOR
      if (Firebase.ready()) {
        String imgBase64 = convertToBase64(jpg_buf, jpg_buf_len);

        if (imgBase64 != "") {
          bool success = true;
          
          success &= Firebase.RTDB.setString(&fbdo, "/FireMonitor/Cam/status", statusPayload);
          success &= Firebase.RTDB.setTimestamp(&fbdo, "/FireMonitor/Cam/last_update");
          success &= Firebase.RTDB.setString(&fbdo, "/FireMonitor/Cam/image_data", "data:image/jpeg;base64," + imgBase64);

          if (success) {
            Serial.println("💾 [Firebase] Đồng bộ trạng thái và hình ảnh nền thành công!");
          }
        }
      }
      
      free(jpg_buf); 
    }
    
    esp_camera_fb_return(fb); 
  }
}