#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// Pin definitions for ESP32-C3 Supermini
#define MOTOR_PIN_1 0
#define MOTOR_PIN_2 8
#define BTN_UP 1      // Short press: scroll UP, Long press: SELECT
#define BTN_DOWN 2    // Short press: scroll DOWN, Long press: BACK

// I2C pins
#define I2C_SDA 5
#define I2C_SCL 6

// Display offset and dimensions
#define DISPLAY_OFFSET_X 28
#define DISPLAY_OFFSET_Y 24
#define DISPLAY_WIDTH 72
#define DISPLAY_HEIGHT 40

// WiFi credentials - Change these to your network
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Initialize U8g2 for SSD1306 128x64 with custom I2C pins
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);

// Web server on port 80
WebServer server(80);

// Button timing
#define DEBOUNCE_TIME 50
int LONG_PRESS_TIME = 800;
int VIBRATION_DURATION = 30;

// Menu items (now editable)
String menuItems[] = {
  "Opt 1",
  "Opt 2",
  "Opt 3",
  "Opt 4",
  "Settings"
};
const int menuSize = 5;

// Variables
int currentItem = 0;
unsigned long btnUpPressTime = 0;
unsigned long btnDownPressTime = 0;
bool btnUpPressed = false;
bool btnDownPressed = false;
bool btnUpLongHandled = false;
bool btnDownLongHandled = false;
bool webServerActive = false;
bool inSettingsMode = false;

// HTML page
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Menu Settings</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      max-width: 600px;
      margin: 50px auto;
      padding: 20px;
      background: #f0f0f0;
    }
    .container {
      background: white;
      padding: 30px;
      border-radius: 10px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.1);
    }
    h1 {
      color: #333;
      text-align: center;
    }
    .setting {
      margin: 20px 0;
      padding: 15px;
      background: #f9f9f9;
      border-radius: 5px;
    }
    label {
      display: block;
      margin-bottom: 5px;
      font-weight: bold;
      color: #555;
    }
    input[type="range"] {
      width: 100%;
      margin: 10px 0;
    }
    input[type="text"] {
      width: 100%;
      padding: 8px;
      border: 1px solid #ddd;
      border-radius: 4px;
      box-sizing: border-box;
    }
    .value-display {
      text-align: center;
      color: #666;
      font-size: 18px;
      margin: 5px 0;
    }
    button {
      background: #4CAF50;
      color: white;
      padding: 12px 30px;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      font-size: 16px;
      width: 100%;
      margin-top: 10px;
    }
    button:hover {
      background: #45a049;
    }
    .status {
      text-align: center;
      padding: 10px;
      margin: 10px 0;
      border-radius: 5px;
      display: none;
    }
    .status.success {
      background: #d4edda;
      color: #155724;
      display: block;
    }
    .note {
      background: #fff3cd;
      padding: 10px;
      border-radius: 5px;
      margin: 15px 0;
      color: #856404;
      text-align: center;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>⚙️ ESP32 Settings</h1>

    <div class="note">
      Press and hold the DOWN button on device to close WiFi and return to menu
    </div>

    <div class="setting">
      <label>Vibration Duration (ms)</label>
      <input type="range" id="vibration" min="10" max="200" value="30" oninput="updateValue('vibration')">
      <div class="value-display" id="vibration-value">30 ms</div>
    </div>

    <div class="setting">
      <label>Long Press Time (ms)</label>
      <input type="range" id="longpress" min="300" max="2000" value="800" step="100" oninput="updateValue('longpress')">
      <div class="value-display" id="longpress-value">800 ms</div>
    </div>

    <div class="setting">
      <label>Menu Option 1 Name</label>
      <input type="text" id="opt1" value="Opt 1" maxlength="10">
    </div>

    <div class="setting">
      <label>Menu Option 2 Name</label>
      <input type="text" id="opt2" value="Opt 2" maxlength="10">
    </div>

    <div class="setting">
      <label>Menu Option 3 Name</label>
      <input type="text" id="opt3" value="Opt 3" maxlength="10">
    </div>

    <div class="setting">
      <label>Menu Option 4 Name</label>
      <input type="text" id="opt4" value="Opt 4" maxlength="10">
    </div>

    <button onclick="saveSettings()">💾 Save Settings</button>
    <div class="status" id="status"></div>

    <button onclick="testVibration()" style="background: #2196F3; margin-top: 20px;">📳 Test Vibration</button>
    <button onclick="getSettings()" style="background: #ff9800;">🔄 Load Current Settings</button>
  </div>

  <script>
    // Load current settings on page load
    window.onload = function() {
      getSettings();
    };

    function updateValue(id) {
      const slider = document.getElementById(id);
      const display = document.getElementById(id + '-value');
      display.textContent = slider.value + ' ms';
    }

    function getSettings() {
      fetch('/get')
      .then(response => response.json())
      .then(data => {
        document.getElementById('vibration').value = data.vibration;
        document.getElementById('longpress').value = data.longpress;
        document.getElementById('opt1').value = data.opt1;
        document.getElementById('opt2').value = data.opt2;
        document.getElementById('opt3').value = data.opt3;
        document.getElementById('opt4').value = data.opt4;
        updateValue('vibration');
        updateValue('longpress');
      });
    }

    function saveSettings() {
      const settings = {
        vibration: document.getElementById('vibration').value,
        longpress: document.getElementById('longpress').value,
        opt1: document.getElementById('opt1').value,
        opt2: document.getElementById('opt2').value,
        opt3: document.getElementById('opt3').value,
        opt4: document.getElementById('opt4').value
      };

      fetch('/save', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(settings)
      })
      .then(response => response.text())
      .then(data => {
        const status = document.getElementById('status');
        status.className = 'status success';
        status.textContent = '✓ Settings saved successfully!';
        setTimeout(() => status.style.display = 'none', 3000);
      });
    }

    function testVibration() {
      fetch('/vibrate')
      .then(response => response.text())
      .then(data => console.log(data));
    }
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  // Initialize pins
  pinMode(MOTOR_PIN_1, OUTPUT);
  pinMode(MOTOR_PIN_2, OUTPUT);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  digitalWrite(MOTOR_PIN_1, LOW);
  digitalWrite(MOTOR_PIN_2, LOW);

  // Initialize U8g2
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tr);

  // Welcome screen
  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 15, DISPLAY_OFFSET_Y + 15, "READY!");
  u8g2.sendBuffer();

  vibrate(100);
  delay(1000);

  drawMenu();
}

