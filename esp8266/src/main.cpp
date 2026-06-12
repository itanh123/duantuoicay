#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 2 // Most ESP8266 boards have the built-in LED on GPIO2
#endif

bool apMode = false;
String ssid = "";
String password = "";

ESP8266WebServer server(80);

// Unique ID for this board (ESP8266 Receiver/Master)
const int MY_BOARD_ID = 1;

// MQTT Broker Configuration
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic = "duantuoicay/2modun/command";
const char* mqtt_sensor_topic = "duantuoicay/2modun/sensor";

WiFiClient espClient;
PubSubClient client(espClient);

// Structure to track the configuration of each pin independently
struct PinControl {
  int gpio;
  const char* name;
  bool activeLow;
  bool manualState;
  bool blinkEnabled;
  int blinkDelay;
  unsigned long lastBlinkTime;
  bool currentToggle;
};

PinControl pins[5] = {
  {D0, "D0", false, false, false, 125, 0, false},
  {D1, "D1", false, false, false, 125, 0, false},
  {D2, "D2", false, false, false, 125, 0, false},
  {D3, "D3", false, false, false, 125, 0, false},
  {D4, "D4", true,  false, false, 125, 0, false} // D4/onboard LED is active-low
};

void updatePin(int index) {
  if (pins[index].blinkEnabled) {
    // Handled by loop() non-blocking timer
  } else {
    // Write manual state
    digitalWrite(pins[index].gpio, pins[index].activeLow ? (pins[index].manualState ? LOW : HIGH) : (pins[index].manualState ? HIGH : LOW));
  }
}

// HTML Wi-Fi Config Portal (AP Mode)
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP8266 Wi-Fi Config Portal</title>
  <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg: #090d16;
      --panel: rgba(17, 25, 45, 0.65);
      --border: rgba(255, 255, 255, 0.08);
      --text: #f3f4f6;
      --text-muted: #9ca3af;
      --primary: #3b82f6;
      --primary-glow: rgba(59, 130, 246, 0.45);
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Outfit', sans-serif; }
    body {
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 20px;
      position: relative;
      overflow: hidden;
    }
    body::before {
      content: ''; position: absolute; width: 300px; height: 300px;
      background: var(--primary-glow); filter: blur(120px); top: 10%; left: 10%; z-index: -1; border-radius: 50%;
    }
    body::after {
      content: ''; position: absolute; width: 300px; height: 300px;
      background: rgba(16, 185, 129, 0.18); filter: blur(120px); bottom: 10%; right: 10%; z-index: -1; border-radius: 50%;
    }
    .card {
      width: 100%;
      max-width: 400px;
      background: var(--panel);
      backdrop-filter: blur(25px);
      border: 1px solid var(--border);
      border-radius: 24px;
      padding: 30px;
      box-shadow: 0 24px 50px rgba(0, 0, 0, 0.45);
      text-align: center;
    }
    h1 {
      font-size: 1.8rem; font-weight: 800; margin-bottom: 10px;
      background: linear-gradient(135deg, #ffffff 40%, var(--primary) 100%);
      -webkit-background-clip: text; -webkit-text-fill-color: transparent;
    }
    p { color: var(--text-muted); font-size: 0.9rem; margin-bottom: 25px; }
    .input-group {
      text-align: left; margin-bottom: 20px;
    }
    label {
      display: block; font-size: 0.8rem; color: var(--text-muted);
      text-transform: uppercase; letter-spacing: 0.05em; margin-bottom: 8px;
    }
    select, input {
      width: 100%; background: rgba(255, 255, 255, 0.04);
      border: 1px solid var(--border); border-radius: 12px;
      color: #fff; padding: 12px 16px; font-size: 0.95rem; outline: none;
      transition: border-color 0.3s;
    }
    select option {
      background: #090d16; color: #fff;
    }
    select:focus, input:focus { border-color: var(--primary); }
    .btn {
      width: 100%; background: var(--primary); border: none; color: #fff;
      padding: 14px; border-radius: 12px; font-size: 1rem; font-weight: 600;
      cursor: pointer; transition: all 0.3s; margin-top: 10px;
      box-shadow: 0 4px 12px var(--primary-glow);
    }
    .btn:hover { transform: translateY(-2px); box-shadow: 0 6px 18px var(--primary-glow); }
  </style>
</head>
<body>
  <div class="card">
    <h1>Wi-Fi Config Portal</h1>
    <p>Select your home Wi-Fi network and enter the password to connect the ESP8266.</p>
    <form action="/savewifi" method="GET">
      <div class="input-group">
        <label for="ssid">Network Name (SSID)</label>
        <select id="ssid" name="ssid">
          <!-- OPTIONS_PLACEHOLDER -->
        </select>
      </div>
      <div class="input-group" style="margin-top: 25px;">
        <label for="password">Password</label>
        <input type="password" id="password" name="password" placeholder="Enter Wi-Fi password" required>
      </div>
      <button type="submit" class="btn">Save & Connect</button>
    </form>
  </div>
</body>
</html>
)rawliteral";

