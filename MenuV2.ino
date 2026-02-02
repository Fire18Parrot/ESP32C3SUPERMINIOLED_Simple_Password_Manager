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
#define BTN_UP 1
#define BTN_DOWN 2

// I2C pins
#define I2C_SDA 5
#define I2C_SCL 6

// Display offset
#define DISPLAY_OFFSET_X 28
#define DISPLAY_OFFSET_Y 24

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);
WebServer server(80);
Preferences preferences;

// BLE HID Keyboard
BLEHIDDevice* hid;
BLECharacteristic* input;
BLECharacteristic* output;
BLEServer* pServer;
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
  MAIN_MENU,
  SETTINGS_MENU,
  WIFI_MODE
};

MenuMode currentMode = MAIN_MENU;
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

// BLE Callbacks
class BLEConnectionCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleConnected = true;
    Serial.println("BLE Connected");
  }
  
  void onDisconnect(BLEServer* pServer) {
    bleConnected = false;
    Serial.println("BLE Disconnected");
    if (bleEnabled) {
      BLEDevice::startAdvertising();
    }
  }
};

// HID Report Descriptor for Keyboard
static const uint8_t _hidReportDescriptor[] = {
  0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
  0x09, 0x06,                    // USAGE (Keyboard)
  0xa1, 0x01,                    // COLLECTION (Application)
  0x85, 0x01,                    //   REPORT_ID (1)
  0x05, 0x07,                    //   USAGE_PAGE (Keyboard)
  0x19, 0xe0,                    //   USAGE_MINIMUM (Keyboard LeftControl)
  0x29, 0xe7,                    //   USAGE_MAXIMUM (Keyboard Right GUI)
  0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
  0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
  0x75, 0x01,                    //   REPORT_SIZE (1)
  0x95, 0x08,                    //   REPORT_COUNT (8)
  0x81, 0x02,                    //   INPUT (Data,Var,Abs)
  0x95, 0x01,                    //   REPORT_COUNT (1)
  0x75, 0x08,                    //   REPORT_SIZE (8)
  0x81, 0x03,                    //   INPUT (Cnst,Var,Abs)
  0x95, 0x06,                    //   REPORT_COUNT (6)
  0x75, 0x08,                    //   REPORT_SIZE (8)
  0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
  0x25, 0x65,                    //   LOGICAL_MAXIMUM (101)
  0x05, 0x07,                    //   USAGE_PAGE (Keyboard)
  0x19, 0x00,                    //   USAGE_MINIMUM (Reserved (no event indicated))
  0x29, 0x65,                    //   USAGE_MAXIMUM (Keyboard Application)
  0x81, 0x00,                    //   INPUT (Data,Ary,Abs)
  0xc0                           // END_COLLECTION
};

void sendKeyboardReport(uint8_t modifier, uint8_t key) {
  if (!bleEnabled || !bleConnected) return;

  uint8_t report[] = {modifier, 0x00, key, 0x00, 0x00, 0x00, 0x00, 0x00};
  input->setValue(report, sizeof(report));
  input->notify();
  delay(10);

  // Release key
  uint8_t release[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  input->setValue(release, sizeof(release));
  input->notify();
  delay(10);
}

void typeString(String text) {
  if (!bleEnabled || !bleConnected) {
    Serial.println("BLE not available!");
    return;
  }
  
  for (int i = 0; i < text.length(); i++) {
    char c = text[i];
    uint8_t key = 0;
    uint8_t modifier = 0;
    
    // Simple character to HID keycode mapping
    if (c >= 'a' && c <= 'z') {
      key = 0x04 + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
      key = 0x04 + (c - 'A');
      modifier = 0x02; // Left shift
    } else if (c >= '1' && c <= '9') {
      key = 0x1E + (c - '1');
    } else if (c == '0') {
      key = 0x27;
    } else if (c == ' ') {
      key = 0x2C;
    } else if (c == '\n') {
      key = 0x28;
    } else if (c == '.') {
      key = 0x37;
    } else if (c == '@') {
      key = 0x1F;
      modifier = 0x02; // Shift + 2
    } else if (c == '-') {
      key = 0x2D;
    } else if (c == '_') {
      key = 0x2D;
      modifier = 0x02; // Shift + -
    }
    
    if (key != 0) {
      sendKeyboardReport(modifier, key);
    }
  }
}

void initBLE() {
  if (!bleEnabled) return;
  
  BLEDevice::init("ESP32 Shortcuts");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BLEConnectionCallbacks());

  hid = new BLEHIDDevice(pServer);
  input = hid->inputReport(1);
  output = hid->outputReport(1);
  input->addDescriptor(new BLE2902());
  output->addDescriptor(new BLE2902());

  hid->manufacturer()->setValue("ESP32");
  hid->pnp(0x02, 0xe502, 0xa111, 0x0210);
  hid->hidInfo(0x00, 0x01);

  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);

  hid->reportMap((uint8_t*)_hidReportDescriptor, sizeof(_hidReportDescriptor));
  hid->startServices();

  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->setAppearance(HID_KEYBOARD);
  pAdvertising->addServiceUUID(hid->hidService()->getUUID());
  pAdvertising->start();

  Serial.println("BLE Keyboard Started");
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
    
    // Properly stop and deinitialize BLE
    if (bleConnected) {
      // Disconnect any connected devices first
      pServer->disconnect(pServer->getConnId());
      delay(100);
    }
    
    // Stop advertising
    BLEDevice::getAdvertising()->stop();
    delay(100);
    
    // Deinitialize BLE completely - this frees all BLE resources
    BLEDevice::deinit(true);
    
    bleConnected = false;
    
    u8g2.clearBuffer();
    u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 15, "BLE OFF");
    u8g2.sendBuffer();
    vibrate(100);
    delay(1000);
    
    Serial.println("BLE Completely Disabled");
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
    <div class="info-box"><strong>📱 Bluetooth:</strong> Connect to "ESP32 Shortcuts" when BLE is enabled</div>
    
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
    <div class="note">Create shortcuts that type text via Bluetooth keyboard</div>
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
  
  pinMode(MOTOR_PIN_1, OUTPUT);
  pinMode(MOTOR_PIN_2, OUTPUT);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  digitalWrite(MOTOR_PIN_1, LOW);
  digitalWrite(MOTOR_PIN_2, LOW);
  
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tr);
  
  loadSettings();
  
  if (bleEnabled) {
    initBLE();
  }
  
  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 15, DISPLAY_OFFSET_Y + 15, "READY!");
  u8g2.sendBuffer();
  vibrate(100);
  delay(1000);
  
  drawMenu();
}

