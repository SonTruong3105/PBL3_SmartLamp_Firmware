# Smart Desk Lamp AI — Đèn Bàn Thông Minh Tích Hợp Trợ Lý Giọng Nói AI (PBL3)

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0%2B-blue.svg)](https://github.com/espressif/esp-idf)
[![Target](https://img.shields.io/badge/Target-ESP32--S3-red.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Protocol](https://img.shields.io/badge/Protocol-MCP%20%7C%20WebSocket%20%7C%20Opus-green.svg)](https://modelcontextprotocol.io/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

> **Đồ án PBL3 / Dự án IoT & AI Nhúng**: Hệ thống đèn bàn thông minh học tập/làm việc đa chức năng, tích hợp cảm biến bảo vệ thị lực & sức khỏe môi trường, màn hình giao diện tiếng Việt và trợ lý giọng nói AI tương tác thời gian thực thông qua giao thức **Model Context Protocol (MCP)**.

---

## 📑 Mục Lục
- [1. Giới thiệu tổng quan](#1-giới-thiệu-tổng-quan)
- [2. Kiến trúc hệ thống](#2-kiến-trúc-hệ-thống)
- [3. Sơ đồ kết nối phần cứng (Pinout)](#3-sơ-đồ-kết-nối-phần-cứng-pinout)
- [4. Các tính năng cốt lõi](#4-các-tính-năng-cốt-lõi)
- [5. Hệ thống công cụ MCP (Model Context Protocol)](#5-hệ-thống-công-cụ-mcp-model-context-protocol)
- [6. Cấu trúc mã nguồn Firmware](#6-cấu-trúc-mã-nguồn-firmware)
- [7. Hướng dẫn cài đặt và nạp Firmware](#7-hướng-dẫn-cài-đặt-và-nạp-firmware)
- [8. Bản quyền & Tham khảo](#8-bản-quyền--tham-khảo)

---

## 1. Giới thiệu tổng quan

Dự án phát triển một thiết bị **Đèn bàn học thông minh** kết hợp trợ lý AI dành cho học sinh, sinh viên và người làm việc văn phòng. Thiết bị không chỉ dừng lại ở việc chiếu sáng mà còn đóng vai trò một người bạn đồng hành thông minh:
- **Tự động hóa chiếu sáng sinh học**: Điều chỉnh nhiệt độ màu (Warm/Cool) và độ sáng bảo vệ mắt dựa trên cảm biến ánh sáng xung quanh (BH1750).
- **Giám sát môi trường học tập**: Theo dõi nhiệt độ, độ ẩm (AHT20) và cảnh báo bụi mịn (GP2Y1010AU0F).
- **Cảm biến hiện diện Radar mmWave (LD2420/LD2410)**: Nhận biết người dùng ngồi vào bàn để tự động bật đèn, nhắc nhở nghỉ ngơi chống gù lưng, mỏi mắt.
- **Trợ lý giọng nói tương tác tự nhiên**: Đàm thoại 2 chiều qua micro stereo và loa I2S với độ trễ thấp, giao tiếp với các mô hình ngôn ngữ lớn (LLM: Qwen, DeepSeek...).
- **Giao thức điều khiển MCP (Model Context Protocol)**: Mô hình AI trên máy chủ có thể chủ động ra lệnh đọc cảm biến, bật/tắt đèn và thiết lập ngữ cảnh học tập.

---

## 2. Kiến trúc hệ thống

```
+---------------------------------------------------------------------------------+
|                                 SMART DESK LAMP                                 |
|                                                                                 |
|   +-------------------+    +--------------------+    +----------------------+   |
|   |   Hệ thống Voice  |    | Cảm biến môi trường|    |  Chiếu sáng & Ngoại vi|   |
|   | - 2x INMP441 (I2S)|    | - AHT20 (Temp/Hum) |    | - PWM Warm/Cool LED  |   |
|   | - MAX98357A (Amp) |    | - BH1750 (Lux)     |    | - 3x Nút nhấn vật lý |   |
|   | - ESP-SR (Wakeup) |    | - GP2Y10 (Bụi mịn) |    | - Màn hình ST7789    |   |
|   | - Opus Audio Codec|    | - Radar LD2420/10  |    |   LVGL Tiếng Việt    |   |
|   +---------+---------+    +---------+----------+    +----------+-----------+   |
|             |                        |                          |               |
|             +------------------------+--------------------------+               |
|                                      |                                          |
|                                      v                                          |
|                         [ ESP32-S3 Core System ]                                |
|                   - FreeRTOS Event Loop & FSM                                   |
|                   - Nhúng máy chủ MCP (JSON-RPC 2.0)                            |
|                   - Wi-Fi Station / WebSocket Protocol Client                   |
+--------------------------------------+------------------------------------------+
                                       |
                   Internet / Mạng nội bộ (WebSocket + TLS)
                                       |
                                       v
                     +-----------------------------------+
                     |           Backend Server          |
                     |   - FastAPI / SQLAlchemy async    |
                     |   - LLM Engine (DeepSeek / Qwen)  |
                     |   - Streaming ASR & TTS (Opus)    |
                     |   - MCP Tool Call Dispatcher      |
                     +-----------------------------------+
```

---

## 3. Sơ đồ kết nối phần cứng (Pinout)

Phần cứng được thiết kế tối ưu trên vi điều khiển **ESP32-S3 (WROOM-1 / DevKit)**:

| Phân hệ | Linh kiện | Chân GPIO ESP32-S3 | Chức năng / Ghi chú |
| :--- | :--- | :--- | :--- |
| **Microphone I2S** | 2x INMP441 | **GPIO 5** (BCLK)<br>**GPIO 4** (WS)<br>**GPIO 6** (SD) | Thu âm Stereo 16-bit 16kHz.<br>Mic 1: Chân L/R nối GND.<br>Mic 2: Chân L/R nối 3V3. |
| **Loa Audio DAC** | MAX98357A | **GPIO 15** (BCLK)<br>**GPIO 16** (LRC/WS)<br>**GPIO 17** (DIN)<br>**GPIO 18** (SD / Enable) | Phát âm thanh phản hồi 24kHz.<br>GPIO18 kéo mức cao để kích hoạt Amp. |
| **Màn hình LCD** | ST7789V 240x320 SPI | **GPIO 38** (SCLK)<br>**GPIO 39** (MOSI)<br>**GPIO 40** (RST)<br>**GPIO 19** (DC)<br>**GPIO 20** (CS) | Giao diện LVGL 9.x nằm ngang (320x240).<br>*(Lưu ý: GPIO19/20 dùng SPI nên nạp qua cổng UART).* |
| **Cảm biến I2C** | AHT20 + BH1750 | **GPIO 41** (SDA)<br>**GPIO 42** (SCL) | Bus I2C 100kHz chung:<br>- AHT20 (0x38): Nhiệt độ, Độ ẩm.<br>- BH1750 (0x23): Cường độ sáng (Lux). |
| **Cảm biến Bụi** | GP2Y1010AU0F | **GPIO 9** (LED Control)<br>**ADC1 CH0** (GPIO 1 / 2) | LED kích hoạt mức thấp (Active-Low). Đọc tín hiệu Analog chuyển đổi nồng độ bụi. |
| **Radar mmWave** | HLK-LD2420 / 10 | **GPIO 47** (TX)<br>**GPIO 48** (RX)<br>**GPIO 14** (Digital OUT) | UART 115200 baud đọc khoảng cách & năng lượng chuyển động. GPIO14 báo có người. |
| **Đèn LED Đôi** | 2 kênh Mosfet PWM | **GPIO 10** (Warm LED)<br>**GPIO 11** (Cool LED) | Điều khiển độ sáng độc lập bằng PWM (LEDC driver) từ 0 - 100%. |
| **Nút bấm vật lý** | 3x Push Buttons | **GPIO 13** (Up / Vol+)<br>**GPIO 12** (Down / Vol-)<br>**GPIO 8** (Toggle Chat / Config) | Nút nhấn kéo nội trở lên (Active-Low). Nhấn giữ GPIO8 khi khởi động để vào chế độ cấu hình Wi-Fi. |

---

## 4. Các tính năng cốt lõi

### 4.1. Màn hình Dashboard & Biểu cảm Robot (LVGL 9.x)
- **Hỗ trợ tiếng Việt đầy đủ**: Tích hợp trực tiếp 2 font chữ tiếng Việt có dấu tối ưu cho nhúng (`font_vi_13` và `font_vi_24`).
- **Giao diện đa tầng (Multi-layer)**:
  - *Tầng Dashboard*: Hiển thị đồng hồ số lớn, lịch ngày tháng, trạng thái kết nối Wi-Fi, nhiệt độ/độ ẩm phòng và nồng độ bụi.
  - *Tầng Biểu cảm Robot (Robot Eyes)*: Mắt robot động chớp mắt, thay đổi biểu cảm tương ứng với trạng thái trợ lý (`Lắng nghe`, `Suy nghĩ`, `Trả lời`).
  - *Tầng Tin nhắn hội thoại*: Khung chat bong bóng hiển thị văn bản người dùng nói và câu trả lời của trợ lý AI.

### 4.2. Xử lý âm thanh độ trễ thấp
- Tích hợp thư viện **ESP-SR** nhận diện từ khóa đánh thức offline.
- Mã hóa và giải mã luồng âm thanh nén chuẩn **Opus** thời gian thực, tiết kiệm băng thông và giảm tối đa độ trễ phản hồi.

---

## 5. Hệ thống công cụ MCP (Model Context Protocol)

Firmware tích hợp một **MCP Server** theo chuẩn JSON-RPC 2.0. Mô hình AI từ xa có thể tự động gọi các công cụ này:

| Tên công cụ (MCP Tool) | Tham số (Arguments) | Mô tả chức năng |
| :--- | :--- | :--- |
| `Lamp.set_power` | `{"state": bool}` | Bật (`true`) hoặc tắt (`false`) đèn bàn. |
| `Lamp.set_brightness` | `{"value": int}` | Đặt mức độ sáng tổng thể (từ `0` đến `100`). |
| `Lamp.set_color_temp` | `{"value": int}` | Chỉnh nhiệt độ màu từ sáng vàng ấm (0) đến sáng trắng mát (100). |
| `Lamp.get_state` | Không | Lấy trạng thái hiện tại của đèn (nguồn, độ sáng, nhiệt độ màu). |
| `self.environment.get_readings` | Không | Đọc dữ liệu nhiệt độ (°C), độ ẩm (%) từ AHT20 và cường độ ánh sáng (Lux) từ BH1750. |
| `self.environment.get_dust` | Không | Đọc nồng độ bụi mịn từ cảm biến quang học GP2Y10. |
| `self.environment.get_presence` | Không | Đọc trạng thái phát hiện người ngồi học và khoảng cách từ Radar mmWave. |

---

## 6. Cấu trúc mã nguồn Firmware

Firmware đã được tinh gọn tối ưu cho riêng dự án:

```
xiaozhi-esp32/
├── CMakeLists.txt                 # File cấu hình build ESP-IDF gốc
├── sdkconfig.defaults             # Các cấu hình mặc định cho ESP32-S3 (PSRAM, Flash)
├── main/
│   ├── main.cc                    # app_main khởi tạo NVS và chạy Application
│   ├── application.cc / .h        # Event loop trung tâm và điều phối State Machine
│   ├── device_state_machine.cc    # Quản lý trạng thái thiết bị (Idle, Listening, Speaking...)
│   ├── mcp_server.cc / .h         # Máy chủ MCP thực thi gọi hàm từ LLM
│   ├── audio/                     # Audio pipeline: I2S driver, VAD, ESP-SR, Opus streaming
│   ├── display/                   # Driver ST7789, Text Glyph và LVGL Theme
│   ├── protocols/                 # Giao thức WebSocket và định dạng gói tin nhị phân
│   ├── assets/                    # Quản lý tài nguyên âm báo và ngôn ngữ tiếng Việt (vi-VN)
│   └── boards/
│       ├── common/                # Board interface chuẩn, quản lý nguồn và Wi-Fi
│       └── my-custom-board/       # DRIVER PHẦN CỨNG RIÊNG CỦA ĐỒ ÁN
│           ├── config.h           # Định nghĩa chân GPIO, thông số màn hình & cảm biến
│           ├── my-custom-board.cc # Triển khai chi tiết logic bo mạch & giao diện LVGL
│           ├── my_board_audio_codec.cc # Audio codec I2S stereo
│           ├── font_vi_13.c / 24.c# Bộ font tiếng Việt hiển thị
│           ├── io/                # Điều khiển nút bấm và LED PWM
│           └── sensors/           # Driver cảm biến AHT20, BH1750, Bụi GP2Y10, Radar LD2420
```

---

## 7. Hướng dẫn cài đặt và nạp Firmware

### 7.1. Yêu cầu môi trường
- **ESP-IDF**: Khuyến nghị phiên bản **v6.0.2** (hoặc ESP-IDF v5.5+).
- **Python**: 3.10+
- **Công cụ nạp**: Cáp Type-C kết nối qua cổng UART (CP2102/CH340) của kit ESP32-S3.

### 7.2. Các bước biên dịch và nạp
1. Mở terminal môi trường ESP-IDF (ESP-IDF 6.0 CMD hoặc qua ESP-IDF Extension trong VS Code):
   ```bash
   cd D:\xiaozhi-esp32\xiaozhi-esp32
   ```
2. Thiết lập target vi điều khiển ESP32-S3:
   ```bash
   idf.py set-target esp32s3
   ```
3. Biên dịch dự án:
   ```bash
   idf.py build
   ```
4. Nạp firmware vào bo mạch (thay `COMx` bằng cổng COM nhận diện trên máy tính):
   ```bash
   idf.py -p COMx flash monitor
   ```

### 7.3. Cấu hình mạng Wi-Fi ban đầu
- Khi bật nguồn thiết bị lần đầu (hoặc nhấn giữ nút **Toggle - GPIO 8** trong lúc khởi động), màn hình sẽ hiển thị trạng thái phát Wi-Fi AP.
- Sử dụng điện thoại hoặc máy tính kết nối vào mạng Wi-Fi do thiết bị phát ra để cấu hình SSID/Mật khẩu Wi-Fi gia đình và địa chỉ Backend Server.

---

## 8. Bản quyền & Tham khảo

- **Giấy phép**: Toàn bộ dự án phát hành theo giấy phép mã nguồn mở **[MIT License](LICENSE)**.
- **Kế thừa & Tham khảo**:
  - Kiến trúc nền tảng và Audio/WebSocket pipeline kế thừa từ dự án mã nguồn mở [XiaoZhi ESP32](https://github.com/78/xiaozhi-esp32).
  - Giao thức điều khiển thiết bị: [Model Context Protocol (MCP)](https://modelcontextprotocol.io/).
  - Thư viện đồ họa nhúng: [LVGL 9.x](https://lvgl.io/).
