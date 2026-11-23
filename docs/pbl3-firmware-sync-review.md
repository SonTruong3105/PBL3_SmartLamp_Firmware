# Review đồng bộ Firmware – Backend – Frontend PBL3

| | |
|---|---|
| **Ngày cập nhật** | 17/11/2026 |
| **Firmware** | `D:\xiaozhi-esp32\xiaozhi-esp32` — HEAD `d6f6b64`, board `my-custom-board`, ESP32-S3, ESP-IDF v6.0.2 |
| **Backend chuẩn** | `D:\Xiaozhi_server` — FastAPI / SQLAlchemy async |
| **Phương pháp** | Đọc code thực tế (không tin tài liệu cũ), đối chiếu contract, map đặc tả phần cứng |
| **Thay đổi code** | **Không.** Tài liệu này chỉ review + kế hoạch. |

> ⚠️ Git state: firmware có 21 thay đổi chưa commit (2 untracked: file này + board), backend có 190.
> Đây là việc của người dùng — **phải bảo toàn, không commit, không ghi đè, không format mass**.

---

## 1. Kết luận tổng quan

Contract firmware ↔ backend **đã khớp khoảng 85%**: OTA dạng flat, identity lấy từ header, echo version, framing binary v1/v2/v3, audio 16 kHz, tool `value` 0..100, `nextCursor` pagination, `light_lux→lux`, tên event — tất cả đã đúng.

Hai "blocker" trong `D:\Xiaozhi_server\HANDOFF.md` / `REVIEW_STRUCTURE.md` là **stale**:
- `pipeline_v3_patch.py` **không còn tồn tại** trong repo (docs nói import nhưng không gọi `install()`).
- `connection_registry.register()` thật sự không ai gọi, nhưng registration chạy trực tiếp qua `sync.register_esp32` (`esp32_ws.py:245-248`) → tính năng vẫn hoạt động, chỉ là wrapper chết.

Các nhóm việc còn lại, xếp theo mức độ:

| Nhóm | Vấn đề | Mức |
|---|---|---|
| Kiến trúc | Chưa có kênh điều khiển **always-on** riêng; WS chỉ mở khi có audio | 🔴 P0 |
| Backend | `str(result)` làm hỏng parse MCP poll; sensor cache ghi nhầm scope | 🔴 P0 |
| Firmware | Null-deref khi parse hello thiếu `transport` | 🔴 P0 |
| Hardware | Radar code là **LD2420**, đặc tả là **LD2410**; chân ADC bụi lệch docs | 🔴 P0 |
| Firmware | Sensor POST bỏ status, token sai khi MQTT, chạy cả lúc OTA | 🟠 P0/P1 |
| Code | Log mic raw, 2 bộ tool đèn, MCP lỗi thiếu `code`, README stale | 🟠 P1 |
| Dọn dẹp | Config chết, tool layer 5 implementation, docs stale | 🟡 P2 |

---

## 2. Cấu trúc firmware

| Module | Trách nhiệm |
|---|---|
| `main/application.cc` | Event loop, state machine, vòng đời protocol, dispatch JSON inbound, heartbeat |
| `main/device_state_machine.*` | Chuyển trạng thái hợp lệ |
| `main/protocols/protocol.{h,cc}` | Contract chung: `AudioStreamPacket`, binary v2/v3, wrapper MCP, heartbeat |
| `main/protocols/websocket_protocol.cc` | Hello WS, header auth, framing v1/v2/v3, chờ hello 10 s |
| `main/protocols/mqtt_protocol.cc` | MQTT control + UDP/AES-CTR audio, hello `version:3` |
| `main/mcp_server.cc` | JSON-RPC 2.0: `initialize`, `tools/list` pagination, `tools/call` |
| `main/boards/common/` | Interface `Board`, OTA, system-info |
| `main/boards/my-custom-board/` | Board: UI LVGL, cảm biến, LED, codec I2S, tool `Lamp.*`, sensor telemetry |
| `main/audio/` | Codec, AFE/VAD, Opus, queue |
| `main/Kconfig.projbuild`, `main/CMakeLists.txt`, `config.json`, `scripts/build.py` | Chuỗi chọn board |

