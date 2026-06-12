import time
import threading
import paho.mqtt.client as mqtt
import RPi.GPIO as GPIO
from gpiozero import LED
import dht11

# --- CẤU HÌNH PHẦN CỨNG ---
# Cấu hình chân GPIO tương ứng với D0-D4 trên Dashboard
PIN_MAPPING = {
    "D0": 17,  # GPIO 17 (Chân vật lý số 11)
    "D1": 27,  # GPIO 27 (Chân vật lý số 13)
    "D2": 22,  # GPIO 22 (Chân vật lý số 15)
    "D3": 5,   # GPIO 5  (Chân vật lý số 29)
    "D4": 6    # GPIO 6  (Chân vật lý số 31)
}

# Chân DATA của cảm biến DHT11 (Chân vật lý số 7)
DHT11_PIN = 4  # GPIO 4

# Khởi tạo chế độ chân GPIO cho RPi.GPIO (dùng cho cảm biến DHT11)
GPIO.setwarnings(False)
GPIO.setmode(GPIO.BCM)
dht_sensor = dht11.DHT11(pin=DHT11_PIN)

# Khởi tạo các chân GPIO làm LED/Relay đầu ra (dùng gpiozero)
leds = {}
for name, gpio_pin in PIN_MAPPING.items():
    try:
        leds[name] = LED(gpio_pin)
        print(f"Khoi tao Pin {name} -> GPIO {gpio_pin} thanh cong")
    except Exception as e:
        print(f"Loi khoi tao Pin {name} -> GPIO {gpio_pin}: {e}")

# --- CẤU HÌNH LED MATRIX MAX7219 ---
matrix_enabled = False
try:
    import spidev
    spi = spidev.SpiDev()
    spi.open(0, 0) # Bus 0, Device 0 (CE0 pin 24)
    spi.max_speed_hz = 10000000 # 10 MHz
    
    def write_reg(reg, val):
        spi.xfer2([reg, val])
        
    def init_matrix():
        write_reg(0x09, 0x00) # Decode mode: none
        write_reg(0x0A, 0x02) # Intensity (brightness: 2/15)
        write_reg(0x0B, 0x07) # Scan limit: 8 digits (all rows)
        write_reg(0x0C, 0x01) # Power on (normal mode)
        write_reg(0x0F, 0x00) # Display test: off
        # Clear screen
        for r in range(1, 9):
            write_reg(r, 0x00)
            
    init_matrix()
    matrix_enabled = True
    print("Khoi tao LED Matrix MAX7219 thanh cong!")
except Exception as e:
    print(f"Loi khoi tao LED Matrix MAX7219: {e}")

