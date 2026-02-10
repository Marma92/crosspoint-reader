#include "BluetoothManager.h"

#include "BLEKeyboardHandler.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

BluetoothManager BluetoothManager::instance;

// Global accessor used by BLEKeyboardHandler callbacks
BLEKeyboardHandler* getActiveKeyboardHandler() {
  return BLUETOOTH_MANAGER.getKeyboardHandler();
}

bool BluetoothManager::initialize() {
#ifdef CONFIG_BT_ENABLED
  if (initialized) {
    return true;
  }

  Serial.printf("[%lu] [BLE] Initializing Bluetooth (central mode)\n", millis());

  try {
    NimBLEDevice::init(DEVICE_NAME);

    // Configure security for bonding with HID devices
    NimBLEDevice::setSecurityAuth(true, false, true);  // bonding, no MITM, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    // Create keyboard handler (lightweight, no BLE resources until connected)
    pKeyboardHandler = new BLEKeyboardHandler();

    initialized = true;
    Serial.printf("[%lu] [BLE] Bluetooth initialized, free heap: %d\n", millis(), ESP.getFreeHeap());
    return true;

  } catch (...) {
    Serial.printf("[%lu] [BLE] Exception during initialization\n", millis());
    NimBLEDevice::deinit();
    return false;
  }
#else
  return false;
#endif
}

void BluetoothManager::shutdown() {
#ifdef CONFIG_BT_ENABLED
  if (!initialized) {
    return;
  }

  Serial.printf("[%lu] [BLE] Shutting down Bluetooth\n", millis());

  // Stop scan if active
  stopScan();

  // Disconnect if connected
  disconnectDevice();

  // Clean up keyboard handler
  if (pKeyboardHandler) {
    delete pKeyboardHandler;
    pKeyboardHandler = nullptr;
  }

  // Deinitialize BLE stack
  NimBLEDevice::deinit();

  pClient = nullptr;
  initialized = false;
  connected = false;
  scanning = false;

  Serial.printf("[%lu] [BLE] Shutdown complete, free heap: %d\n", millis(), ESP.getFreeHeap());
#endif
}

bool BluetoothManager::startScan() {
#ifdef CONFIG_BT_ENABLED
  if (!initialized || scanning) {
    return scanning;
  }

  Serial.printf("[%lu] [BLE] Starting BLE scan (%lu sec)\n", millis(), SCAN_DURATION_SEC);

  auto* pScan = NimBLEDevice::getScan();
  pScan->clearResults();
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(80);
  pScan->setMaxResults(MAX_SCAN_RESULTS);

  if (pScan->start(SCAN_DURATION_SEC, false, false)) {
    scanning = true;
    return true;
  }

  Serial.printf("[%lu] [BLE] Failed to start scan\n", millis());
  return false;
#else
  return false;
#endif
}

void BluetoothManager::stopScan() {
#ifdef CONFIG_BT_ENABLED
  if (!scanning) {
    return;
  }

  auto* pScan = NimBLEDevice::getScan();
  pScan->stop();
  scanning = false;
  Serial.printf("[%lu] [BLE] Scan stopped\n", millis());
#endif
}

bool BluetoothManager::connectToDevice(const std::string& address, uint8_t addressType) {
#ifdef CONFIG_BT_ENABLED
  if (!initialized) {
    return false;
  }

  // Stop scanning if active
  stopScan();

  Serial.printf("[%lu] [BLE] Connecting to %s (type: %u)\n", millis(), address.c_str(), addressType);

  // Clean up existing client if any
  if (pClient && pClient->isConnected()) {
    pClient->disconnect();
  }

  // Create or reuse client
  if (!pClient) {
    if (!setupClient()) {
      return false;
    }
  }

  NimBLEAddress bleAddress(address, addressType);
  pClient->setPeerAddress(bleAddress);
  pClient->setConnectTimeout(10);  // 10 seconds

  if (!pClient->connect()) {
    Serial.printf("[%lu] [BLE] Connection failed\n", millis());
    return false;
  }

  // Connection succeeded, now discover and subscribe to HID reports
  if (!subscribeToHidReports()) {
    Serial.printf("[%lu] [BLE] HID subscription failed, disconnecting\n", millis());
    pClient->disconnect();
    connected = false;
    return false;
  }

  connected = true;
  Serial.printf("[%lu] [BLE] Connected and subscribed to HID, free heap: %d\n", millis(), ESP.getFreeHeap());
  return true;
#else
  (void)address;
  return false;
#endif
}

void BluetoothManager::disconnectDevice() {
#ifdef CONFIG_BT_ENABLED
  if (pClient && pClient->isConnected()) {
    Serial.printf("[%lu] [BLE] Disconnecting device\n", millis());
    pClient->disconnect();
  }
  connected = false;
#endif
}

