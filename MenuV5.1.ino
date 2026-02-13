#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEHIDDevice.h>
#include <HIDTypes.h>
#include <HIDKeyboardTypes.h>

// Pin definitions for ESP32-C3 Supermini
#define MOTOR_PIN_1 0
#define MOTOR_PIN_2 8
#define BTN_UP 2      // TP223 touch sensor
#define BTN_DOWN 1    // TP223 touch sensor

// I2C pins
#define I2C_SDA 5
#define I2C_SCL 6

// Display offset
#define DISPLAY_OFFSET_X 28
#define DISPLAY_OFFSET_Y 24

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);
WebServer server(80);
Preferences preferences;

// BLE HID Keyboard
BLEHIDDevice* hid = nullptr;
BLECharacteristic* inputKeyboard = nullptr;
BLECharacteristic* outputKeyboard = nullptr;
BLEServer* pServer = nullptr;
bool bleConnected = false;
bool bleEnabled = true;

#define DEBOUNCE_TIME 50
int LONG_PRESS_TIME = 800;
int VIBRATION_DURATION = 30;
#define MAX_MENU_ITEMS 10

struct MenuItem {
  String name;
  String textContent;
};

MenuItem menuItems[MAX_MENU_ITEMS];
int menuItemCount = 0;

// Menu state
enum MenuMode {
  SPLASH_SCREEN,
  LOGIN_SCREEN,
  MAIN_MENU,
  SETTINGS_MENU,
  WIFI_MODE
};

MenuMode currentMode = SPLASH_SCREEN;
int currentItem = 0;
int settingsItem = 0;

const char* settingsOptions[] = {
  "BLE: ON",
  "WiFi Mode",
  "< Back"
};
const int settingsSize = 3;

unsigned long btnUpPressTime = 0;
unsigned long btnDownPressTime = 0;
bool btnUpPressed = false;
bool btnDownPressed = false;
bool btnUpLongHandled = false;
bool btnDownLongHandled = false;
bool webServerActive = false;

// Login sequence
int loginSequence[] = {1, 2, 1, 2};
int loginIndex = 0;
unsigned long lastLoginInput = 0;
#define LOGIN_TIMEOUT 2000