**Chuỗi build:** `config.json → build.py → Kconfig.projbuild → CMakeLists.txt → board source + config.h`.
Đúng 1 `DECLARE_BOARD(MyCustomBoard)` tại `my-custom-board.cc:865`. Không đổi pin board cũ (ảnh hưởng OTA).

### Điểm mạnh
- Core chỉ phụ thuộc interface `Board`, không import board cụ thể.
- State đổi qua `SetDeviceState()`, callback schedule qua `Application::Schedule()`.
- Framing binary khớp backend; tool schema dùng chung 2 transport.

### Vấn đề firmware
- 🔴 `websocket_protocol.cc:226-227` deref `transport->valuestring` khi `transport == nullptr` → crash. Bản MQTT làm đúng với `cJSON_IsString` (`mqtt_protocol.cc:420`).
- 🔴 `websocket_protocol.cc:224-229`: hello thiếu `transport` → không set event bit → chờ timeout 10 s thay vì báo lỗi ngay.
- 🟠 `my_board_audio_codec.cc:48-55`: log `MIC RAW` 1 lần/giây vẫn bật, comment ghi "Bỏ đoạn này sau khi test".
- 🟠 2 bộ tool đèn song song: `self.lights.set_warm_cool`/`get_state` (`io/led_control.cc:67,78`, đăng ký tại `my-custom-board.cc:826`) và `Lamp.*` (`my-custom-board.cc:713-776`). Comment ở `mcp_server.cc:44-45` mới chặn `self.lamp.*`, không chặn `self.lights.*`.
- 🟠 Sensor POST `PublishSensorTelemetry()` (`my-custom-board.cc:600-678`): bỏ status/body HTTP (`:643-648`); URL bỏ path `/xiaozhi/ota/` rồi nối `/api/sensor/report` (`:613-619`); token chỉ lấy `Settings("websocket")` (`:636-640` → sai khi MQTT); state guard không loại `kDeviceStateUpgrading` (`:604-608`).
- 🟠 MCP lỗi chỉ có `message`, thiếu `code` (`mcp_server.cc:444-451`); arg lạ bị bỏ qua im lặng (`:521-544`); structured result bị escape thành chuỗi trong `content[0].text` (`mcp_server.h:294-299`).
- 🟡 Identity lệch: `config.json:2,6` = `my-custom-boards` (số nhiều), thư mục/Kconfig = `my-custom-board` / `MY_CUSTOM_BOARD`. OTA body báo tên số nhiều → cân nhắc cẩn thận vì ảnh hưởng OTA compatibility.
- 🟡 `self.upgrade_firmware` dùng constructor 3 đối số nên description thành default, `url` không required (`mcp_server.cc:155`).
- 🟡 `channels:1` trong hello nhưng `input_channels_=2` (`websocket_protocol.cc:214`, `my_board_audio_codec.cc:18`).
- 🟡 v3 force `timestamp=0` (`websocket_protocol.cc:131`) + câu vô nghĩa `bp3->type = bp3->type;` (`:125`); backend không được dùng timestamp v3 cho AEC.
- 🟡 Rò rỉ cJSON ở handler `custom` (`application.cc:651,654`).
- 🟡 `config.json` xin `CONFIG_USE_WECHAT_MESSAGE_STYLE=y` nhưng `sdkconfig:1130` đang tắt; OTA URL trỏ `chat.tbs.io.vn` thay vì default Kconfig.

---

## 3. Cấu trúc backend

