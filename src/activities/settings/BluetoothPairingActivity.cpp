#include "BluetoothPairingActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>

#include <algorithm>

#include "BluetoothManager.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "PairedDeviceStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

void BluetoothPairingActivity::taskTrampoline(void* param) {
  auto* self = static_cast<BluetoothPairingActivity*>(param);
  self->displayTaskLoop();
}

void BluetoothPairingActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();
  updateRequired = true;

  PAIRED_DEVICES.loadFromFile();

  // Ensure BLE is initialized
  if (!BLUETOOTH_MANAGER.isInitialized()) {
    BLUETOOTH_MANAGER.initialize();
  }

  xTaskCreate(&BluetoothPairingActivity::taskTrampoline, "BTPairingTask", 4096, this, 1, &displayTaskHandle);

  startScan();
}

void BluetoothPairingActivity::onExit() {
  Activity::onExit();

  BLUETOOTH_MANAGER.stopScan();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void BluetoothPairingActivity::startScan() {
  state = State::SCANNING;
  stateStartTime = millis();
  deviceList.clear();
  selectedIndex = 0;
  updateRequired = true;

  BLUETOOTH_MANAGER.startScan();
}

void BluetoothPairingActivity::processScanResults() {
#ifdef CONFIG_BT_ENABLED
  auto results = BLUETOOTH_MANAGER.getScanResults();
  deviceList.clear();

  for (int i = 0; i < results.getCount(); i++) {
    const auto* dev = results.getDevice(i);
    if (!dev || !dev->isConnectable()) continue;

    DiscoveredDevice discovered;
    discovered.name = dev->haveName() ? dev->getName() : "(Unknown)";
    discovered.address = dev->getAddress().toString();
    discovered.addressType = dev->getAddress().getType();
    discovered.rssi = dev->getRSSI();
    discovered.isPaired = PAIRED_DEVICES.hasPairedDevice(discovered.address);

    // Prioritize devices advertising HID service
    bool isHid = dev->isAdvertisingService(NimBLEUUID((uint16_t)0x1812));

    // Include HID devices, paired devices, and devices with names
    if (isHid || discovered.isPaired || dev->haveName()) {
      deviceList.push_back(discovered);
    }
  }

  // Sort: paired first, then HID, then by signal strength
  std::sort(deviceList.begin(), deviceList.end(), [](const DiscoveredDevice& a, const DiscoveredDevice& b) {
    if (a.isPaired != b.isPaired) return a.isPaired;
    return a.rssi > b.rssi;
  });

  if (selectedIndex >= static_cast<int>(deviceList.size())) {
    selectedIndex = deviceList.empty() ? 0 : static_cast<int>(deviceList.size()) - 1;
  }
#endif
}

void BluetoothPairingActivity::selectDevice() {
  if (deviceList.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(deviceList.size())) {
    return;
  }

  const auto& device = deviceList[selectedIndex];
  attemptConnection(device.address, device.addressType, device.name);
}

void BluetoothPairingActivity::attemptConnection(const std::string& address, uint8_t addressType,
                                                  const std::string& name) {
  connectingAddress = address;
  connectingAddressType = addressType;
  connectingName = name;
  state = State::CONNECTING;
  stateStartTime = millis();
  updateRequired = true;
}

void BluetoothPairingActivity::loop() {
  switch (state) {
    case State::SCANNING: {
      // Poll scan completion
      if (!BLUETOOTH_MANAGER.isScanning()) {
        processScanResults();
        state = State::DEVICE_LIST;
        updateRequired = true;
      }
      break;
    }

    case State::DEVICE_LIST: {
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        BLUETOOTH_MANAGER.stopScan();
        onGoBack();
        return;
      }

      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        selectDevice();
        return;
      }

      // Left = rescan
      if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        startScan();
        return;
      }

      // Right on paired device = forget prompt
      if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        if (!deviceList.empty() && deviceList[selectedIndex].isPaired) {
          state = State::FORGET_PROMPT;
          updateRequired = true;
          return;
        }
      }

      // Navigation
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        if (!deviceList.empty()) {
          selectedIndex = (selectedIndex > 0) ? (selectedIndex - 1) : static_cast<int>(deviceList.size()) - 1;
          updateRequired = true;
        }
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        if (!deviceList.empty()) {
          selectedIndex =
              (selectedIndex < static_cast<int>(deviceList.size()) - 1) ? (selectedIndex + 1) : 0;
          updateRequired = true;
        }
      }
      break;
    }

    case State::CONNECTING: {
      unsigned long elapsed = millis() - stateStartTime;
      if (elapsed > CONNECTION_TIMEOUT_MS) {
        Serial.printf("[%lu] [BTPair] Connection timeout\n", millis());
        state = State::CONNECTION_FAILED;
        stateStartTime = millis();
        updateRequired = true;
        break;
      }

      bool success = BLUETOOTH_MANAGER.connectToDevice(connectingAddress, connectingAddressType);

      if (success) {
        PAIRED_DEVICES.addDevice(connectingName, connectingAddress, connectingAddressType);
        state = State::CONNECTED;
      } else {
        state = State::CONNECTION_FAILED;
      }
      stateStartTime = millis();
      updateRequired = true;
      break;
    }

    case State::CONNECTED: {
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
          mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        onGoBack();
        return;
      }
      break;
    }

    case State::CONNECTION_FAILED: {
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        // Retry
        startScan();
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        onGoBack();
        return;
      }
      break;
    }

    case State::FORGET_PROMPT: {
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        // Forget the device
        if (!deviceList.empty()) {
          const auto& dev = deviceList[selectedIndex];
          PAIRED_DEVICES.removeDevice(dev.address);

          // If currently connected to this device, disconnect
          if (BLUETOOTH_MANAGER.getConnectedDeviceAddress() == dev.address) {
            BLUETOOTH_MANAGER.disconnectDevice();
          }

          // Also remove NimBLE bond
#ifdef CONFIG_BT_ENABLED
          NimBLEDevice::deleteBond(NimBLEAddress(dev.address, dev.addressType));
#endif
        }
        startScan();
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        state = State::DEVICE_LIST;
        updateRequired = true;
        return;
      }
      break;
    }
  }
}

void BluetoothPairingActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void BluetoothPairingActivity::render() const {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Bluetooth", true, EpdFontFamily::BOLD);

  switch (state) {
    case State::SCANNING:
      renderScanning();
      break;
    case State::DEVICE_LIST:
      renderDeviceList();
      break;
    case State::CONNECTING:
      renderConnecting();
      break;
    case State::CONNECTED:
      renderConnected();
      break;
    case State::CONNECTION_FAILED:
      renderConnectionFailed();
      break;
    case State::FORGET_PROMPT:
      renderForgetPrompt();
      break;
  }

  renderer.displayBuffer();
}

void BluetoothPairingActivity::renderScanning() const {
  renderer.drawCenteredText(UI_10_FONT_ID, 100, "Scanning for devices...");

  const auto labels = mappedInput.mapLabels("« Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BluetoothPairingActivity::renderDeviceList() const {
  const auto pageWidth = renderer.getScreenWidth();

  if (deviceList.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, 100, "No devices found");
    renderer.drawCenteredText(SMALL_FONT_ID, 130, "Put your device in pairing mode");

    const auto labels = mappedInput.mapLabels("« Back", "", "Rescan", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  // Draw device list
  const int startY = 60;
  const int rowHeight = 30;
  const int maxVisible = 8;

  for (int i = 0; i < static_cast<int>(deviceList.size()) && i < maxVisible; i++) {
    const int y = startY + i * rowHeight;
    const bool isSelected = (i == selectedIndex);

    if (isSelected) {
      renderer.fillRect(0, y - 2, pageWidth - 1, rowHeight);
    }

    // Device name
    std::string displayName = deviceList[i].name;
    if (deviceList[i].isPaired) {
      displayName += " *";
    }
    renderer.drawText(UI_10_FONT_ID, 20, y, displayName.c_str(), !isSelected);

    // Signal strength indicator
    int bars = 0;
    if (deviceList[i].rssi > -60) bars = 3;
    else if (deviceList[i].rssi > -75) bars = 2;
    else bars = 1;

    char rssiStr[8];
    snprintf(rssiStr, sizeof(rssiStr), "%d", bars);
    // Draw simple bar indicator
    for (int b = 0; b < bars; b++) {
      int barX = pageWidth - 40 + b * 6;
      int barH = 6 + b * 4;
      int barY = y + 14 - barH;
      renderer.fillRect(barX, barY, 4, barH);
      if (isSelected) {
        // Invert for selected row — draw outline instead
        renderer.drawRect(barX, barY, 4, barH);
      }
    }
  }

  const char* rightHint = "";
  if (!deviceList.empty() && deviceList[selectedIndex].isPaired) {
    rightHint = "Forget";
  }

  const auto labels = mappedInput.mapLabels("« Back", "Connect", "Rescan", rightHint);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BluetoothPairingActivity::renderConnecting() const {
  std::string msg = "Connecting to " + connectingName + "...";
  renderer.drawCenteredText(UI_10_FONT_ID, 100, msg.c_str());
}

void BluetoothPairingActivity::renderConnected() const {
  std::string msg = "Connected to " + connectingName;
  renderer.drawCenteredText(UI_10_FONT_ID, 100, msg.c_str());
  renderer.drawCenteredText(SMALL_FONT_ID, 140, "Device paired and ready");

  const auto labels = mappedInput.mapLabels("« Back", "OK", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BluetoothPairingActivity::renderConnectionFailed() const {
  std::string msg = "Failed to connect to " + connectingName;
  renderer.drawCenteredText(UI_10_FONT_ID, 100, msg.c_str());
  renderer.drawCenteredText(SMALL_FONT_ID, 130, "Make sure the device is in pairing mode");

  const auto labels = mappedInput.mapLabels("« Back", "Retry", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BluetoothPairingActivity::renderForgetPrompt() const {
  if (deviceList.empty()) return;

  const auto& dev = deviceList[selectedIndex];
  std::string msg = "Forget " + dev.name + "?";
  renderer.drawCenteredText(UI_10_FONT_ID, 100, msg.c_str());
  renderer.drawCenteredText(SMALL_FONT_ID, 130, "This will unpair the device");

  const auto labels = mappedInput.mapLabels("Cancel", "Forget", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