// Splash screen placeholder
const unsigned char splashImage[360] U8X8_PROGMEM = {
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbf, 
0xff, 0xff, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xdd, 0x55, 0xee, 0xee, 0xef, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xfb, 0x55, 0x55, 0x55, 0x55, 0x55, 0x57, 0x77, 0x77, 0x57, 0xbb, 0xbb, 0xbb, 
0xbb, 0xff, 0xff, 0xff, 0xff, 0xbf, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xfd, 0x7f, 0xef, 
0xff, 0xff, 0xfa, 0x0b, 0xff, 0xff, 0xfb, 0xff, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x75, 0x55, 
0x77, 0xbb, 0xbb, 0xbf, 0xff, 0xfa, 0x22, 0x22, 0x3a, 0xff, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 
0x55, 0x5d, 0xdf, 0xff, 0xff, 0xfe, 0x07, 0xf3, 0xaa, 0xa8, 0xff, 0xff, 0x55, 0x55, 0x57, 0x55, 
0x55, 0x55, 0x57, 0x77, 0x55, 0xbb, 0xff, 0xff, 0xe0, 0x2b, 0xba, 0xbf, 0xfb, 0xa2, 0x55, 0x55, 
0x55, 0x45, 0x55, 0x55, 0x55, 0x55, 0x55, 0xff, 0xff, 0xfe, 0x00, 0xaa, 0xab, 0xee, 0xaa, 0xaa, 
0x55, 0x55, 0x51, 0x11, 0x55, 0x55, 0x55, 0x55, 0x55, 0xbf, 0xff, 0x80, 0x00, 0x20, 0x3b, 0xbb, 
0xba, 0xaa, 0x55, 0x55, 0x44, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xaa, 0xa8, 0x00, 0x00, 0x0a, 
0x0a, 0xa0, 0xaa, 0xaa, 0x55, 0x51, 0x11, 0x15, 0x55, 0x55, 0x51, 0x55, 0x55, 0x20, 0x00, 0x00, 
0x22, 0x20, 0x20, 0x00, 0x20, 0x00, 0x40, 0x40, 0x45, 0x55, 0x55, 0x54, 0x44, 0x55, 0x55, 0x00, 
0x00, 0x02, 0xa0, 0x00, 0x80, 0x00, 0x00, 0x00, 0x15, 0x11, 0x15, 0x51, 0x15, 0x55, 0x11, 0x11, 
0x11, 0x02, 0xa0, 0x2a, 0x00, 0x00, 0x22, 0x20, 0x00, 0x00, 0x55, 0x55, 0x55, 0x44, 0x00, 0x55, 
0x54, 0x55, 0x55, 0x2a, 0xaa, 0x80, 0x00, 0x00, 0x0a, 0x88, 0x00, 0x88, 0x55, 0x55, 0x11, 0x11, 
0x11, 0x15, 0x55, 0x51, 0x55, 0xbb, 0xa0, 0x00, 0x00, 0x00, 0x22, 0x00, 0x20, 0x22, 0x55, 0x54, 
0x55, 0x45, 0x55, 0x45, 0x55, 0x55, 0x55, 0xa8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x51, 0x11, 0x11, 0x11, 0x11, 0x15, 0x55, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 
0x00, 0x00, 0x44, 0x00, 0x44, 0x44, 0x05, 0x55, 0x55, 0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x08, 0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x15, 0x55, 0x55, 0x51, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x04, 0x41, 0x44, 0x15, 0x55, 0x54, 0x44, 0x45, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// BLE Callbacks
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleConnected = true;
    Serial.println("BLE Connected - stable connection");
  }
  
  void onDisconnect(BLEServer* pServer) {
    bleConnected = false;
    Serial.println("BLE Disconnected");
  }
};

// Standard HID Report Descriptor
const uint8_t reportMap[] = {
  0x05, 0x01,  // Usage Page (Generic Desktop)
  0x09, 0x06,  // Usage (Keyboard)
  0xA1, 0x01,  // Collection (Application)
  0x85, 0x01,  //   Report ID (1)
  0x05, 0x07,  //   Usage Page (Keyboard/Keypad)
  0x19, 0xE0,  //   Usage Minimum (Keyboard Left Control)
  0x29, 0xE7,  //   Usage Maximum (Keyboard Right GUI)
  0x15, 0x00,  //   Logical Minimum (0)
  0x25, 0x01,  //   Logical Maximum (1)
  0x75, 0x01,  //   Report Size (1)
  0x95, 0x08,  //   Report Count (8)
  0x81, 0x02,  //   Input (Data, Variable, Absolute)
  0x95, 0x01,  //   Report Count (1)
  0x75, 0x08,  //   Report Size (8)
  0x81, 0x01,  //   Input (Constant)
  0x95, 0x05,  //   Report Count (5)
  0x75, 0x01,  //   Report Size (1)
  0x05, 0x08,  //   Usage Page (LEDs)
  0x19, 0x01,  //   Usage Minimum (Num Lock)
  0x29, 0x05,  //   Usage Maximum (Kana)
  0x91, 0x02,  //   Output (Data, Variable, Absolute)
  0x95, 0x01,  //   Report Count (1)
  0x75, 0x03,  //   Report Size (3)
  0x91, 0x01,  //   Output (Constant)
  0x95, 0x06,  //   Report Count (6)
  0x75, 0x08,  //   Report Size (8)
  0x15, 0x00,  //   Logical Minimum (0)
  0x25, 0x65,  //   Logical Maximum (101)
  0x05, 0x07,  //   Usage Page (Keyboard/Keypad)
  0x19, 0x00,  //   Usage Minimum (0)
  0x29, 0x65,  //   Usage Maximum (101)
  0x81, 0x00,  //   Input (Data, Array)
  0xC0         // End Collection
};