void loop() {
  if (webServerActive) server.handleClient();
  handleButtons();
}

void loadSettings() {
  preferences.begin("menu-settings", false);
  VIBRATION_DURATION = preferences.getInt("vibration", 30);
  LONG_PRESS_TIME = preferences.getInt("longpress", 800);
  bleEnabled = preferences.getBool("bleEnabled", true);
  menuItemCount = preferences.getInt("itemCount", 0);
  
  if (menuItemCount == 0) {
    menuItemCount = 4;
    menuItems[0] = {"Email", "your@email.com"};
    menuItems[1] = {"Password", "MyPass123"};
    menuItems[2] = {"URL", "github.com"};
    menuItems[3] = {"Hello", "Hello World"};
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
  bool upState = digitalRead(BTN_UP) == LOW;
  bool downState = digitalRead(BTN_DOWN) == LOW;
  
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
    // Main menu selection
    if (currentItem == menuItemCount) {
      // Enter settings menu
      currentMode = SETTINGS_MENU;
      settingsItem = 0;
      drawMenu();
      return;
    }
    
    // Regular shortcut selection
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
        delay(200);
        
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
    // Settings menu selection
    if (settingsItem == 0) {
      // Toggle BLE
      toggleBLE();
    } else if (settingsItem == 1) {
      // Enter WiFi mode
      startWebServer();
    } else if (settingsItem == 2) {
      // Back to main menu
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
    // Draw main menu
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
    // Draw settings menu
    u8g2.drawStr(DISPLAY_OFFSET_X + 2, DISPLAY_OFFSET_Y + 8, "= SETTINGS =");
    
    int startItem = max(0, settingsItem - 1);
    int endItem = min(settingsSize, startItem + 3);
    
    int yPos = DISPLAY_OFFSET_Y + 24;
    for (int i = startItem; i < endItem; i++) {
      int xPos = DISPLAY_OFFSET_X + 2;
      
      // Update BLE status text dynamically
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
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 20, "WiFi...");
  u8g2.sendBuffer();
  
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.softAP("ESP32-Menu", "12345678");
  }
  
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
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 8, "Web UI:");
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  u8g2.drawStr(DISPLAY_OFFSET_X + 2, DISPLAY_OFFSET_Y + 20, ip.c_str());
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 32, "Long:EXIT");
  u8g2.sendBuffer();
}

void stopWebServer() {
  server.stop();
  server.close();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  webServerActive = false;
  
  u8g2.clearBuffer();
  u8g2.drawStr(DISPLAY_OFFSET_X + 5, DISPLAY_OFFSET_Y + 15, "WiFi OFF");
  u8g2.sendBuffer();
  delay(1000);
  
  currentMode = SETTINGS_MENU;
  drawMenu();
}