// HTML Dashboard (stored in flash memory)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP8266 Master IoT Control Center</title>
  <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg: #090d16;
      --panel: rgba(17, 25, 45, 0.65);
      --border: rgba(255, 255, 255, 0.08);
      --text: #f3f4f6;
      --text-muted: #9ca3af;
      --primary: #3b82f6;
      --primary-glow: rgba(59, 130, 246, 0.45);
      --accent-neon: #10b981;
      --accent-neon-glow: rgba(16, 185, 129, 0.45);
    }
    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      font-family: 'Outfit', sans-serif;
    }
    body {
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 20px;
      overflow-x: hidden;
      position: relative;
    }
    body::before {
      content: '';
      position: absolute;
      width: 350px;
      height: 350px;
      background: var(--primary-glow);
      filter: blur(140px);
      top: 10%;
      left: 10%;
      z-index: -1;
      border-radius: 50%;
    }
    body::after {
      content: '';
      position: absolute;
      width: 300px;
      height: 300px;
      background: rgba(16, 185, 129, 0.18);
      filter: blur(120px);
      bottom: 15%;
      right: 10%;
      z-index: -1;
      border-radius: 50%;
    }
    .container {
      width: 100%;
      max-width: 460px;
      background: var(--panel);
      backdrop-filter: blur(25px);
      border: 1px solid var(--border);
      border-radius: 28px;
      padding: 30px;
      box-shadow: 0 24px 50px rgba(0, 0, 0, 0.45);
      margin-top: 20px;
    }
    h1 {
      font-size: 2rem;
      font-weight: 800;
      text-align: center;
      background: linear-gradient(135deg, #ffffff 40%, var(--primary) 100%);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      margin-bottom: 5px;
    }
    .subtitle {
      font-size: 0.95rem;
      color: var(--text-muted);
      text-align: center;
      margin-bottom: 25px;
    }
    .board-selector {
      display: flex;
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid var(--border);
      border-radius: 14px;
      padding: 5px;
      margin-bottom: 25px;
      gap: 5px;
    }
    .board-btn {
      flex: 1;
      background: transparent;
      border: none;
      color: var(--text-muted);
      padding: 12px;
      border-radius: 10px;
      font-size: 0.95rem;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
    }
    .board-btn:hover {
      color: #fff;
    }
    .board-btn.active {
      background: var(--primary);
      color: #fff;
      box-shadow: 0 4px 12px var(--primary-glow);
    }
    .card {
      background: rgba(255, 255, 255, 0.025);
      border: 1px solid rgba(255, 255, 255, 0.04);
      border-radius: 18px;
      padding: 20px;
      margin-bottom: 18px;
      display: flex;
      flex-direction: column;
      gap: 15px;
      transition: transform 0.2s, border-color 0.2s;
    }
    .card:hover {
      border-color: rgba(255, 255, 255, 0.08);
      transform: translateY(-2px);
    }
    .card-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .pin-info {
      display: flex;
      flex-direction: column;
    }
    .pin-title {
      font-size: 1.1rem;
      font-weight: 600;
      color: #fff;
    }
    .pin-subtitle {
      font-size: 0.8rem;
      color: var(--text-muted);
      margin-top: 2px;
    }
    .controls-wrapper {
      display: flex;
      gap: 20px;
    }
    .control-item {
      text-align: center;
    }
    .control-label {
      font-size: 0.75rem;
      color: var(--text-muted);
      margin-bottom: 6px;
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }
    .switch {
      position: relative;
      display: inline-block;
      width: 52px;
      height: 28px;
    }
    .switch input {
      opacity: 0;
      width: 0;
      height: 0;
    }
    .slider {
      position: absolute;
      cursor: pointer;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      background-color: #242c3d;
      transition: .3s;
      border-radius: 30px;
    }
    .slider:before {
      position: absolute;
      content: "";
      height: 20px;
      width: 20px;
      left: 4px;
      bottom: 4px;
      background-color: #fff;
      transition: .3s;
      border-radius: 50%;
    }
    input:checked + .slider {
      background-color: var(--primary);
      box-shadow: 0 0 10px var(--primary-glow);
    }
    input:checked + .slider:before {
      transform: translateX(24px);
    }
    .speed-section {
      display: none;
      margin-top: 12px;
      border-top: 1px solid rgba(255,255,255,0.04);
      padding-top: 12px;
    }
    .flex-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
    }
    .range-slider {
      width: 100%;
      -webkit-appearance: none;
      background: #242c3d;
      height: 6px;
      border-radius: 3px;
      outline: none;
      margin-top: 10px;
    }
    .range-slider::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 18px;
      height: 18px;
      border-radius: 50%;
      background: var(--primary);
      cursor: pointer;
      box-shadow: 0 0 6px var(--primary-glow);
      transition: transform 0.1s;
    }
    .range-slider::-webkit-slider-thumb:hover {
      transform: scale(1.25);
    }
    .status-badge {
      font-size: 0.85rem;
      background: rgba(16, 185, 129, 0.12);
      color: var(--accent-neon);
      border: 1px solid rgba(16, 185, 129, 0.25);
      padding: 8px 16px;
      border-radius: 20px;
      font-weight: 600;
      display: inline-block;
      text-shadow: 0 0 8px var(--accent-neon-glow);
    }
    .footer {
      margin-top: 25px;
      font-size: 0.8rem;
      color: var(--text-muted);
      text-align: center;
    }
    /* Sensor Card Styles */
    .sensor-card {
      background: rgba(255, 255, 255, 0.02);
      border: 1px solid rgba(255, 255, 255, 0.04);
      border-radius: 20px;
      padding: 15px 20px;
      display: flex;
      justify-content: space-around;
      align-items: center;
      margin-bottom: 20px;
      box-shadow: inset 0 1px 1px rgba(255, 255, 255, 0.02);
    }
    .sensor-item {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .sensor-icon {
      width: 24px;
      height: 24px;
    }
    .sensor-info {
      display: flex;
      flex-direction: column;
      text-align: left;
    }
    .sensor-label {
      font-size: 0.75rem;
      color: var(--text-muted);
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }
    .sensor-value {
      font-size: 1.25rem;
      font-weight: 700;
      color: #fff;
      margin-top: 1px;
    }
    .sensor-divider {
      width: 1px;
      height: 30px;
      background: rgba(255, 255, 255, 0.08);
    }
    /* Wi-Fi Config Modal Styles */
    .modal {
      display: none;
      position: fixed;
      z-index: 100;
      left: 0;
      top: 0;
      width: 100%;
      height: 100%;
      background: rgba(9, 13, 22, 0.8);
      backdrop-filter: blur(8px);
      align-items: center;
      justify-content: center;
    }
    .modal-content {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 24px;
      padding: 30px;
      width: 90%;
      max-width: 380px;
      box-shadow: 0 24px 50px rgba(0, 0, 0, 0.6);
      text-align: center;
      position: relative;
    }
    .close-btn {
      position: absolute;
      top: 15px;
      right: 20px;
      background: transparent;
      border: none;
      color: var(--text-muted);
      font-size: 1.5rem;
      cursor: pointer;
    }
    .close-btn:hover {
      color: #fff;
    }
  </style>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/paho-mqtt/1.0.1/mqttws31.min.js" type="text/javascript"></script>
</head>
<body>
  <div class="container">
    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
      <h1 style="margin: 0; text-align: left;">IoT Control Center</h1>
      <button onclick="openWifiModal()" style="background: transparent; border: none; color: var(--text-muted); cursor: pointer; font-size: 1.3rem; transition: color 0.2s; display: flex; align-items: center; justify-content: center; padding: 5px;"><svg style="width:24px;height:24px" viewBox="0 0 24 24"><path fill="currentColor" d="M12,15.5A3.5,3.5 0 0,1 8.5,12A3.5,3.5 0 0,1 12,8.5A3.5,3.5 0 0,1 15.5,12A3.5,3.5 0 0,1 12,15.5M19.43,12.97C19.47,12.65 19.5,12.33 19.5,12C19.5,11.67 19.47,11.34 19.43,11L21.54,9.37C21.73,9.22 21.78,8.95 21.66,8.73L19.66,5.27C19.54,5.05 19.27,4.96 19.05,5.05L16.56,6.05C16.04,5.66 15.47,5.34 14.86,5.08L14.48,2.42C14.44,2.18 14.24,2 14,2H10C9.76,2 9.56,2.18 9.52,2.42L9.14,5.08C8.53,5.34 7.96,5.66 7.44,6.05L4.95,5.05C4.73,4.96 4.46,5.05 4.34,5.27L2.34,8.73C2.21,8.95 2.27,9.22 2.46,9.37L4.57,11C4.53,11.34 4.5,11.67 4.5,12C4.5,12.33 4.53,12.65 4.57,12.97L2.46,14.63C2.27,14.78 2.21,15.05 2.34,15.27L4.34,18.73C4.46,18.95 4.73,19.04 4.95,18.95L7.44,17.95C7.96,18.34 8.53,18.66 9.14,18.92L9.52,21.58C9.56,21.82 9.76,22 10,22H14C14.24,22 14.44,21.82 14.48,21.58L14.86,18.92C15.47,18.66 16.04,18.34 16.56,17.95L19.05,18.95C19.27,19.04 19.54,18.95 19.66,18.73L21.66,15.27C21.78,15.05 21.73,14.78 21.54,14.63L19.43,12.97Z" /></svg></button>
    </div>
    <div class="subtitle" style="text-align: left; margin-bottom: 20px;">Raspberry Pi IoT Controller (via ESP8266 Master)</div>

    <!-- Sensor Display Card (DHT11 on Pi 3) -->
    <div class="sensor-card">
      <div class="sensor-item">
        <svg class="sensor-icon" viewBox="0 0 24 24"><path fill="#f43f5e" d="M12 2A3 3 0 0 0 9 5v8.28a5 5 0 1 0 6 0V5a3 3 0 0 0-3-3m0 2a1 1 0 0 1 1 1v3h-2V5a1 1 0 0 1 1-1m0 10a3 3 0 0 1-1 5.67V13h2v6.67A3 3 0 0 1 12 14" /></svg>
        <div class="sensor-info">
          <span class="sensor-label">Pi 3 Temp</span>
          <span id="temp-val" class="sensor-value">-- °C</span>
        </div>
      </div>
      <div class="sensor-divider"></div>
      <div class="sensor-item">
        <svg class="sensor-icon" viewBox="0 0 24 24"><path fill="#0284c7" d="M12 2S4.5 8.5 4.5 13A7.5 7.5 0 0 0 12 20.5A7.5 7.5 0 0 0 19.5 13C19.5 8.5 12 2 12 2M12 4.4C14.7 7.1 17.5 10.9 17.5 13A5.5 5.5 0 0 1 12 18.5A5.5 5.5 0 0 1 6.5 13C6.5 10.9 9.3 7.1 12 4.4M12 9A1 1 0 0 0 11 10A2 2 0 0 0 9 12A1 1 0 1 0 11 12A1 1 0 0 0 12 11A1 1 0 1 0 12 9" /></svg>
        <div class="sensor-info">
          <span class="sensor-label">Pi 3 Humidity</span>
          <span id="hum-val" class="sensor-value">-- %</span>
        </div>
      </div>
    </div>

    <!-- Pin Cards (D0 to D4) -->
    <div id="pin-cards-container">
      <!-- Generated by JS -->
    </div>

    <!-- LED Matrix Scroll Speed Card -->
    <div class="card" style="margin-bottom: 20px;">
      <div class="card-header" style="border-bottom: 1px solid rgba(255,255,255,0.04); padding-bottom: 10px; margin-bottom: 10px;">
        <div class="pin-info">
          <span class="pin-title">LED Matrix Scroll Speed</span>
          <span class="pin-subtitle">Tốc độ chạy chữ hiển thị</span>
        </div>
      </div>
      <div>
        <div class="flex-row">
          <span style="font-size: 0.9rem; color: var(--text-muted);">Độ trễ cuộn chữ</span>
          <span id="matrix-speed-val" style="font-weight: 600; color: var(--primary);">40 ms</span>
        </div>
        <input type="range" min="20" max="150" value="40" class="range-slider" id="matrix-speed-slider" oninput="changeMatrixSpeed(this.value)">
      </div>
    </div>

    <!-- Device Scheduler Card -->
    <div class="card" style="margin-bottom: 20px;">
      <div class="card-header" style="border-bottom: 1px solid rgba(255,255,255,0.04); padding-bottom: 10px; margin-bottom: 10px;">
        <div class="pin-info">
          <span class="pin-title">Hẹn giờ thiết bị</span>
          <span class="pin-subtitle">Lập lịch bật/tắt thiết bị theo giờ</span>
        </div>
      </div>
      <div>
        <div style="display: flex; flex-direction: column; gap: 12px; margin-top: 10px;">
          <div style="display: flex; align-items: center; justify-content: space-between; gap: 10px;">
            <span style="font-size: 0.9rem; color: var(--text-muted); min-width: 80px;">Thiết bị:</span>
            <select id="sched-pin" style="flex: 1; background: rgba(255,255,255,0.04); border: 1px solid var(--border); border-radius: 12px; color: #fff; padding: 10px 14px; font-size: 0.95rem; outline: none;">
              <option value="D0">Pin D0 - Aux Relay</option>
              <option value="D1">Pin D1 - Water Pump</option>
              <option value="D2">Pin D2 - Solenoid Valve</option>
              <option value="D3">Pin D3 - Indicator LED</option>
              <option value="D4">Pin D4 - Onboard LED</option>
            </select>
          </div>
          
          <div style="display: flex; align-items: center; justify-content: space-between; gap: 10px;">
            <span style="font-size: 0.9rem; color: var(--text-muted); min-width: 80px;">Hành động:</span>
            <select id="sched-action" style="flex: 1; background: rgba(255,255,255,0.04); border: 1px solid var(--border); border-radius: 12px; color: #fff; padding: 10px 14px; font-size: 0.95rem; outline: none;">
              <option value="ON">BẬT (ON)</option>
              <option value="OFF">TẮT (OFF)</option>
            </select>
          </div>

          <div style="display: flex; align-items: center; justify-content: space-between; gap: 10px;">
            <span style="font-size: 0.9rem; color: var(--text-muted); min-width: 80px;">Thời gian:</span>
            <input type="time" id="sched-time" style="flex: 1; background: rgba(255,255,255,0.04); border: 1px solid var(--border); border-radius: 12px; color: #fff; padding: 8px 14px; font-size: 0.95rem; outline: none;">
          </div>

          <div style="display: flex; gap: 10px; margin-top: 5px;">
            <button onclick="setDeviceSchedule()" style="flex: 1; background: var(--primary); border: none; color: #fff; padding: 12px; border-radius: 12px; font-weight: 600; cursor: pointer; transition: all 0.2s; box-shadow: 0 4px 12px var(--primary-glow);">Đặt lịch</button>
            <button onclick="clearSchedules()" style="background: rgba(239, 68, 68, 0.12); border: 1px solid rgba(239, 68, 68, 0.25); color: #ef4444; padding: 12px 18px; border-radius: 12px; font-weight: 600; cursor: pointer; transition: all 0.2s;">Xóa hết</button>
          </div>
        </div>

        <!-- Local Schedules List -->
        <div id="sched-list-container" style="margin-top: 15px; border-top: 1px solid rgba(255,255,255,0.04); padding-top: 15px; display: none;">
          <div style="font-size: 0.9rem; font-weight: 600; margin-bottom: 8px; color: #fff;">Danh sách đã đặt (one-shot):</div>
          <div id="sched-list" style="display: flex; flex-direction: column; gap: 8px;"></div>
        </div>
      </div>
    </div>

    <!-- Countdown Timer Card -->
    <div class="card" style="margin-bottom: 20px;">
      <div class="card-header" style="border-bottom: 1px solid rgba(255,255,255,0.04); padding-bottom: 10px; margin-bottom: 10px;">
        <div class="pin-info">
          <span class="pin-title">Hẹn giờ đếm ngược</span>
          <span class="pin-subtitle">Bật thiết bị sau khoảng thời gian đếm ngược</span>
        </div>
      </div>
      <div>
        <div style="display: flex; flex-direction: column; gap: 12px; margin-top: 10px;">
          <div style="display: flex; align-items: center; justify-content: space-between; gap: 10px;">
            <span style="font-size: 0.9rem; color: var(--text-muted); min-width: 80px;">Thiết bị:</span>
            <select id="timer-pin" style="flex: 1; background: rgba(255,255,255,0.04); border: 1px solid var(--border); border-radius: 12px; color: #fff; padding: 10px 14px; font-size: 0.95rem; outline: none;">
              <option value="D0">Pin D0 - Aux Relay</option>
              <option value="D1">Pin D1 - Water Pump</option>
              <option value="D2">Pin D2 - Solenoid Valve</option>
              <option value="D3">Pin D3 - Indicator LED</option>
              <option value="D4">Pin D4 - Onboard LED</option>
            </select>
          </div>

          <div style="display: flex; align-items: center; justify-content: space-between; gap: 10px;">
            <span style="font-size: 0.9rem; color: var(--text-muted); min-width: 80px;">Số giây:</span>
            <input type="number" id="timer-duration" min="1" max="999" value="10" style="flex: 1; background: rgba(255,255,255,0.04); border: 1px solid var(--border); border-radius: 12px; color: #fff; padding: 8px 14px; font-size: 0.95rem; outline: none;">
          </div>

          <div style="display: flex; gap: 10px; margin-top: 5px;">
            <button id="btn-start-timer" onclick="startCountdownTimer()" style="flex: 1; background: var(--primary); border: none; color: #fff; padding: 12px; border-radius: 12px; font-weight: 600; cursor: pointer; transition: all 0.2s; box-shadow: 0 4px 12px var(--primary-glow);">Bắt đầu</button>
            <button id="btn-cancel-timer" onclick="cancelCountdownTimer()" style="background: rgba(239, 68, 68, 0.12); border: 1px solid rgba(239, 68, 68, 0.25); color: #ef4444; padding: 12px 18px; border-radius: 12px; font-weight: 600; cursor: pointer; transition: all 0.2s; display: none;">Hủy</button>
          </div>
        </div>

        <!-- Local Visual Countdown -->
        <div id="local-countdown-container" style="margin-top: 15px; border-top: 1px solid rgba(255,255,255,0.04); padding-top: 15px; display: none; text-align: center;">
          <div style="font-size: 0.9rem; color: var(--text-muted); margin-bottom: 5px;">Đang đếm ngược thiết bị <span id="local-countdown-pin" style="color:#fff; font-weight:600;">--</span>:</div>
          <div id="local-countdown-val" style="font-size: 2.5rem; font-weight: 800; color: var(--primary); text-shadow: 0 0 15px var(--primary-glow);">0</div>
        </div>
      </div>
    </div>

    <div class="flex-row" style="justify-content: center; margin-top: 15px;">
      <span class="status-badge" id="status">Connecting Cloud...</span>
    </div>
  </div>
  <div class="footer">Hosted on ESP8266 Controller</div>

  <!-- Wi-Fi Config Modal -->
  <div id="wifi-modal" class="modal">
    <div class="modal-content">
      <button class="close-btn" onclick="closeWifiModal()">&times;</button>
      <h2 style="font-size: 1.5rem; font-weight: 800; margin-bottom: 10px; background: linear-gradient(135deg, #ffffff 40%, var(--primary) 100%); -webkit-background-clip: text; -webkit-text-fill-color: transparent;">Configure Wi-Fi</h2>
      <p style="color: var(--text-muted); font-size: 0.85rem; margin-bottom: 20px;">Change ESP8266 home Wi-Fi settings</p>
      
      <div id="scan-status" style="font-size: 0.9rem; margin-bottom: 15px; color: var(--primary);">Scanning networks...</div>
      
      <div class="input-group" id="ssid-select-group" style="display:none; text-align: left; margin-bottom: 15px;">
        <label style="display:block; font-size:0.75rem; color:var(--text-muted); margin-bottom:5px; text-transform:uppercase;">SSID</label>
        <select id="modal-ssid" style="width:100%; background:rgba(255,255,255,0.04); border:1px solid var(--border); border-radius:10px; color:#fff; padding:10px; outline:none; font-size:0.9rem;"></select>
      </div>
      
      <div class="input-group" style="text-align: left; margin-bottom: 20px;">
        <label style="display:block; font-size:0.75rem; color:var(--text-muted); margin-bottom:5px; text-transform:uppercase;">Password</label>
        <input type="password" id="modal-password" placeholder="Enter password" style="width:100%; background:rgba(255,255,255,0.04); border:1px solid var(--border); border-radius:10px; color:#fff; padding:10px; outline:none; font-size:0.9rem;">
      </div>
      
      <button onclick="saveWifiSettings()" style="width:100%; background:var(--primary); border:none; color:#fff; padding:12px; border-radius:10px; font-weight:600; cursor:pointer; box-shadow:0 4px 12px var(--primary-glow); transition: all 0.2s;">Save & Restart</button>
    </div>
  </div>

  <script>
    let currentBoard = 3;
    const pinsConfig = [
      { name: "D0", desc: "GPIO 17 / Aux Relay" },
      { name: "D1", desc: "GPIO 27 / Water Pump" },
      { name: "D2", desc: "GPIO 22 / Solenoid Valve" },
      { name: "D3", desc: "GPIO 5 / Indicator LED" },
      { name: "D4", desc: "GPIO 6 / Onboard LED" }
    ];

    function generateCards() {
      const container = document.getElementById('pin-cards-container');
      container.innerHTML = '';
      pinsConfig.forEach(pin => {
        const idLower = pin.name.toLowerCase();
        const cardHtml = `
          <div class="card">
            <div class="card-header">
              <div class="pin-info">
                <span class="pin-title">Pin ${pin.name}</span>
                <span class="pin-subtitle">${pin.desc}</span>
              </div>
              <div class="controls-wrapper">
                <div class="control-item">
                  <div class="control-label">Manual</div>
                  <label class="switch">
                    <input type="checkbox" id="${idLower}-switch" onchange="togglePin('${pin.name}', this)">
                    <span class="slider"></span>
                  </label>
                </div>
                <div class="control-item">
                  <div class="control-label">Blink</div>
                  <label class="switch">
                    <input type="checkbox" id="${idLower}-blink" onchange="toggleBlink('${pin.name}', this)">
                    <span class="slider"></span>
                  </label>
                </div>
              </div>
            </div>
            <div id="${idLower}-speed-section" class="speed-section">
              <div class="flex-row">
                <span style="font-size: 0.9rem; color: var(--text-muted);">Blink Frequency</span>
                <span id="${idLower}-speed-val" style="font-weight: 600; color: var(--primary);">4 Hz</span>
              </div>
              <input type="range" min="1" max="15" value="4" class="range-slider" id="${idLower}-speed-slider" oninput="changeSpeed('${pin.name}', this.value)">
            </div>
          </div>
        `;
        container.innerHTML += cardHtml;
      });
    }

    // MQTT Over WebSockets Setup
    let mqttClient;
    const mqttHost = "broker.hivemq.com";
    const mqttPort = 8884;
    const mqttPath = "/mqtt";
    const mqttTopic = "duantuoicay/2modun/command";
    const mqttSensorTopic = "duantuoicay/2modun/sensor";

    function connectMQTT() {
      const statusEl = document.getElementById('status');
      statusEl.style.background = 'rgba(245, 158, 11, 0.12)';
      statusEl.style.color = '#f59e0b';
      statusEl.style.borderColor = 'rgba(245, 158, 11, 0.25)';
      statusEl.innerText = 'Connecting Cloud...';

      const clientId = "WebClient86-" + Math.random().toString(16).substr(2, 8);
      mqttClient = new Paho.MQTT.Client(mqttHost, Number(mqttPort), mqttPath, clientId);

      mqttClient.onConnectionLost = onConnectionLost;
      mqttClient.onMessageArrived = onMessageArrived;

      const options = {
        useSSL: true,
        onSuccess: onConnectSuccess,
        onFailure: onConnectFailure,
        keepAliveInterval: 30
      };

      mqttClient.connect(options);
    }

    function onConnectSuccess() {
      console.log("Connected to MQTT Broker via WebSockets");
      const statusEl = document.getElementById('status');
      statusEl.style.background = 'rgba(16, 185, 129, 0.12)';
      statusEl.style.color = 'var(--accent-neon)';
      statusEl.style.borderColor = 'rgba(16, 185, 129, 0.25)';
      statusEl.innerText = 'Cloud Connected';
      
      // Subscribe to sensor readings
      mqttClient.subscribe(mqttSensorTopic);
      console.log("Subscribed to " + mqttSensorTopic);
    }

    function onConnectFailure(err) {
      console.log("MQTT Connection Failed:", err);
      const statusEl = document.getElementById('status');
      statusEl.style.background = 'rgba(239, 68, 68, 0.12)';
      statusEl.style.color = '#ef4444';
      statusEl.style.borderColor = 'rgba(239, 68, 68, 0.25)';
      statusEl.innerText = 'Cloud Connect Error';
      setTimeout(connectMQTT, 5000);
    }

    function onConnectionLost(responseObject) {
      if (responseObject.errorCode !== 0) {
        console.log("MQTT Connection Lost:", responseObject.errorMessage);
        const statusEl = document.getElementById('status');
        statusEl.style.background = 'rgba(245, 158, 11, 0.12)';
        statusEl.style.color = '#f59e0b';
        statusEl.style.borderColor = 'rgba(245, 158, 11, 0.25)';
        statusEl.innerText = 'Reconnecting...';
        setTimeout(connectMQTT, 5000);
      }
    }

    function onMessageArrived(message) {
      console.log("Received MQTT message: " + message.destinationName + " -> " + message.payloadString);
      if (message.destinationName === mqttSensorTopic) {
        const parts = message.payloadString.split(',');
        if (parts.length >= 2) {
          document.getElementById('temp-val').innerText = parts[0] + ' °C';
          document.getElementById('hum-val').innerText = parts[1] + ' %';
        }
      }
    }

    function publishCommand(boardId, pin, action, speed) {
      const statusEl = document.getElementById('status');
      if (mqttClient && mqttClient.isConnected()) {
        const payload = `${boardId},${pin},${action},${speed}`;
        const message = new Paho.MQTT.Message(payload);
        message.destinationName = mqttTopic;
        mqttClient.send(message);
        console.log("Published MQTT message: " + payload);

        const oldText = statusEl.innerText;
        const oldBg = statusEl.style.background;
        const oldColor = statusEl.style.color;
        const oldBorder = statusEl.style.borderColor;

        statusEl.style.background = 'rgba(16, 185, 129, 0.12)';
        statusEl.style.color = 'var(--accent-neon)';
        statusEl.style.borderColor = 'rgba(16, 185, 129, 0.25)';
        statusEl.innerText = 'Command Sent!';

        setTimeout(() => {
          if (statusEl.innerText === 'Command Sent!') {
            statusEl.innerText = oldText;
            statusEl.style.background = oldBg;
            statusEl.style.color = oldColor;
            statusEl.style.borderColor = oldBorder;
          }
        }, 800);
      } else {
        console.log("MQTT Client not connected. Retrying connect...");
        connectMQTT();
      }
    }

    function togglePin(pin, el) {
      const state = el.checked ? 'ON' : 'OFF';
      publishCommand(currentBoard, pin, state, 0);
    }

    function toggleBlink(pin, el) {
      const enabled = el.checked;
      const lower = pin.toLowerCase();
      document.getElementById(`${lower}-speed-section`).style.display = enabled ? 'block' : 'none';
      
      const manualSwitch = document.getElementById(`${lower}-switch`);
      manualSwitch.disabled = enabled;
      manualSwitch.parentElement.parentElement.style.opacity = enabled ? '0.35' : '1';
      
      if (enabled) {
        const speedVal = document.getElementById(`${lower}-speed-slider`).value;
        const delayMs = Math.round(1000 / (2 * speedVal));
        publishCommand(currentBoard, pin, "BLINK", delayMs);
      } else {
        publishCommand(currentBoard, pin, "STOPBLINK", 0);
      }
    }

    function changeSpeed(pin, val) {
      const lower = pin.toLowerCase();
      document.getElementById(`${lower}-speed-val`).innerText = val + ' Hz';
      const delayMs = Math.round(1000 / (2 * val));
      publishCommand(currentBoard, pin, "BLINK", delayMs);
    }

    function changeMatrixSpeed(val) {
      document.getElementById('matrix-speed-val').innerText = val + ' ms';
      publishCommand(3, "MATRIX", "SPEED", val);
    }

    // Scheduling functions
    let schedules = [];

    function loadSchedules() {
      const stored = localStorage.getItem('device_schedules');
      if (stored) {
        try {
          schedules = JSON.parse(stored);
        } catch (e) {
          schedules = [];
        }
      }
      renderSchedules();
    }

    function saveSchedules() {
      localStorage.setItem('device_schedules', JSON.stringify(schedules));
    }

    function renderSchedules() {
      const container = document.getElementById('sched-list-container');
      const listEl = document.getElementById('sched-list');
      if (schedules.length === 0) {
        container.style.display = 'none';
        listEl.innerHTML = '';
        return;
      }
      container.style.display = 'block';
      listEl.innerHTML = '';
      schedules.forEach((item, index) => {
        const pinText = pinsConfig.find(p => p.name === item.pin)?.desc || item.pin;
        const row = document.createElement('div');
        row.style.display = 'flex';
        row.style.justifyContent = 'space-between';
        row.style.alignItems = 'center';
        row.style.background = 'rgba(255,255,255,0.02)';
        row.style.border = '1px solid rgba(255,255,255,0.04)';
        row.style.padding = '8px 12px';
        row.style.borderRadius = '8px';
        
        row.innerHTML = `
          <div style="display: flex; flex-direction: column;">
            <span style="font-size: 0.85rem; font-weight: 600; color: #fff;">${item.pin} (${item.action}) - <span style="color: var(--primary);">${item.time}</span></span>
            <span style="font-size: 0.75rem; color: var(--text-muted);">${pinText}</span>
          </div>
          <button onclick="removeScheduleAt(${index})" style="background: transparent; border: none; color: #ef4444; cursor: pointer; padding: 4px; display: flex; align-items: center; justify-content: center;">
            <svg style="width:16px;height:16px" viewBox="0 0 24 24"><path fill="currentColor" d="M19,4H15.5L14.5,3H9.5L8.5,4H5V6H19M6,19A2,2 0 0,0 8,21H16A2,2 0 0,0 18,19V7H6V19Z" /></svg>
          </button>
        `;
        listEl.appendChild(row);
      });
    }

    function setDeviceSchedule() {
      const pin = document.getElementById('sched-pin').value;
      const action = document.getElementById('sched-action').value;
      const timeVal = document.getElementById('sched-time').value;
      if (!timeVal) {
        alert('Vui lòng chọn thời gian hẹn giờ!');
        return;
      }
      
      // Publish schedule set to MQTT: 3,SCHED,SET,pin,action,time
      publishCommand(3, "SCHED", "SET", `${pin},${action},${timeVal}`);
      
      // Add to local list
      schedules.push({ pin, action, time: timeVal });
      saveSchedules();
      renderSchedules();
      
      alert(`Đã đặt lịch hẹn cho ${pin} ${action} lúc ${timeVal}`);
    }

    function removeScheduleAt(index) {
      schedules.splice(index, 1);
      saveSchedules();
      
      // Re-synchronize Pi: Clear all, then push remaining
      publishCommand(3, "SCHED", "CLEAR", "0");
      
      // Re-send all remaining schedules one by one to Pi after a small delay
      schedules.forEach((item, idx) => {
        setTimeout(() => {
          publishCommand(3, "SCHED", "SET", `${item.pin},${item.action},${item.time}`);
        }, (idx + 1) * 300);
      });
      
      renderSchedules();
    }

    function clearSchedules() {
      if (confirm('Bạn có chắc chắn muốn xóa toàn bộ lịch hẹn giờ?')) {
        schedules = [];
        saveSchedules();
        renderSchedules();
        publishCommand(3, "SCHED", "CLEAR", "0");
      }
    }

    // Countdown Timer Functions
    let countdownInterval = null;
    let localRemaining = 0;

    function startCountdownTimer() {
      const pin = document.getElementById('timer-pin').value;
      const duration = parseInt(document.getElementById('timer-duration').value);
      if (isNaN(duration) || duration <= 0) {
        alert('Vui lòng nhập số giây hợp lệ!');
        return;
      }

      // Publish start countdown: 3,TIMER,START,pin,duration
      publishCommand(3, "TIMER", "START", `${pin},${duration}`);

      // Start local UI countdown
      clearInterval(countdownInterval);
      localRemaining = duration;
      document.getElementById('local-countdown-pin').innerText = pin;
      document.getElementById('local-countdown-val').innerText = localRemaining;
      document.getElementById('local-countdown-container').style.display = 'block';
      document.getElementById('btn-cancel-timer').style.display = 'block';

      countdownInterval = setInterval(() => {
        localRemaining--;
        if (localRemaining <= 0) {
          clearInterval(countdownInterval);
          document.getElementById('local-countdown-container').style.display = 'none';
          document.getElementById('btn-cancel-timer').style.display = 'none';
          // Update the switch state locally
          const switchEl = document.getElementById(`${pin.toLowerCase()}-switch`);
          if (switchEl) switchEl.checked = true;
          alert(`Đã kích hoạt BẬT thiết bị ${pin} sau khi đếm ngược xong!`);
        } else {
          document.getElementById('local-countdown-val').innerText = localRemaining;
        }
      }, 1000);
    }

    function cancelCountdownTimer() {
      clearInterval(countdownInterval);
      document.getElementById('local-countdown-container').style.display = 'none';
      document.getElementById('btn-cancel-timer').style.display = 'none';
      // Publish cancel: 3,TIMER,CANCEL,0
      publishCommand(3, "TIMER", "CANCEL", "0");
    }

    // Wi-Fi Configuration Modal Functions
    function openWifiModal() {
      document.getElementById('wifi-modal').style.display = 'flex';
      const scanStatus = document.getElementById('scan-status');
      const ssidGroup = document.getElementById('ssid-select-group');
      scanStatus.style.display = 'block';
      scanStatus.innerText = 'Scanning Wi-Fi networks...';
      ssidGroup.style.display = 'none';

      fetch('/scan')
        .then(res => res.json())
        .then(data => {
          const select = document.getElementById('modal-ssid');
          select.innerHTML = '';
          if (data.length === 0) {
            select.innerHTML = '<option value="">No networks found</option>';
          } else {
            data.forEach(ssid => {
              const opt = document.createElement('option');
              opt.value = ssid;
              opt.innerText = ssid;
              select.appendChild(opt);
            });
          }
          scanStatus.style.display = 'none';
          ssidGroup.style.display = 'block';
        })
        .catch(() => {
          scanStatus.innerText = 'Error scanning. Manual entry:';
          ssidGroup.innerHTML = '<label style="display:block; font-size:0.75rem; color:var(--text-muted); margin-bottom:5px; text-transform:uppercase;">SSID</label><input type="text" id="modal-ssid-text" placeholder="Enter SSID" style="width:100%; background:rgba(255,255,255,0.04); border:1px solid var(--border); border-radius:10px; color:#fff; padding:10px; outline:none; font-size:0.9rem;">';
          scanStatus.style.display = 'none';
          ssidGroup.style.display = 'block';
        });
    }

    function closeWifiModal() {
      document.getElementById('wifi-modal').style.display = 'none';
    }

    function saveWifiSettings() {
      const select = document.getElementById('modal-ssid');
      const textInput = document.getElementById('modal-ssid-text');
      const ssid = select ? select.value : (textInput ? textInput.value : '');
      const password = document.getElementById('modal-password').value;

      if (!ssid) {
        alert('Please enter or select an SSID');
        return;
      }

      const statusEl = document.getElementById('status');
      statusEl.style.background = 'rgba(245, 158, 11, 0.12)';
      statusEl.style.color = '#f59e0b';
      statusEl.style.borderColor = 'rgba(245, 158, 11, 0.25)';
      statusEl.innerText = 'Saving Settings...';
      
      closeWifiModal();

      fetch(`/savewifi?ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`)
        .then(res => {
          if (res.ok) {
            statusEl.style.background = 'rgba(16, 185, 129, 0.12)';
            statusEl.style.color = 'var(--accent-neon)';
            statusEl.style.borderColor = 'rgba(16, 185, 129, 0.25)';
            statusEl.innerText = 'Saved! Restarting...';
            alert('Settings saved. ESP8266 will restart to connect to new network.');
          } else {
            throw new Error();
          }
        })
        .catch(() => {
          statusEl.style.background = 'rgba(239, 68, 68, 0.12)';
          statusEl.style.color = '#ef4444';
          statusEl.style.borderColor = 'rgba(239, 68, 68, 0.25)';
          statusEl.innerText = 'Save Config Fail';
        });
    }

    // Initialize UI and Connect MQTT
    generateCards();
    loadSchedules();
    connectMQTT();
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  if (apMode) {
    String options = "";
    int n = WiFi.scanNetworks();
    if (n <= 0) {
      options += "<option value=''>No networks found</option>";
    } else {
      String seenSSIDs[20];
      int seenCount = 0;
      for (int i = 0; i < n; ++i) {
        String currentSSID = WiFi.SSID(i);
        bool duplicate = false;
        for (int j = 0; j < seenCount; ++j) {
          if (seenSSIDs[j] == currentSSID) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate && !currentSSID.isEmpty() && seenCount < 20) {
          seenSSIDs[seenCount++] = currentSSID;
          options += "<option value=\"" + currentSSID + "\">" + currentSSID + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
        }
      }
    }
    String page = String(CONFIG_HTML);
    page.replace("<!-- OPTIONS_PLACEHOLDER -->", options);
    server.send(200, "text/html", page);
  } else {
    server.send_P(200, "text/html", INDEX_HTML);
  }
}