uint8_t getHIDCode(char c) {
  if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
  if (c >= 'A' && c <= 'Z') return 0x04 + (c - 'A');
  if (c >= '1' && c <= '9') return 0x1E + (c - '1');
  if (c == '0') return 0x27;
  if (c == ' ') return 0x2C;
  if (c == '\n' || c == '\r') return 0x28;
  if (c == '.') return 0x37;
  if (c == ',') return 0x36;
  if (c == '!') return 0x1E;
  if (c == '@') return 0x1F;
  if (c == '#') return 0x20;
  if (c == '$') return 0x21;
  if (c == '%') return 0x22;
  if (c == '-') return 0x2D;
  if (c == '_') return 0x2D;
  if (c == '/') return 0x38;
  if (c == ':') return 0x33;
  if (c == ';') return 0x33;
  return 0;
}

bool needsShift(char c) {
  if (c >= 'A' && c <= 'Z') return true;
  if (c == '!' || c == '@' || c == '#' || c == '$' || c == '%') return true;
  if (c == '^' || c == '&' || c == '*' || c == '(' || c == ')') return true;
  if (c == '_' || c == '+' || c == '{' || c == '}' || c == '|') return true;
  if (c == ':' || c == '"' || c == '<' || c == '>' || c == '?') return true;
  if (c == '~') return true;
  return false;
}

void sendKeyReport(uint8_t modifier, uint8_t key) {
  if (!bleConnected || inputKeyboard == nullptr) {
    return;
  }
  
  // FIXED: Don't include report ID in the data
  uint8_t report[8] = {modifier, 0x00, key, 0x00, 0x00, 0x00, 0x00, 0x00};
  inputKeyboard->setValue(report, sizeof(report));
  inputKeyboard->notify();
}

void typeChar(char c) {
  if (!bleConnected) return;
  
  uint8_t code = getHIDCode(c);
  if (code == 0) return;
  
  uint8_t modifier = needsShift(c) ? 0x02 : 0x00;
  
  sendKeyReport(modifier, code);
  delay(50);
  sendKeyReport(0x00, 0x00);
  delay(50);
}

void typeString(String text) {
  if (!bleEnabled || !bleConnected) {
    Serial.println("BLE not available!");
    return;
  }
  
  for (int i = 0; i < text.length(); i++) {
    typeChar(text[i]);
  }
}

void initBLE() {
  if (!bleEnabled) return;
  
  Serial.println("=== Init BLE - NO AUTH ===");
  
  BLEDevice::init("ESP32-Keys");
  
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  hid = new BLEHIDDevice(pServer);
  inputKeyboard = hid->inputReport(1);
  outputKeyboard = hid->outputReport(1);
  
  hid->manufacturer()->setValue("ESP32");
  hid->pnp(0x02, 0x05ac, 0x820a, 0x0100);
  hid->hidInfo(0x00, 0x01);
  
  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_NO_BOND);
  pSecurity->setCapability(ESP_IO_CAP_NONE);
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  
  hid->reportMap((uint8_t*)reportMap, sizeof(reportMap));
  hid->startServices();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->setAppearance(HID_KEYBOARD);
  pAdvertising->addServiceUUID(hid->hidService()->getUUID());
  pAdvertising->start();
  
  Serial.println("BLE Ready - NO PAIRING NEEDED");
}

void toggleBLE() {
  bleEnabled = !bleEnabled;
  saveSettings();
  
  u8g2.clearBuffer();
  
  if (bleEnabled) {
    u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 10, "Enabling");
    u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 20, "BLE...");
    u8g2.sendBuffer();
    delay(500);
    
    initBLE();
    
    u8g2.clearBuffer();
    u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 15, "BLE ON");
    u8g2.sendBuffer();
    vibrate(100);
    delay(1000);
  } else {
    u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 10, "Disabling");
    u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 20, "BLE...");
    u8g2.sendBuffer();
    delay(500);
    
    if (bleConnected && pServer) {
      pServer->disconnect(pServer->getConnId());
      delay(100);
    }
    
    if (BLEDevice::getAdvertising()) {
      BLEDevice::getAdvertising()->stop();
      delay(100);
    }
    
    BLEDevice::deinit(true);
    
    bleConnected = false;
    hid = nullptr;
    inputKeyboard = nullptr;
    outputKeyboard = nullptr;
    pServer = nullptr;
    
    u8g2.clearBuffer();
    u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 15, "BLE OFF");
    u8g2.sendBuffer();
    vibrate(100);
    delay(1000);
  }
  
  drawMenu();
}