| Lớp | Module | Trách nhiệm |
|---|---|---|
| App/lifespan | `app/main.py` | FastAPI, DB, runtime, warmup, MQTT, background tasks, `/health` |
| ESP32 WS | `app/routes/esp32_ws.py` | `/xiaozhi/v1/`: handshake, auth, unwrap binary, VAD, MCP relay, broadcast |
| OTA | `app/core/api/ota_handler.py` | `/xiaozhi/ota` — JSON config cho firmware |
| Transport | `app/core/protocols/{base,binary}.py` | `WebSocketTransport`/`MqttTransport`, framing v1/v2/v3, timeout 10 s |
| Pipeline | `app/core/connection.py`, `esp32_connection.py` | LLM→TTS, queue/rate control, alias `transport` |
| Device MCP | `app/core/providers/tools/device_mcp_manager.py` | Discover `initialize`/`tools/list`, đăng ký tool động, `tools/call` |
| Dashboard sync | `device_sync_ws.py`, `device_sync_hub.py`, `lamp_state.py`, `sensor_state.py` | `/ws/sync/{id}`, `/api/sensor/report`, REST command |
| Registry | `core/sync.py`, `broadcast_hub.py`, `connection_registry.py` | 4 nơi lưu device/handler chồng chéo |
| Frontend | `templates/index.html` (canonical), `static/js/chat.js`, `dashboard-theme.js` | Chat WS + `/ws/sync/`; `lamp_sync.js` đã retired |

Luồng dữ liệu:

```
ESP32 --WS /xiaozhi/v1/--> esp32_ws.py --> ESP32ConnectionHandler --> connection.py
                              |--> DeviceMCPManager (discover + call tool)
                              |--> binary.py + protocols/base.py
OTA   --> core/api/ota_handler.py (JSON flat)
Web   --> chat_ws.py, device_sync_ws.py --> device_sync_hub / lamp_state / sensor_state
MQTT  --> core/mqtt_server.py (UDP + AES)
```

---

## 4. Đối chiếu đặc tả phần cứng

| Thành phần | Code | Bằng chứng | Kết luận |
|---|---|---|---|
| AHT20 | ✅ | `sensors/i2c_sensors.cc:10,25-41,91-119` — I2C `0x38`, 100 kHz | Đúng |
| BH1750 | ✅ | `sensors/i2c_sensors.cc:11,43-59,79-87` — `0x23`, mode `0x10` | Thiếu BH1750 vẫn đọc AHT20 |
| GP2Y1010AU0F | ⚠️ | `sensors/dust_sensor.cc:11-14,61-94`; `pm25 = 0.17*mV − 0.1` (`my-custom-board.cc:583`) | `config.h:36-37` = `ADC_CHANNEL_0` (GPIO1), README ghi GPIO2/CH1 → **đo lại hardware** |
| LD2410 radar | ⚠️ **sai model** | `sensors/radar_sensor.cc:14-16`: header `F4 F3 F2 F1` / footer `F8 F7 F6 F5` = **LD2420** | LD2410 dùng `AA AA AA AA`. Đổi parser hoặc đổi cảm biến |
| 2 mic I2S | ✅ | `my_board_audio_codec.cc:11-19` — `I2S_STD_SLOT_BOTH`, `input_channels_=2`, BCLK5/WS4/SD6, shift 16 | Log debug chưa gỡ |
| 2 loa + MAX98357A | ⚠️ 1 amp | `config.h:15-18`, `my_board_audio_codec.cc:14` — một MAX98357A, slot LEFT | "2 loa" = 2 loa song song vào 1 amp; output 24 kHz, input 16 kHz |
| TFT 2.0" | ✅ | `config.h:23-34` ST7789 320×240 SPI 38/39/40/19/20; render `my-custom-board.cc:325,398,596` | README tool names stale (xem mục 7) |
| 2 dãy LED 3000K/6000K | ✅ | `config.h:43-44` GPIO10/11; `led_control.cc:6-10,24-63` LEDC 5 kHz; `my-custom-board.cc:469` `kelvin = 3000 + 3000*cool/(warm+cool)` | Đúng dải 3000–6000K |

---

## 5. Contract firmware ↔ backend — đã khớp