std::string BluetoothManager::getConnectedDeviceName() const {
#ifdef CONFIG_BT_ENABLED
  if (connected && pClient && pClient->isConnected()) {
    // Read device name from GAP service
    auto* pService = pClient->getService(NimBLEUUID((uint16_t)0x1800));
    if (pService) {
      auto* pChar = pService->getCharacteristic(NimBLEUUID((uint16_t)0x2A00));
      if (pChar) {
        return pChar->getValue().c_str();
      }
    }
    return pClient->getPeerAddress().toString();
  }
#endif
  return "";
}

std::string BluetoothManager::getConnectedDeviceAddress() const {
#ifdef CONFIG_BT_ENABLED
  if (connected && pClient) {
    return pClient->getPeerAddress().toString();
  }
#endif
  return "";
}

BLEKeyboardHandler* BluetoothManager::getKeyboardHandler() const {
#ifdef CONFIG_BT_ENABLED
  return pKeyboardHandler;
#else
  return nullptr;
#endif
}

size_t BluetoothManager::getMemoryUsage() const {
#ifdef CONFIG_BT_ENABLED
  size_t usage = sizeof(*this);
  if (pClient) usage += 4096;  // Estimate for NimBLE client state
  if (pKeyboardHandler) usage += pKeyboardHandler->getMemoryUsage();
  return usage;
#else
  return sizeof(*this);
#endif
}

#ifdef CONFIG_BT_ENABLED
NimBLEScanResults BluetoothManager::getScanResults() {
  auto* pScan = NimBLEDevice::getScan();

  // Update scanning state based on actual scan status
  scanning = pScan->isScanning();

  return pScan->getResults();
}

bool BluetoothManager::setupClient() {
  pClient = NimBLEDevice::createClient();
  if (!pClient) {
    Serial.printf("[%lu] [BLE] Failed to create client\n", millis());
    return false;
  }

  pClient->setClientCallbacks(new ClientCallbacks(), true);

  // Conservative connection parameters for low power
  pClient->setConnectionParams(12, 24, 0, 200);

  return true;
}

bool BluetoothManager::subscribeToHidReports() {
  if (!pClient || !pClient->isConnected()) {
    return false;
  }

  Serial.printf("[%lu] [BLE] Discovering HID service...\n", millis());

  // Look for the standard HID service (0x1812)
  auto* pHidService = pClient->getService(NimBLEUUID((uint16_t)HID_SERVICE_UUID));
  if (!pHidService) {
    Serial.printf("[%lu] [BLE] HID service not found\n", millis());
    return false;
  }

  Serial.printf("[%lu] [BLE] HID service found, discovering characteristics...\n", millis());

  // Find all HID Report characteristics (0x2A4D)
  // HID devices may have multiple report characteristics (input, output, feature)
  const auto& chars = pHidService->getCharacteristics(true);
  bool subscribed = false;

  for (auto* pChar : chars) {
    if (pChar->getUUID() == NimBLEUUID((uint16_t)0x2A4D) && pChar->canNotify()) {
      Serial.printf("[%lu] [BLE] Subscribing to HID Report (handle: %d)\n", millis(), pChar->getHandle());

      bool result = pChar->subscribe(true, [this](NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length,
                                                   bool isNotify) {
        (void)pChar;
        (void)isNotify;
        if (pKeyboardHandler) {
          pKeyboardHandler->processKeyboardReport(pData, length);
        }
      });

      if (result) {
        subscribed = true;
        Serial.printf("[%lu] [BLE] Subscribed to HID Report\n", millis());
      }
    }
  }

  // Also try to subscribe to Boot Keyboard Input Report (0x2A22)
  // Some simple page-turners only support boot protocol
  auto* pBootChar = pHidService->getCharacteristic(NimBLEUUID((uint16_t)0x2A22));
  if (pBootChar && pBootChar->canNotify()) {
    Serial.printf("[%lu] [BLE] Subscribing to Boot Keyboard Report\n", millis());

    bool result = pBootChar->subscribe(
        true, [this](NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
          (void)pChar;
          (void)isNotify;
          if (pKeyboardHandler) {
            pKeyboardHandler->processKeyboardReport(pData, length);
          }
        });

    if (result) {
      subscribed = true;
      Serial.printf("[%lu] [BLE] Subscribed to Boot Keyboard Report\n", millis());
    }
  }

  if (!subscribed) {
    Serial.printf("[%lu] [BLE] No subscribable HID report characteristics found\n", millis());
  }

  return subscribed;
}

void BluetoothManager::ClientCallbacks::onConnect(NimBLEClient* pClient) {
  Serial.printf("[%lu] [BLE] Connected to %s\n", millis(), pClient->getPeerAddress().toString().c_str());
}

void BluetoothManager::ClientCallbacks::onDisconnect(NimBLEClient* pClient, int reason) {
  Serial.printf("[%lu] [BLE] Disconnected (reason: %d)\n", millis(), reason);
  auto& manager = BluetoothManager::getInstance();
  manager.connected = false;
}

void BluetoothManager::ClientCallbacks::onConnectFail(NimBLEClient* pClient, int reason) {
  Serial.printf("[%lu] [BLE] Connection failed (reason: %d)\n", millis(), reason);
}
#endif