const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Shortcuts</title>
  <style>
    body { font-family: Arial; max-width: 800px; margin: 20px auto; padding: 20px; background: #f0f0f0; }
    .container { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; text-align: center; }
    h2 { color: #555; border-bottom: 2px solid #4CAF50; padding-bottom: 10px; margin-top: 30px; }
    .setting { margin: 20px 0; padding: 15px; background: #f9f9f9; border-radius: 5px; }
    label { display: block; margin-bottom: 5px; font-weight: bold; color: #555; }
    input[type="range"] { width: 100%; margin: 10px 0; }
    input[type="text"], textarea { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; font-family: monospace; }
    textarea { min-height: 60px; resize: vertical; }
    .value-display { text-align: center; color: #666; font-size: 18px; margin: 5px 0; }
    button { background: #4CAF50; color: white; padding: 12px 30px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; width: 100%; margin-top: 10px; }
    button:hover { background: #45a049; }
    button.danger { background: #f44336; }
    button.danger:hover { background: #da190b; }
    .status { text-align: center; padding: 10px; margin: 10px 0; border-radius: 5px; display: none; }
    .status.success { background: #d4edda; color: #155724; display: block; }
    .note { background: #fff3cd; padding: 10px; border-radius: 5px; margin: 15px 0; color: #856404; text-align: center; }
    .info-box { background: #d1ecf1; padding: 15px; border-radius: 5px; margin: 15px 0; color: #0c5460; }
    .menu-item { background: white; border: 2px solid #ddd; border-radius: 5px; padding: 15px; margin: 15px 0; }
    .menu-item-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
    .menu-number { background: #4CAF50; color: white; padding: 5px 10px; border-radius: 3px; font-weight: bold; }
    .delete-btn { background: #f44336; color: white; border: none; padding: 5px 15px; border-radius: 3px; cursor: pointer; }
    .add-btn { background: #2196F3; margin-top: 20px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>⚙️ ESP32 Shortcuts</h1>
    <div class="note">Long press DOWN to exit WiFi mode</div>
    <div class="info-box"><strong>📱 Connect:</strong> Look for "ESP32-Keys" in Bluetooth - NO PIN REQUIRED</div>
    
    <h2>⏱️ System Settings</h2>
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
    
    <h2>⌨️ Keyboard Shortcuts</h2>
    <div class="note">Test with simple text first!</div>
    <div id="menu-items-container"></div>
    <button class="add-btn" onclick="addMenuItem()">➕ Add Shortcut</button>
    
    <h2>💾 Save</h2>
    <button onclick="saveSettings()">💾 Save All Settings</button>
    <div class="status" id="status"></div>
    <button onclick="testVibration()" style="background: #2196F3; margin-top: 20px;">📳 Test Vibration</button>
    <button onclick="getSettings()" style="background: #ff9800;">🔄 Reload</button>
    <button onclick="factoryReset()" class="danger" style="margin-top: 20px;">⚠️ Factory Reset</button>
  </div>

  <script>
    let menuItemsData = [];
    window.onload = () => getSettings();
    
    function updateValue(id) {
      const slider = document.getElementById(id);
      document.getElementById(id + '-value').textContent = slider.value + ' ms';
    }
    
    function getSettings() {
      fetch('/get').then(r => r.json()).then(data => {
        document.getElementById('vibration').value = data.vibration;
        document.getElementById('longpress').value = data.longpress;
        updateValue('vibration');
        updateValue('longpress');
        menuItemsData = data.items || [];
        renderMenuItems();
      });
    }
    
    function renderMenuItems() {
      const container = document.getElementById('menu-items-container');
      container.innerHTML = '';
      menuItemsData.forEach((item, index) => {
        const div = document.createElement('div');
        div.className = 'menu-item';
        div.innerHTML = `
          <div class="menu-item-header">
            <span class="menu-number">Shortcut ${index + 1}</span>
            <button class="delete-btn" onclick="deleteMenuItem(${index})">🗑️</button>
          </div>
          <label>Display Name (max 10 chars)</label>
          <input type="text" id="item-name-${index}" value="${item.name}" maxlength="10">
          <label style="margin-top: 10px;">Text to Type</label>
          <textarea id="item-text-${index}">${item.text}</textarea>
        `;
        container.appendChild(div);
      });
    }
    
    function addMenuItem() {
      if (menuItemsData.length >= 9) { alert('Max 9 shortcuts!'); return; }
      menuItemsData.push({name: 'New', text: ''});
      renderMenuItems();
    }
    
    function deleteMenuItem(index) {
      if (confirm('Delete?')) { menuItemsData.splice(index, 1); renderMenuItems(); }
    }
    
    function saveSettings() {
      const items = [];
      menuItemsData.forEach((item, index) => {
        items.push({
          name: document.getElementById(`item-name-${index}`).value,
          text: document.getElementById(`item-text-${index}`).value
        });
      });
      fetch('/save', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          vibration: document.getElementById('vibration').value,
          longpress: document.getElementById('longpress').value,
          items: items
        })
      }).then(() => {
        const status = document.getElementById('status');
        status.className = 'status success';
        status.textContent = '✓ Saved!';
        setTimeout(() => status.style.display = 'none', 3000);
      });
    }
    
    function testVibration() { fetch('/vibrate'); }
    function factoryReset() {
      if (confirm('Reset all?')) fetch('/reset', {method: 'POST'}).then(() => alert('Reset! Restarting...'));
    }
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP32 BLE Keyboard - TP223 Touch ===");
  
  pinMode(MOTOR_PIN_1, OUTPUT);
  pinMode(MOTOR_PIN_2, OUTPUT);
  pinMode(BTN_UP, INPUT);      // TP223 - no pull-up needed
  pinMode(BTN_DOWN, INPUT);    // TP223 - no pull-up needed
  digitalWrite(MOTOR_PIN_1, LOW);
  digitalWrite(MOTOR_PIN_2, LOW);
  
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tr);
  
  loadSettings();
  
  showSplashScreen();
  
  currentMode = LOGIN_SCREEN;
  drawLoginScreen();
  
  if (bleEnabled) {
    initBLE();
  }
}

void loop() {
  if (webServerActive) {
    server.handleClient();
  }
  
  // Monitor connection and restart advertising if needed
  if (bleEnabled && !bleConnected && !webServerActive) {
    static unsigned long lastAdvCheck = 0;
    if (millis() - lastAdvCheck > 30000) {
      lastAdvCheck = millis();
      Serial.println("Restarting advertising...");
      BLEDevice::startAdvertising();
    }
  }
  
  if (currentMode == LOGIN_SCREEN) {
    handleLoginButtons();
  } else {
    handleButtons();
  }
}

void showSplashScreen() {
  u8g2.clearBuffer();
  u8g2.drawXBMP(DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 72, 40, splashImage);
  u8g2.sendBuffer();
  vibrate(100);
  delay(3000);
}

void drawLoginScreen() {
  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 8, "== LOGIN ==");
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 20, "Enter:");
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 30, "U-D-U-D");
  
  String progress = "";
  for (int i = 0; i < loginIndex; i++) {
    progress += loginSequence[i] == 1 ? "U" : "D";
    if (i < loginIndex - 1) progress += "-";
  }
  if (progress.length() > 0) {
    u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 40, progress.c_str());
  }
  
  u8g2.sendBuffer();
}

void handleLoginButtons() {
  bool upState = digitalRead(BTN_UP) == HIGH;    // TP223 outputs HIGH when touched
  bool downState = digitalRead(BTN_DOWN) == HIGH;
  
  if (loginIndex > 0 && millis() - lastLoginInput > LOGIN_TIMEOUT) {
    loginIndex = 0;
    vibrate(200);
    drawLoginScreen();
  }
  
  if (upState && !btnUpPressed) {
    btnUpPressed = true;
    delay(DEBOUNCE_TIME);
    
    if (loginSequence[loginIndex] == 1) {
      loginIndex++;
      lastLoginInput = millis();
      vibrate(50);
      
      if (loginIndex >= 4) {
        vibrate(100);
        delay(100);
        vibrate(100);
        currentMode = MAIN_MENU;
        drawMenu();
      } else {
        drawLoginScreen();
      }
    } else {
      loginIndex = 0;
      vibrate(200);
      drawLoginScreen();
    }
  }
  
  if (!upState) {
    btnUpPressed = false;
  }
  
  if (downState && !btnDownPressed) {
    btnDownPressed = true;
    delay(DEBOUNCE_TIME);
    
    if (loginSequence[loginIndex] == 2) {
      loginIndex++;
      lastLoginInput = millis();
      vibrate(50);
      
      if (loginIndex >= 4) {
        vibrate(100);
        delay(100);
        vibrate(100);
        currentMode = MAIN_MENU;
        drawMenu();
      } else {
        drawLoginScreen();
      }
    } else {
      loginIndex = 0;
      vibrate(200);
      drawLoginScreen();
    }
  }
  
  if (!downState) {
    btnDownPressed = false;
  }
}

void loadSettings() {
  preferences.begin("menu-settings", false);
  VIBRATION_DURATION = preferences.getInt("vibration", 30);
  LONG_PRESS_TIME = preferences.getInt("longpress", 800);
  bleEnabled = preferences.getBool("bleEnabled", true);
  menuItemCount = preferences.getInt("itemCount", 0);
  
  if (menuItemCount == 0) {
    menuItemCount = 4;
    menuItems[0] = {"Test", "hello"};
    menuItems[1] = {"Email", "test@test.com"};
    menuItems[2] = {"URL", "example.com"};
    menuItems[3] = {"ABC", "abc123"};
  } else {
    for (int i = 0; i < menuItemCount && i < MAX_MENU_ITEMS - 1; i++) {
      menuItems[i].name = preferences.getString(("name" + String(i)).c_str(), "Item");
      menuItems[i].textContent = preferences.getString(("text" + String(i)).c_str(), "");
    }
  }
  
  menuItems[menuItemCount] = {"Settings", ""};
  preferences.end();
}

void saveSettings() {
  preferences.begin("menu-settings", false);
  preferences.putInt("vibration", VIBRATION_DURATION);
  preferences.putInt("longpress", LONG_PRESS_TIME);
  preferences.putBool("bleEnabled", bleEnabled);
  preferences.putInt("itemCount", menuItemCount);
  
  for (int i = 0; i < menuItemCount; i++) {
    preferences.putString(("name" + String(i)).c_str(), menuItems[i].name);
    preferences.putString(("text" + String(i)).c_str(), menuItems[i].textContent);
  }
  preferences.end();
}

void vibrate(int duration) {
  digitalWrite(MOTOR_PIN_1, HIGH);
  digitalWrite(MOTOR_PIN_2, HIGH);
  delay(duration);
  digitalWrite(MOTOR_PIN_1, LOW);
  digitalWrite(MOTOR_PIN_2, LOW);
}

void handleButtons() {
  bool upState = digitalRead(BTN_UP) == HIGH;    // TP223 outputs HIGH when touched
  bool downState = digitalRead(BTN_DOWN) == HIGH;
  
  if (upState && !btnUpPressed) {
    btnUpPressed = true;
    btnUpPressTime = millis();
    btnUpLongHandled = false;
  }
  
  if (upState && btnUpPressed && !btnUpLongHandled && millis() - btnUpPressTime >= LONG_PRESS_TIME) {
    btnUpLongHandled = true;
    selectItem();
  }
  
  if (!upState && btnUpPressed) {
    if (!btnUpLongHandled && millis() - btnUpPressTime >= DEBOUNCE_TIME) {
      scrollUp();
    }
    btnUpPressed = false;
  }
  
  if (downState && !btnDownPressed) {
    btnDownPressed = true;
    btnDownPressTime = millis();
    btnDownLongHandled = false;
  }
  
  if (downState && btnDownPressed && !btnDownLongHandled && millis() - btnDownPressTime >= LONG_PRESS_TIME) {
    btnDownLongHandled = true;
    goBack();
  }
  
  if (!downState && btnDownPressed) {
    if (!btnDownLongHandled && millis() - btnDownPressTime >= DEBOUNCE_TIME) {
      scrollDown();
    }
    btnDownPressed = false;
  }
}

void scrollUp() {
  vibrate(VIBRATION_DURATION);
  
  if (currentMode == MAIN_MENU) {
    currentItem = (currentItem == 0) ? menuItemCount : currentItem - 1;
  } else if (currentMode == SETTINGS_MENU) {
    settingsItem = (settingsItem == 0) ? settingsSize - 1 : settingsItem - 1;
  }
  
  drawMenu();
}

void scrollDown() {
  vibrate(VIBRATION_DURATION);
  
  if (currentMode == MAIN_MENU) {
    currentItem = (currentItem == menuItemCount) ? 0 : currentItem + 1;
  } else if (currentMode == SETTINGS_MENU) {
    settingsItem = (settingsItem == settingsSize - 1) ? 0 : settingsItem + 1;
  }
  
  drawMenu();
}

void selectItem() {
  vibrate(100);
  
  if (currentMode == MAIN_MENU) {
    if (currentItem == menuItemCount) {
      currentMode = SETTINGS_MENU;
      settingsItem = 0;
      drawMenu();
      return;
    }
    
    u8g2.clearBuffer();
    u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 10, "SELECT:");
    u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 25, menuItems[currentItem].name.c_str());
    u8g2.sendBuffer();
    delay(500);
    
    if (menuItems[currentItem].textContent.length() > 0) {
      if (bleEnabled && bleConnected) {
        u8g2.clearBuffer();
        u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 15, "Typing...");
        u8g2.sendBuffer();
        
        typeString(menuItems[currentItem].textContent);
        
        vibrate(50);
        delay(500);
      } else if (bleEnabled && !bleConnected) {
        u8g2.clearBuffer();
        u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 10, "BLE Not");
        u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 20, "Connected!");
        u8g2.sendBuffer();
        vibrate(200);
        delay(1500);
      } else {
        u8g2.clearBuffer();
        u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 10, "BLE is");
        u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 20, "Disabled!");
        u8g2.sendBuffer();
        vibrate(200);
        delay(1500);
      }
    }
    
    drawMenu();
  } 
  else if (currentMode == SETTINGS_MENU) {
    if (settingsItem == 0) {
      toggleBLE();
    } else if (settingsItem == 1) {
      startWebServer();
    } else if (settingsItem == 2) {
      currentMode = MAIN_MENU;
      drawMenu();
    }
  }
}