| Hạng mục | Firmware | Backend |
|---|---|---|
| Identity | Header `Device-Id` = MAC, không gửi `device_id` JSON | `esp32_ws.py:178,215-221` — `device_id` JSON chỉ optional-check |
| WS version | default 1, override từ settings | Echo `version` (`esp32_ws.py:223-226`), OTA `version:1` (`ota_handler.py:65`) |
| Binary framing | v2 `>HHIII` 16B, v3 `>BBH` 4B (`protocol.h:17-31`) | `binary.py:16-37` y hệt |
| Sample rate | Hello 16000, đọc lại từ server (`websocket_protocol.cc:213,239-246`) | `config.yaml:197` = 16000; `server_sample_rate=24000` **không ai dùng** |
| Tool lamp | `Lamp.*` arg `value` 0..100 (`my-custom-board.cc:733-773`) | `builtin.py:469,474`, `plugins.py:446,461` dùng `value` |
| CCT | 0=ấm…100=lạnh | Kelvin snap 3000/4500/6000 → 0/50/100 |
| Sensor key | HTTP `light_lux`, tool `lux` | `device_sync_ws.py:28-30` normalize `light_lux→lux` |
| OTA shape | Top-level `firmware/mqtt/websocket/server_time` (`ota.cc:147-238`) | Flat, không wrapper `server` (`ota_handler.py:58-76`) |
| Pagination | `nextCursor` (`mcp_server.cc:495-503`) | Loop `nextCursor` (`device_mcp_manager.py:135-145`) |
| Event names | — | `lamp_sync`, `lamp_update`, `sensor_update`, `full_state`, `device_status` |
| Heartbeat | Chỉ MQTT (`application.cc:271`; base WS trả `false` ở `protocol.cc:100`) | Chỉ `mqtt_server.py:265` xử lý |

> 📌 Bản review cũ nói backend phải đổi `brightness→value`, `kelvin→value`, sample rate 24 kHz,
> `device_id` trong hello — **đều đã lỗi thời**. Không đổi tên tool ở một phía.

---

## 6. Chức năng server theo yêu cầu

| Chức năng | Trạng thái | Bằng chứng |
|---|---|---|
| Điều khiển đèn bàn | ✅ | `Lamp.*`, allowlist `device_sync_ws.py:17-20` |
| Hỏi đáp AI cơ bản | ✅ | Pipeline `core/connection.py` VAD→ASR→LLM→TTS |
| Nhiệt độ/độ ẩm/chất lượng không khí | ✅ | `device_sync_ws.py:23-30`, `sensor_state.py`, UI `index.html:646-675`; firmware POST 5 s |
| Thiết lập & báo lịch trình | ✅ | `repositories/calendar.py`, `reminder_engine.py`, `scheduler.py`, `calendar.py:22` |
| Phát nhạc thư viện | ✅ nhưng phụ thuộc mạng | `music.py:57,85,92` — YouTube search → Ogg → queue sau TTS |

Nếu muốn "thư viện nhạc" nội bộ thay vì YouTube, cần đổi implementation `play_music`.

---

## 7. Kiến trúc 2 luồng — khoảng cách lớn nhất

**Yêu cầu:** một luồng âm thanh + một luồng điều khiển riêng; luồng âm thanh cũng điều khiển được đèn.

| Luồng | Đường đi | Đánh giá |
|---|---|---|
| Âm thanh | `WS /xiaozhi/v1/` binary Opus v1/v2/v3, 16 kHz/60 ms | ✅ |
| Điều khiển in-band | Cùng WS, `{"type":"mcp",...}` → `esp32_ws.py:309-349` | ✅ "audio điều khiển đèn" đã chạy trong hội thoại |
| Device → server | HTTP POST `/api/sensor/report` mỗi 5 s (`my-custom-board.cc:600-678`) | ✅ nhưng **chỉ sensor**, chưa có kênh lệnh tổng quát |
| Server → device | REST `/api/devices/{id}/command` → WS nếu có session, else MQTT (`builtin.py:428-438`) | ⚠️ **WS chỉ tồn tại khi audio mở** — `WebsocketProtocol::Start()` là no-op, `websocket_protocol.cc:19-22` *"Only connect to server when audio channel is needed"*; khi idle `SendText` rớt (`:56-59`) |
| UI | `WS /ws/sync/{device_id}` (`device_sync_ws.py:169`) | Chỉ cho web, **không phải kênh điều khiển thiết bị** |

**Kết luận:** yêu cầu "2 luồng riêng biệt" **chưa đáp ứng**: thiếu kênh điều khiển always-on từ server xuống device khi không có audio. MQTT có control channel, WS thì không.

---

## 8. Kế hoạch sửa

