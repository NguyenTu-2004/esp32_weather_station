# 🌤️ ESP32 Weather Station System

Dự án Hệ thống Trạm Thời tiết IoT (IoT Weather Station) sử dụng vi điều khiển ESP32 để thu thập dữ liệu môi trường, hiển thị tại chỗ và giám sát từ xa thông qua Dashboard.

---

## 📂 Cấu trúc dự án (Project Structure)

Dự án được chia thành 2 phần chính hoạt động độc lập và liên kết với nhau:

### 1. Trạm đo (Weather Station / Sensor Node)
Đây là thiết bị đặt ngoài trời hoặc tại vị trí cần đo đạc.
* **📂 Thư mục:** `esp32dev_weather_station`
* **🛠️ Phần cứng (Hardware):**
    * **MCU:** ESP32 Dev Module (WROOM-32).
    * **Cảm biến:** BME280 (Đo Nhiệt độ, Độ ẩm, Áp suất khí quyển).
    * **Nguồn:** Pin Li-ion hoặc Adapter 5V.
* **💻 Phần mềm (Software):**
    * Sử dụng **FreeRTOS** để quản lý đa nhiệm.
    * Thư viện điều khiển BME280.
    * Thư viện `esp_wifi`, `mqtt_client`.
* **🌐 Giao thức (Protocols):**
    * **I2C:** Giao tiếp với cảm biến BME280.
    * **MQTT:** Gửi dữ liệu JSON lên Broker (Server).
    * **ESP-NOW:** Gửi dữ liệu tầm gần về Base Station.

### 2. Trạm trung tâm (Base Station / Gateway)
Đây là thiết bị đặt trong nhà để hiển thị thông số nhanh.
* **📂 Thư mục:** `esp32c3_base_station`
* **🛠️ Phần cứng (Hardware):**
    * **MCU:** ESP32-C3 Mini.
    * **Hiển thị:** Màn hình OLED 0.96 inch (SSD1306).
* **💻 Phần mềm (Software):**
    * Xử lý hiển thị giao diện người dùng (UI).
    * Nhận dữ liệu từ Sensor Node.
* **🌐 Giao thức (Protocols):**
    * **I2C:** Giao tiếp với màn hình OLED.
    * **WiFi / ESP-NOW:** Nhận dữ liệu từ trạm đo.

---

## 📊 Tích hợp Node-RED Dashboard

Hệ thống hỗ trợ giám sát trực quan qua Node-RED. Dưới đây là các bước thực hiện để đẩy dữ liệu lên Dashboard:

**Bước 1: Chuẩn bị môi trường**
* Cài đặt **Node-RED** và **Mosquitto MQTT Broker** (trên Raspberry Pi, PC hoặc Cloud).

**Bước 2: Cấu hình ESP32 (Weather Station)**
* Trong code `main.c`, thiết lập `MQTT_URI` trỏ về địa chỉ IP của MQTT Broker.
* Thiết lập Topic publish dữ liệu, ví dụ: `home/weather/status`.
* Dữ liệu gửi đi định dạng JSON: `{"temp": 30.5, "hum": 60, "press": 1013}`.

**Bước 3: Thiết lập Node-RED Flow**
1.  Kéo node **`mqtt in`**: Cấu hình Server và Topic `home/weather/status`.
2.  Kéo node **`json`**: Để giải mã chuỗi JSON nhận được từ ESP32.
3.  Kéo các node **`dashboard`** (Gauge, Chart, Text): Nối vào các trường dữ liệu tương ứng (`msg.payload.temp`, `msg.payload.hum`).
4.  Nhấn **Deploy** và truy cập Dashboard (thường là `http://localhost:1880/ui`).

---

## 📸 Hình ảnh dự án (Images)

### Sơ đồ nguyên lý
<img width="2200" height="1700" alt="esp_bme280-1" src="https://github.com/user-attachments/assets/78401a38-ca1d-4947-90ce-48a73aa4ccb6" />


### Sản phẩm thực tế
<img width="1920" height="2560" alt="image" src="https://github.com/user-attachments/assets/c0ea39f1-c0a8-412e-968c-e32380052989" />


### Giao diện Dashboard
<img width="1280" height="800" alt="VirtualBox_Ubuntu 22 04_02_01_2026_22_37_11" src="https://github.com/user-attachments/assets/053d9e6f-f824-4d27-8ea5-0ac939b53dd0" />


---