void goBack() {
  vibrate(50);
  
  if (currentMode == WIFI_MODE && webServerActive) {
    stopWebServer();
    return;
  }
  
  if (currentMode == SETTINGS_MENU) {
    currentMode = MAIN_MENU;
    drawMenu();
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
  
  if (currentMode == MAIN_MENU) {
    u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 8, "== MENU ==");
    
    int totalItems = menuItemCount + 1;
    int startItem = max(0, currentItem - 1);
    int endItem = min(totalItems, startItem + 3);
    
    int yPos = DISPLAY_OFFSET_Y + 24;
    for (int i = startItem; i < endItem; i++) {
      int xPos = DISPLAY_OFFSET_X + 2;
      if (i == currentItem) {
        u8g2.drawBox(xPos, yPos - 8, 68, 10);
        u8g2.setDrawColor(0);
        u8g2.drawStr(xPos + 2, yPos, ">");
        u8g2.drawStr(xPos + 10, yPos, menuItems[i].name.c_str());
        u8g2.setDrawColor(1);
      } else {
        u8g2.drawStr(xPos + 10, yPos, menuItems[i].name.c_str());
      }
      yPos += 10;
    }
  } 
  else if (currentMode == SETTINGS_MENU) {
    u8g2.drawStr(DISPLAY_OFFSET_X + 2, DISPLAY_OFFSET_Y + 8, "= SETTINGS =");
    
    int startItem = max(0, settingsItem - 1);
    int endItem = min(settingsSize, startItem + 3);
    
    int yPos = DISPLAY_OFFSET_Y + 24;
    for (int i = startItem; i < endItem; i++) {
      int xPos = DISPLAY_OFFSET_X + 2;
      
      String itemText = settingsOptions[i];
      if (i == 0) {
        itemText = bleEnabled ? "BLE: ON" : "BLE: OFF";
      }
      
      if (i == settingsItem) {
        u8g2.drawBox(xPos, yPos - 8, 68, 10);
        u8g2.setDrawColor(0);
        u8g2.drawStr(xPos + 2, yPos, ">");
        u8g2.drawStr(xPos + 10, yPos, itemText.c_str());
        u8g2.setDrawColor(1);
      } else {
        u8g2.drawStr(xPos + 10, yPos, itemText.c_str());
      }
      yPos += 10;
    }
  }
  
  u8g2.sendBuffer();
}