void loop() {
  if (webServerActive) {
    server.handleClient();
  }
  handleButtons();
}

void handleButtons() {
  bool upState = digitalRead(BTN_UP) == LOW;
  bool downState = digitalRead(BTN_DOWN) == LOW;

  // UP Button handling
  if (upState && !btnUpPressed) {
    btnUpPressed = true;
    btnUpPressTime = millis();
    btnUpLongHandled = false;
  }

  if (upState && btnUpPressed && !btnUpLongHandled) {
    if (millis() - btnUpPressTime >= LONG_PRESS_TIME) {
      // Long press - SELECT
      btnUpLongHandled = true;
      selectItem();
    }
  }

  if (!upState && btnUpPressed) {
    if (!btnUpLongHandled && millis() - btnUpPressTime >= DEBOUNCE_TIME) {
      // Short press - UP (only if not in settings mode)
      if (!inSettingsMode) {
        scrollUp();
      }
    }
    btnUpPressed = false;
  }

  // DOWN Button handling
  if (downState && !btnDownPressed) {
    btnDownPressed = true;
    btnDownPressTime = millis();
    btnDownLongHandled = false;
  }

  if (downState && btnDownPressed && !btnDownLongHandled) {
    if (millis() - btnDownPressTime >= LONG_PRESS_TIME) {
      // Long press - BACK
      btnDownLongHandled = true;
      goBack();
    }
  }

  if (!downState && btnDownPressed) {
    if (!btnDownLongHandled && millis() - btnDownPressTime >= DEBOUNCE_TIME) {
      // Short press - DOWN (only if not in settings mode)
      if (!inSettingsMode) {
        scrollDown();
      }
    }
    btnDownPressed = false;
  }
}

void scrollUp() {
  currentItem--;
  if (currentItem < 0) {
    currentItem = menuSize - 1;
  }
  vibrate(VIBRATION_DURATION);
  drawMenu();
  Serial.println("Scroll Up");
}

void scrollDown() {
  currentItem++;
  if (currentItem >= menuSize) {
    currentItem = 0;
  }
  vibrate(VIBRATION_DURATION);
  drawMenu();
  Serial.println("Scroll Down");
}