### Giai đoạn 0 — Chốt trước khi đụng code
- Backup/ghi nhận worktree (190 + 21 thay đổi). Không commit, không format mass.
- Chốt 4 quyết định: (a) LD2410 hay LD2420; (b) chân ADC bụi; (c) giữ bộ tool đèn nào (`Lamp.*` hay `self.lights.*`); (d) mono hay stereo ở hello.
- Cập nhật `HANDOFF.md`, `REVIEW_STRUCTURE.md`, README board (đang stale: gọi `self.environment.get_readings`/`get_dust`/`get_presence` trong khi code thật là `self.get_environment` + `Sensor.Read`; cũng ghi radar LD2420 trong khi đặc tả là LD2410).

### Giai đoạn 1 — P0
1. **Kiến trúc 2 luồng**: thêm kênh điều khiển always-on cho device (VD `WS /ws/control/{device_id}` hoặc dùng control channel MQTT), firmware mở lúc boot không đợi `OpenAudioChannel`. `_send_mcp_command` ưu tiên kênh này, WS audio làm fallback. Giữ in-band MCP trên audio.
2. **Radar**: thống nhất LD2410 (`radar_sensor.cc:15-16` + parser) hoặc đổi đặc tả/README.
3. **ADC bụi**: đo thực tế rồi thống nhất `config.h` ↔ README.
4. Backend `device_mcp_manager.py:183`: trả `result` dạng dict/JSON thật, bỏ `str()`, check `reply["error"]`.
5. Backend `esp32_ws.py:140`: `sensor_state.update_bulk(parsed, device_id=device_id)`.
6. Firmware `websocket_protocol.cc:226-227`: dùng `cJSON_IsString`; thiếu `transport` → lỗi ngay.
7. Firmware sensor POST: log status, fallback token theo transport, loại `kDeviceStateUpgrading`.

### Giai đoạn 2 — P1
8. Gỡ log MIC RAW (`my_board_audio_codec.cc:48-55`), bỏ `bp3->type = bp3->type;`, sửa rò rỉ cJSON.
9. Gộp 2 bộ tool đèn về 1 chuẩn.
10. MCP error thêm `code`; `await abort()` trong MQTT (`mqtt_server.py:592,744`); identity plural/singular.
11. `esp32_ws.py:216`: luôn log mismatch dù auth tắt; sửa `self.upgrade_firmware` arg `url` (`mcp_server.cc:155`).

### Giai đoạn 3 — P2
12. Xóa `server_sample_rate` (24000) hoặc dùng có chủ đích; xóa stub `lamp_sync.js`; gộp `connection_registry` vào `sync`; gộp 5 implementation tool layer.
13. Dọn comment patch, cập nhật docs với line number thực tế; cân nhắc `play_music` local nếu không muốn YouTube.

### Validation
- Firmware: `python3 scripts/build.py my-custom-board --name my-custom-boards` + `python3 -m unittest discover -s scripts/tests -v` (source ESP-IDF v6.0.2).
- Backend: `.\venv\Scripts\python.exe -m pytest tests -q` — baseline **54 passed**.
- Contract test: hello có header không có `device_id` JSON, binary v1/v2/v3, `tools/list` có `nextCursor`, `value=0/50/100`, sensor report HTTP, reconnect đổi device.
- **Hardware bắt buộc**: đo ADC bụi, xác nhận model radar, test 2 mic + amp, CCT 3000–6000 K, và **điều khiển đèn lúc idle (không có audio)** — nghiệm thu yêu cầu 2 luồng. Build thành công không thay thế test hardware.

---

## 9. Tiêu chí hoàn thành

- Có kênh điều khiển riêng, luôn sẵn sàng khi thiết bị idle; audio vẫn điều khiển được đèn.
- Radar/ADC đúng phần cứng thực tế; README khớp tool schema thật.
- Backend parse đúng MCP result mọi đường (in-band, poll, REST); sensor hiển thị đúng từng device.
- Không còn log debug, tool trùng tên, hay config chết gây nhiễu.
- Test được cả WebSocket, MQTT/UDP, OTA, MCP và các trường hợp mất kết nối trên thiết bị thật.