void startWebServer() {
  currentMode = WIFI_MODE;
  
  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 10, "Starting");
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 20, "WiFi AP...");
  u8g2.sendBuffer();
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-Shortcuts", "12345678");
  
  IPAddress IP = WiFi.softAPIP();
  
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", htmlPage); });
  
  server.on("/get", HTTP_GET, []() {
    StaticJsonDocument<2048> doc;
    doc["vibration"] = VIBRATION_DURATION;
    doc["longpress"] = LONG_PRESS_TIME;
    JsonArray items = doc.createNestedArray("items");
    for (int i = 0; i < menuItemCount; i++) {
      JsonObject item = items.createNestedObject();
      item["name"] = menuItems[i].name;
      item["text"] = menuItems[i].textContent;
    }
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  server.on("/save", HTTP_POST, []() {
    StaticJsonDocument<2048> doc;
    deserializeJson(doc, server.arg("plain"));
    VIBRATION_DURATION = doc["vibration"];
    LONG_PRESS_TIME = doc["longpress"];
    JsonArray items = doc["items"];
    menuItemCount = min((int)items.size(), MAX_MENU_ITEMS - 1);
    for (int i = 0; i < menuItemCount; i++) {
      menuItems[i].name = items[i]["name"].as<String>();
      menuItems[i].textContent = items[i]["text"].as<String>();
    }
    saveSettings();
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/vibrate", HTTP_GET, []() { vibrate(VIBRATION_DURATION); server.send(200, "text/plain", "OK"); });
  server.on("/reset", HTTP_POST, []() {
    preferences.begin("menu-settings", false);
    preferences.clear();
    preferences.end();
    server.send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
  });
  
  server.begin();
  webServerActive = true;
  
  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 8, "WiFi AP:");
  u8g2.drawStr(DISPLAY_OFFSET_X + 2, DISPLAY_OFFSET_Y + 20, IP.toString().c_str());
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 32, "Long:EXIT");
  u8g2.sendBuffer();
}

void stopWebServer() {
  server.stop();
  server.close();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  webServerActive = false;
  
  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 15, "WiFi OFF");
  u8g2.sendBuffer();
  delay(1000);
  
  currentMode = SETTINGS_MENU;
  drawMenu();
}