void selectItem() {
  vibrate(100);
  Serial.print("Selected: ");
  Serial.println(menuItems[currentItem]);

  // Check if Settings was selected
  if (currentItem == menuSize - 1) {
    startWebServer();
    return;
  }

  // Show selected item
  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 10, "SELECT:");
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 25, menuItems[currentItem].c_str());
  u8g2.sendBuffer();

  delay(1000);
  drawMenu();
}

void goBack() {
  vibrate(50);
  Serial.println("Going back...");

  // If in settings mode, stop the web server
  if (inSettingsMode && webServerActive) {
    stopWebServer();
    return;
  }

  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 10, DISPLAY_OFFSET_Y + 15, "< BACK");
  u8g2.sendBuffer();

  delay(500);
  drawMenu();
}

void drawMenu() {
  u8g2.clearBuffer();

  // Title
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 8, "== MENU ==");

  // Show current item + one above/below (3 items visible)
  int startItem = max(0, currentItem - 1);
  int endItem = min(menuSize, startItem + 3);

  int yPos = DISPLAY_OFFSET_Y + 24;
  for (int i = startItem; i < endItem; i++) {
    int xPos = DISPLAY_OFFSET_X + 2;

    if (i == currentItem) {
      // Draw selection box
      u8g2.drawBox(xPos, yPos - 8, 68, 10);
      u8g2.setDrawColor(0); // Inverted color for text
      u8g2.drawStr(xPos + 2, yPos, ">");
      u8g2.drawStr(xPos + 10, yPos, menuItems[i].c_str());
      u8g2.setDrawColor(1); // Reset to normal color
    } else {
      u8g2.drawStr(xPos + 2, yPos, " ");
      u8g2.drawStr(xPos + 10, yPos, menuItems[i].c_str());
    }
    yPos += 10;
  }

  u8g2.sendBuffer();
}

void vibrate(int duration) {
  // Activate both motors
  digitalWrite(MOTOR_PIN_1, HIGH);
  digitalWrite(MOTOR_PIN_2, HIGH);
  delay(duration);
  digitalWrite(MOTOR_PIN_1, LOW);
  digitalWrite(MOTOR_PIN_2, LOW);
}

void startWebServer() {
  inSettingsMode = true;

  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 10, "Starting");
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 20, "WiFi...");
  u8g2.sendBuffer();

  // Connect to WiFi or start Access Point
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  // If WiFi connection fails, start Access Point
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.softAP("ESP32-Menu", "12345678");
    Serial.println("AP Started");
  }

  // Setup web server routes
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", htmlPage);
  });

  server.on("/get", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["vibration"] = VIBRATION_DURATION;
    doc["longpress"] = LONG_PRESS_TIME;
    doc["opt1"] = menuItems[0];
    doc["opt2"] = menuItems[1];
    doc["opt3"] = menuItems[2];
    doc["opt4"] = menuItems[3];

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.on("/save", HTTP_POST, []() {
    String body = server.arg("plain");
    Serial.println("Settings received: " + body);

    // Parse JSON
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (!error) {
      // Update settings
      VIBRATION_DURATION = doc["vibration"];
      LONG_PRESS_TIME = doc["longpress"];
      menuItems[0] = doc["opt1"].as<String>();
      menuItems[1] = doc["opt2"].as<String>();
      menuItems[2] = doc["opt3"].as<String>();
      menuItems[3] = doc["opt4"].as<String>();

      Serial.println("Settings updated!");
      Serial.println("Vibration: " + String(VIBRATION_DURATION));
      Serial.println("Long Press: " + String(LONG_PRESS_TIME));
      Serial.println("Menu items updated");
    }

    server.send(200, "text/plain", "OK");
  });

  server.on("/vibrate", HTTP_GET, []() {
    vibrate(VIBRATION_DURATION);
    server.send(200, "text/plain", "Vibrated!");
  });

  server.begin();
  webServerActive = true;

  // Display IP address
  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 8, "Web UI:");

  String ip;
  if (WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP().toString();
  } else {
    ip = WiFi.softAPIP().toString();
  }

  u8g2.drawStr(DISPLAY_OFFSET_X + 2, DISPLAY_OFFSET_Y + 20, ip.c_str());
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 32, "Long:BACK");
  u8g2.sendBuffer();

  Serial.println("Web server started at: " + ip);
}

void stopWebServer() {
  server.stop();
  server.close();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  webServerActive = false;
  inSettingsMode = false;

  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 15, "WiFi OFF");
  u8g2.sendBuffer();

  delay(1000);
  drawMenu();

  Serial.println("Web server stopped, WiFi off");
}