# --- CẤU HÌNH FONT CHỮ 8x8 CHO MATRIX ---
FONT = {
    '0': [0x3E, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3E, 0x00],
    '1': [0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00],
    '2': [0x3E, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00],
    '3': [0x3E, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3E, 0x00],
    '4': [0x0C, 0x1C, 0x2C, 0x4C, 0x7E, 0x0C, 0x0C, 0x00],
    '5': [0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3E, 0x00],
    '6': [0x3C, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x3C, 0x00],
    '7': [0x7E, 0x06, 0x0C, 0x18, 0x18, 0x30, 0x30, 0x00],
    '8': [0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00],
    '9': [0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00],
    'A': [0x18, 0x24, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00],
    'B': [0x7C, 0x42, 0x42, 0x7C, 0x42, 0x42, 0x7C, 0x00],
    'C': [0x3C, 0x42, 0x40, 0x40, 0x40, 0x42, 0x3C, 0x00],
    'D': [0x78, 0x44, 0x42, 0x42, 0x42, 0x44, 0x78, 0x00],
    'E': [0x7E, 0x40, 0x40, 0x7C, 0x40, 0x40, 0x7E, 0x00],
    'F': [0x7E, 0x40, 0x40, 0x7C, 0x40, 0x40, 0x40, 0x00],
    'G': [0x3C, 0x42, 0x40, 0x4E, 0x42, 0x42, 0x3E, 0x00],
    'H': [0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00],
    'I': [0x3E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x3E, 0x00],
    'J': [0x3E, 0x02, 0x02, 0x02, 0x02, 0x42, 0x3C, 0x00],
    'K': [0x42, 0x44, 0x48, 0x70, 0x48, 0x44, 0x42, 0x00],
    'L': [0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00],
    'M': [0x42, 0x66, 0x5A, 0x42, 0x42, 0x42, 0x42, 0x00],
    'N': [0x42, 0x62, 0x52, 0x4A, 0x46, 0x42, 0x42, 0x00],
    'O': [0x3C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00],
    'P': [0x7C, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x40, 0x00],
    'Q': [0x3C, 0x42, 0x42, 0x42, 0x4A, 0x44, 0x3A, 0x00],
    'R': [0x7C, 0x42, 0x42, 0x7C, 0x48, 0x44, 0x42, 0x00],
    'S': [0x3C, 0x42, 0x40, 0x3C, 0x02, 0x42, 0x3C, 0x00],
    'T': [0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00],
    'U': [0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00],
    'V': [0x42, 0x42, 0x42, 0x42, 0x42, 0x24, 0x18, 0x00],
    'W': [0x42, 0x42, 0x42, 0x42, 0x5A, 0x66, 0x42, 0x00],
    'X': [0x42, 0x42, 0x24, 0x18, 0x24, 0x42, 0x42, 0x00],
    'Y': [0x42, 0x42, 0x24, 0x18, 0x18, 0x18, 0x18, 0x00],
    'Z': [0x7E, 0x02, 0x04, 0x08, 0x18, 0x20, 0x7E, 0x00],
    '%': [0x62, 0x66, 0x0C, 0x18, 0x30, 0x66, 0x46, 0x00],
    '.': [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18],
    ':': [0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00],
    ' ': [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
    '*': [0x1C, 0x14, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00]
}

# Các biểu tượng trạng thái ON / OFF khi dieu khien thiet bi
PATTERN_ON = [
    0x00,
    0x02,  # . . . . . . 1 .
    0x06,  # . . . . . . 1 1
    0x0E,  # . . . . . 1 1 1
    0x9C,  # 1 . . 1 1 1 . .
    0xF8,  # 1 1 1 1 1 . . .
    0x70,  # . 1 1 1 . . . .
    0x20   # . . 1 . . . . .
]

PATTERN_OFF = [
    0x00,
    0x66,  # . 1 1 . . 1 1 .
    0x66,  # . 1 1 . . 1 1 .
    0x3C,  # . . 1 1 1 1 . .
    0x18,  # . . . 1 1 . . .
    0x3C,  # . . 1 1 1 1 . .
    0x66,  # . 1 1 . . 1 1 .
    0x66   # . 1 1 . . 1 1 .
]

# Các biến lưu trạng thái hiển thị
current_temp = None
current_hum = None
matrix_event_data = None
matrix_event_time = 0
scroll_delay_sec = 0.04

# Biến lưu trữ lịch hẹn giờ
schedules = []
last_triggered_min = ""

# Biến đếm ngược (Countdown Timer)
countdown_seconds = None
countdown_pin = None
countdown_lock = threading.Lock()


def trigger_matrix_event(pattern, duration_sec=1.5):
    global matrix_event_data, matrix_event_time
    if not matrix_enabled:
        return
    matrix_event_data = pattern
    matrix_event_time = time.time() + duration_sec

def countdown_thread_func():
    global countdown_seconds, countdown_pin
    print(f"Luong dem nguoc thiet bi bat dau: {countdown_pin} trong {countdown_seconds}s")
    while True:
        time.sleep(1)
        with countdown_lock:
            if countdown_seconds is None:
                print("Luong dem nguoc bi huy.")
                break
            
            countdown_seconds -= 1
            print(f"[Countdown] Con lai: {countdown_seconds}s")
            
            if countdown_seconds <= 0:
                print(f"[Countdown] Den 0! Kich hoat Bat Pin {countdown_pin}")
                if countdown_pin in leds:
                    leds[countdown_pin].on()
                    trigger_matrix_event(PATTERN_ON, duration_sec=2.0)
                countdown_seconds = None
                countdown_pin = None
                break

# --- CẤU HÌNH MQTT ---
MQTT_SERVER = "broker.hivemq.com"
MQTT_COMMAND_TOPIC = "duantuoicay/2modun/command"
MQTT_SENSOR_TOPIC = "duantuoicay/2modun/sensor"
BOARD_ID = 3  # ID của Raspberry Pi 3

# Hàm callback khi kết nối thành công tới MQTT Broker
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Ket noi MQTT Broker thanh cong!")
        client.subscribe(MQTT_COMMAND_TOPIC)
        print(f"Da dang ky subscribe topic nhan lenh: {MQTT_COMMAND_TOPIC}")
    else:
        print(f"Ket noi that bai, ma loi: {rc}")

# Hàm callback khi nhận được lệnh từ MQTT
def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode('utf-8')
        print(f"Nhan tin nhan lenh MQTT: {payload}")
        
        parts = payload.split(',')
        if len(parts) < 3:
            return
            
        board_id = int(parts[0])
        pin_name = parts[1].upper()
        action = parts[2].upper()
        
        # Nhận lệnh chỉnh tốc độ cuộn Matrix
        if board_id == 3 and pin_name == "MATRIX" and action == "SPEED":
            global scroll_delay_sec
            if len(parts) >= 4:
                speed = int(parts[3])
                scroll_delay_sec = speed / 1000.0
                print(f"-> Cap nhat toc do cuon matrix: {speed} ms")
            return
            
        # Nhận lệnh hẹn giờ chạy từ Web
        if board_id == 3 and pin_name == "SCHED":
            global schedules
            if action == "SET":
                if len(parts) >= 6:
                    target_pin = parts[3].upper()
                    target_action = parts[4].upper()
                    sched_time = parts[5]
                    # Thêm vào danh sách hẹn giờ
                    schedules.append({
                        "pin": target_pin,
                        "action": target_action,
                        "time": sched_time
                    })
                    print(f"-> Da dat lich hen gio: {target_pin} {target_action} luc {sched_time}")
                    trigger_matrix_event(PATTERN_ON)
            elif action == "CLEAR":
                schedules = []
                print("-> Da xoa sach lich hen gio")
                trigger_matrix_event(PATTERN_OFF)
            return

        # Nhận lệnh đếm ngược từ Web: 3,TIMER,START,pin,duration hoặc 3,TIMER,CANCEL,0
        if board_id == 3 and pin_name == "TIMER":
            global countdown_seconds, countdown_pin
            if action == "START":
                with countdown_lock:
                    countdown_pin = parts[3].upper()
                    countdown_seconds = int(parts[4])
                t = threading.Thread(target=countdown_thread_func, daemon=True)
                t.start()
            elif action == "CANCEL":
                with countdown_lock:
                    countdown_seconds = None
                    countdown_pin = None
                print("-> Da huy lenh dem nguoc")
                trigger_matrix_event(PATTERN_OFF)
            return

        if board_id != BOARD_ID and board_id != 0:
            return
            
        if pin_name not in leds:
            print(f"Khong tim thay pin: {pin_name}")
            return
            
        led = leds[pin_name]
        speed = int(parts[3]) if len(parts) >= 4 else 0
        
        if action == "ON":
            print(f"-> Bat Pin {pin_name} (GPIO {PIN_MAPPING[pin_name]})")
            led.on()
            trigger_matrix_event(PATTERN_ON)
        elif action == "OFF":
            print(f"-> Tat Pin {pin_name} (GPIO {PIN_MAPPING[pin_name]})")
            led.off()
            trigger_matrix_event(PATTERN_OFF)
        elif action == "BLINK":
            speed_sec = speed / 1000.0
            print(f"-> Chop tat Pin {pin_name} (GPIO {PIN_MAPPING[pin_name]}) chu ky {speed_sec}s")
            led.blink(on_time=speed_sec, off_time=speed_sec, background=True)
            trigger_matrix_event(PATTERN_ON)
        elif action == "STOPBLINK":
            print(f"-> Dung chop tat Pin {pin_name} (GPIO {PIN_MAPPING[pin_name]})")
            led.off()
            trigger_matrix_event(PATTERN_OFF)
            
    except Exception as e:
        print(f"Loi xu ly lenh: {e}")

# Hàm chạy ngầm đọc cảm biến DHT11 và gửi dữ liệu lên MQTT
def sensor_loop():
    global current_temp, current_hum
    print("Bat dau luong doc cam bien DHT11...")
    while True:
        result = dht_sensor.read()
        if result.is_valid():
            temp = result.temperature
            hum = result.humidity
            current_temp = temp
            current_hum = hum
            payload = f"{temp:.1f},{hum:.1f}"
            print(f"Cam bien DHT11: Nhiet do = {temp:.1f}C, Do am = {hum:.1f}% -> Dang publish...")
            try:
                # Gửi kèm retain=True để Client Web/ESP8266 khi kết nối sẽ nhận được ngay giá trị mới nhất
                client.publish(MQTT_SENSOR_TOPIC, payload, retain=True)
            except Exception as e:
                print(f"Loi publish du lieu cam bien: {e}")
        else:
            # Lỗi đọc DHT11 là rất phổ biến (sai checksum), bỏ qua và đọc lại ở chu kỳ sau
            pass
        
        time.sleep(5) # Đọc lại sau 5 giây

# Luồng chạy hiển thị cuộn chữ trên LED Matrix (Nhiệt độ/Độ ẩm)
def matrix_scroll_loop():
    if not matrix_enabled:
        return
    print("Bat dau luong hien thi LED Matrix...")
    while True:
        # Kiểm tra sự kiện hiển thị tạm thời (ON/OFF)
        if matrix_event_data is not None and time.time() < matrix_event_time:
            for r in range(8):
                write_reg(r + 1, matrix_event_data[r])
            time.sleep(0.1)
            continue
            
        # Kiểm tra hiển thị đếm ngược (Countdown)
        global countdown_seconds
        if countdown_seconds is not None:
            sec_str = str(countdown_seconds)
            if len(sec_str) == 1:
                # Single digit: display statically
                pattern = FONT.get(sec_str, FONT[' '])
                for r in range(8):
                    write_reg(r + 1, pattern[r])
                time.sleep(0.05)
            else:
                # Double/triple digits: scroll it
                text = f"  {sec_str}  "
                L = len(text)
                for offset in range((L - 1) * 8):
                    if countdown_seconds is None or len(str(countdown_seconds)) == 1:
                        break
                    if matrix_event_data is not None and time.time() < matrix_event_time:
                        break
                    char_idx = offset // 8
                    pixel_shift = offset % 8
                    for r in range(8):
                        char_curr = text[char_idx]
                        char_next = text[char_idx + 1] if char_idx + 1 < L else ' '
                        val_curr = FONT.get(char_curr, FONT[' '])[r]
                        val_next = FONT.get(char_next, FONT[' '])[r]
                        byte_to_show = ((val_curr << pixel_shift) | (val_next >> (8 - pixel_shift))) & 0xFF
                        write_reg(r + 1, byte_to_show)
                    time.sleep(0.05)
            continue
            
        # Hiển thị cuộn Nhiệt độ & Độ ẩm
        if current_temp is not None and current_hum is not None:
            text = f"    T:{current_temp:.1f}*C  H:{current_hum:.1f}%    "
            L = len(text)
            
            # Cuộn từng cột điểm ảnh (pixel)
            for offset in range((L - 1) * 8):
                # Nếu có đếm ngược, ngắt cuộn chữ lập tức
                if countdown_seconds is not None:
                    break
                # Nếu có sự kiện đột xuất, ngắt cuộn chữ lập tức
                if matrix_event_data is not None and time.time() < matrix_event_time:
                    break
                    
                char_idx = offset // 8
                pixel_shift = offset % 8
                
                for r in range(8):
                    char_curr = text[char_idx].upper()
                    char_next = text[char_idx + 1].upper() if char_idx + 1 < L else ' '
                    
                    val_curr = FONT.get(char_curr, FONT[' '])[r]
                    val_next = FONT.get(char_next, FONT[' '])[r]
                    
                    byte_to_show = ((val_curr << pixel_shift) | (val_next >> (8 - pixel_shift))) & 0xFF
                    write_reg(r + 1, byte_to_show)
                    
                time.sleep(scroll_delay_sec)
        else:
            # Nháy 2 dấu chấm nhỏ ở đáy để báo đang khởi động
            for r in range(1, 9):
                write_reg(r, 0x00)
            write_reg(8, 0x18)
            time.sleep(0.5)
            write_reg(8, 0x00)
            time.sleep(0.5)

# Luồng chạy kiểm tra lịch hẹn giờ (Scheduler)
def scheduler_loop():
    global schedules, last_triggered_min
    print("Bat dau luong hen gio (Scheduler)...")
    while True:
        current_hm = time.strftime("%H:%M") # Lấy giờ hiện tại "HH:MM"
        
        # Chỉ kích hoạt 1 lần duy nhất trong phút đó
        if current_hm != last_triggered_min:
            triggered = []
            for sched in schedules:
                if sched["time"] == current_hm:
                    pin_name = sched["pin"]
                    action = sched["action"]
                    if pin_name in leds:
                        led = leds[pin_name]
                        if action == "ON":
                            print(f"[Scheduler] Kich hoat Bat Pin {pin_name}")
                            led.on()
                            trigger_matrix_event(PATTERN_ON)
                        elif action == "OFF":
                            print(f"[Scheduler] Kich hoat Tat Pin {pin_name}")
                            led.off()
                            trigger_matrix_event(PATTERN_OFF)
                    triggered.append(sched)
            
            # Xóa các lịch hẹn giờ đã kích hoạt
            if triggered:
                last_triggered_min = current_hm
                for sched in triggered:
                    schedules.remove(sched)
                    
        time.sleep(5) # Kiểm tra mỗi 5 giây

# Khởi tạo MQTT Client
try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
except AttributeError:
    client = mqtt.Client()

client.on_connect = on_connect
client.on_message = on_message

print("Dang ket noi den MQTT Broker...")
client.connect(MQTT_SERVER, 1883, 60)

# Khởi chạy luồng đọc cảm biến độc lập
sensor_thread = threading.Thread(target=sensor_loop, daemon=True)
sensor_thread.start()

# Khởi chạy luồng hiển thị LED Matrix độc lập
matrix_thread = threading.Thread(target=matrix_scroll_loop, daemon=True)
matrix_thread.start()

# Khởi chạy luồng Scheduler hẹn giờ độc lập
scheduler_thread = threading.Thread(target=scheduler_loop, daemon=True)
scheduler_thread.start()

# Vòng lặp lắng nghe tin nhắn MQTT liên tục
try:
    client.loop_forever()
except KeyboardInterrupt:
    print("\nDang tat chuong trinh...")
    for led in leds.values():
        led.close()
    GPIO.cleanup()
    if matrix_enabled:
        for r in range(1, 9):
            write_reg(r, 0x00)
        spi.close()
    print("Da dung!")
