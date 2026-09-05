# ung-dung-IoT-trong-canh-bao-chay-rung
# MÔ HÌNH ỨNG DỤNG IoT TRONG CẢNH BÁO CHÁY RỪNG

## Giới thiệu

Cháy rừng là một trong những thảm họa thiên nhiên gây thiệt hại nghiêm trọng đến tài nguyên rừng, môi trường sinh thái và đời sống con người. Việc phát hiện sớm các dấu hiệu cháy có vai trò rất quan trọng trong công tác phòng cháy và chữa cháy rừng.

Dự án xây dựng mô hình ứng dụng Internet of Things (IoT) nhằm giám sát và cảnh báo sớm nguy cơ cháy rừng thông qua việc thu thập, xử lý dữ liệu môi trường từ các cảm biến và gửi cảnh báo đến người quản lý theo thời gian thực.

---

## Mục tiêu

- Giám sát liên tục các thông số môi trường trong khu vực rừng.
- Phát hiện sớm các dấu hiệu có nguy cơ gây cháy.
- Cảnh báo kịp thời cho lực lượng quản lý và phòng cháy chữa cháy.
- Hiển thị dữ liệu trực quan trên Website và thiết bị di động.
- Giảm thiểu thiệt hại về tài nguyên rừng và môi trường.

---

## Công nghệ sử dụng

### Phần cứng

- ESP8266 NodeMCU
- Cảm biến nhiệt độ và độ ẩm DHT11/DHT22
- Cảm biến khí gas MQ-2
- Cảm biến lửa Flame Sensor
- Buzzer cảnh báo
- LED cảnh báo
- Nguồn cấp 5V

### Phần mềm

- Arduino IDE
- MQTT Protocol
- PHP
- MySQL
- HTML, CSS, JavaScript
- XAMPP Server

---

## Kiến trúc hệ thống

```text
+----------------+
|   Cảm biến     |
| DHT11, MQ-2,   |
| Flame Sensor   |
+-------+--------+
        |
        v
+----------------+
|  ESP8266 WiFi  |
+-------+--------+
        |
        | Internet
        v
+----------------+
| Web Server     |
| PHP + MySQL    |
+-------+--------+
        |
        v
+----------------+
| Website Giám   |
| sát và Cảnh báo|
+----------------+
```

---

## Chức năng chính

### 1. Thu thập dữ liệu

Hệ thống liên tục thu thập:

- Nhiệt độ môi trường
- Độ ẩm không khí
- Nồng độ khí gas
- Tín hiệu phát hiện lửa

### 2. Xử lý dữ liệu

ESP8266 phân tích dữ liệu từ các cảm biến và xác định mức độ nguy hiểm dựa trên các ngưỡng được cấu hình trước.

Ví dụ:

| Điều kiện | Trạng thái |
|------------|------------|
| Nhiệt độ > 50°C | Cảnh báo |
| MQ-2 phát hiện khói | Cảnh báo |
| Flame Sensor phát hiện lửa | Nguy hiểm |

### 3. Cảnh báo

Khi phát hiện nguy cơ cháy:

- Kích hoạt còi Buzzer.
- Bật đèn LED cảnh báo.
- Gửi dữ liệu lên Website.
- Hiển thị trạng thái cảnh báo thời gian thực.

### 4. Giám sát từ xa

Người dùng có thể:

- Xem dữ liệu cảm biến.
- Theo dõi trạng thái hệ thống.
- Kiểm tra lịch sử cảnh báo.
- Giám sát từ điện thoại hoặc máy tính.

---

## Cơ sở dữ liệu

### Bảng sensor_data

| Trường | Kiểu dữ liệu | Mô tả |
|----------|-------------|---------|
| id | INT | Khóa chính |
| temperature | FLOAT | Nhiệt độ |
| humidity | FLOAT | Độ ẩm |
| gas | FLOAT | Nồng độ khí |
| flame | INT | Trạng thái lửa |
| created_at | DATETIME | Thời gian ghi nhận |

---

## Quy trình hoạt động

1. Cảm biến thu thập dữ liệu môi trường.
2. ESP8266 xử lý và gửi dữ liệu lên Server.
3. Server lưu dữ liệu vào MySQL.
4. Website hiển thị dữ liệu theo thời gian thực.
5. Nếu phát hiện nguy cơ cháy:
   - Buzzer kêu.
   - LED sáng.
   - Website hiển thị cảnh báo.

---

## Ưu điểm

- Chi phí triển khai thấp.
- Giám sát liên tục 24/7.
- Cảnh báo sớm nguy cơ cháy.
- Dễ dàng mở rộng quy mô.
- Theo dõi từ xa thông qua Internet.

---

## Hạn chế

- Phụ thuộc vào kết nối Internet.
- Phạm vi cảm biến còn giới hạn.
- Chưa tích hợp định vị GPS và AI phân tích hình ảnh.

---

## Hướng phát triển

- Tích hợp GPS xác định vị trí cháy.
- Gửi cảnh báo qua SMS và Email.
- Kết hợp Camera AI nhận diện khói và lửa.
- Sử dụng năng lượng mặt trời cho các nút cảm biến.
- Triển khai mạng cảm biến diện rộng trong rừng.

---

## Kết luận

Mô hình ứng dụng IoT trong cảnh báo cháy rừng giúp phát hiện sớm các nguy cơ cháy thông qua hệ thống cảm biến và kết nối Internet. Giải pháp góp phần nâng cao hiệu quả quản lý rừng, hỗ trợ công tác phòng cháy chữa cháy, giảm thiểu thiệt hại về tài nguyên thiên nhiên và bảo vệ môi trường.