void handleStatus() {
  String json = "{";
  json += "\"wifi\":\"" + WiFi.SSID() + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"pins\":[";
  for (int i = 0; i < 5; i++) {
    json += "{";
    json += "\"name\":\"" + String(pins[i].name) + "\",";
    json += "\"state\":" + String(pins[i].manualState ? "true" : "false") + ",";
    json += "\"blink\":" + String(pins[i].blinkEnabled ? "true" : "false") + ",";
    json += "\"speed\":" + String(pins[i].blinkDelay);
    json += "}";
    if (i < 4) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleSetPin() {
  if (server.hasArg("pin") && server.hasArg("state")) {
    String pinStr = server.arg("pin");
    bool state = (server.arg("state") == "on");
    
    int index = -1;
    for (int i = 0; i < 5; i++) {
      if (pinStr == pins[i].name) {
        index = i;
        break;
      }
    }

    if (index != -1) {
      pins[index].manualState = state;
      updatePin(index);
      Serial.printf("Web Control: Pin %s set to %s\n", pins[index].name, state ? "ON" : "OFF");
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Invalid Pin");
    }
  } else {
    server.send(400, "text/plain", "Missing Arguments");
  }
}

void handleSetBlink() {
  if (server.hasArg("pin")) {
    String pinStr = server.arg("pin");
    int index = -1;
    for (int i = 0; i < 5; i++) {
      if (pinStr == pins[i].name) {
        index = i;
        break;
      }
    }

    if (index != -1) {
      if (server.hasArg("enabled")) {
        pins[index].blinkEnabled = (server.arg("enabled") == "true");
        updatePin(index);
        Serial.printf("Web Control: Blink mode for %s set to %s\n", pins[index].name, pins[index].blinkEnabled ? "Enabled" : "Disabled");
      }
      if (server.hasArg("speed")) {
        pins[index].blinkDelay = server.arg("speed").toInt();
        Serial.printf("Web Control: Blink speed for %s set to %d ms\n", pins[index].name, pins[index].blinkDelay);
      }
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Invalid Pin");
    }
  } else {
    server.send(400, "text/plain", "Missing Pin Argument");
  }
}

void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  if (n > 0) {
    String seenSSIDs[20];
    int seenCount = 0;
    for (int i = 0; i < n; ++i) {
      String currentSSID = WiFi.SSID(i);
      bool duplicate = false;
      for (int j = 0; j < seenCount; ++j) {
        if (seenSSIDs[j] == currentSSID) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate && !currentSSID.isEmpty() && seenCount < 20) {
        seenSSIDs[seenCount++] = currentSSID;
        if (seenCount > 1) json += ",";
        json += "\"" + currentSSID + "\"";
      }
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleSaveWifi() {
  if (server.hasArg("ssid")) {
    String new_ssid = server.arg("ssid");
    String new_pass = server.hasArg("password") ? server.arg("password") : "";
    
    Serial.println("Saving new Wi-Fi credentials:");
    Serial.println("SSID: " + new_ssid);
    
    server.send(200, "text/plain", "OK");
    delay(2000);
    
    WiFi.persistent(true);
    WiFi.begin(new_ssid.c_str(), new_pass.c_str());
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  // Expected format: board_id,pin,action,speed (e.g. "1,D4,ON,0")
  int firstComma = message.indexOf(',');
  int secondComma = message.indexOf(',', firstComma + 1);
  int thirdComma = message.indexOf(',', secondComma + 1);

  if (firstComma != -1 && secondComma != -1 && thirdComma != -1) {
    int targetBoard = message.substring(0, firstComma).toInt();
    String pinStr = message.substring(firstComma + 1, secondComma);
    String actionStr = message.substring(secondComma + 1, thirdComma);
    int speedVal = message.substring(thirdComma + 1).toInt();

    // Check if this command targets this board or all boards
    if (targetBoard == 0 || targetBoard == MY_BOARD_ID) {
      int index = -1;
      pinStr.toUpperCase();
      for (int i = 0; i < 5; i++) {
        if (pinStr == pins[i].name) {
          index = i;
          break;
        }
      }

      if (index != -1) {
        actionStr.toUpperCase();
        if (actionStr == "ON") {
          pins[index].manualState = true;
          pins[index].blinkEnabled = false;
          updatePin(index);
        } else if (actionStr == "OFF") {
          pins[index].manualState = false;
          pins[index].blinkEnabled = false;
          updatePin(index);
        } else if (actionStr == "BLINK") {
          pins[index].blinkEnabled = true;
          if (speedVal > 0) {
            pins[index].blinkDelay = speedVal;
          }
          updatePin(index);
        } else if (actionStr == "STOPBLINK") {
          pins[index].blinkEnabled = false;
          updatePin(index);
        }
      }
    }
  }
}

// MQTT Connection Recovery
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT Broker...");
    // Create unique client ID
    String clientId = "ESP8266Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected!");
      client.subscribe(mqtt_topic);
      client.subscribe(mqtt_sensor_topic);
      Serial.printf("Subscribed to command & sensor topics\n");
    } else {
      Serial.print("failed, state=");
      Serial.print(client.state());
      Serial.println(" - Retrying in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP8266 WiFi Setup ---");

  // Initialize GPIO pins D0 to D4 as outputs
  for (int i = 0; i < 5; i++) {
    pinMode(pins[i].gpio, OUTPUT);
    digitalWrite(pins[i].gpio, pins[i].activeLow ? HIGH : LOW); // Default OFF
  }

  // Load Wi-Fi credentials saved in SDK flash
  WiFi.persistent(true);
  ssid = WiFi.SSID();
  password = WiFi.psk();
  if (ssid.length() == 0) {
    ssid = "PIXELBOX T1";
    password = "Pixelbox2022";
  }

  Serial.println("Attempting to connect to Wi-Fi SSID: " + ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { // 10 seconds timeout
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to Wi-Fi!");
    Serial.print("ESP8266 IP: ");
    Serial.println(WiFi.localIP());

    // Setup mDNS
    if (MDNS.begin("esp8266")) {
      Serial.println("mDNS responder started. Reachable at: http://esp8266.local");
    } else {
      Serial.println("Error setting up MDNS responder!");
    }
    apMode = false;
  } else {
    Serial.println("\nFailed to connect to Wi-Fi. Starting Access Point mode...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP8266_IoT_Config", "12345678");
    Serial.print("Access Point started. IP: ");
    Serial.println(WiFi.softAPIP());
    apMode = true;
  }

  // Set up MQTT client
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Setup routes
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/savewifi", handleSaveWifi);
  server.on("/setpin", handleSetPin);
  server.on("/setblink", handleSetBlink);

  server.begin();
  Serial.println("HTTP Server started.");
}

void loop() {
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      static unsigned long lastReconnectAttempt = 0;
      unsigned long now = millis();
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        Serial.print("Connecting to MQTT Broker in loop...");
        String clientId = "ESP8266Client-" + String(random(0xffff), HEX);
        if (client.connect(clientId.c_str())) {
          Serial.println("connected!");
          client.subscribe(mqtt_topic);
          client.subscribe(mqtt_sensor_topic);
        } else {
          Serial.print("failed, state=");
          Serial.println(client.state());
        }
      }
    } else {
      client.loop();
    }
  }

  MDNS.update();

  unsigned long currentMillis = millis();
  for (int i = 0; i < 5; i++) {
    if (pins[i].blinkEnabled) {
      if (currentMillis - pins[i].lastBlinkTime >= (unsigned long)pins[i].blinkDelay) {
        pins[i].lastBlinkTime = currentMillis;
        pins[i].currentToggle = !pins[i].currentToggle;
        digitalWrite(pins[i].gpio, pins[i].activeLow ? (pins[i].currentToggle ? LOW : HIGH) : (pins[i].currentToggle ? HIGH : LOW));
      }
    }
  }
}