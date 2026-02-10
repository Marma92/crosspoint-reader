#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_BT_ENABLED
#include <NimBLEDevice.h>
#endif

class BLEKeyboardHandler;

/**
 * Bluetooth Manager for CrossPoint Reader (BLE Central role)
 *
 * The ESP32 acts as a BLE central/client that scans for and connects to
 * BLE HID peripherals (page-turning gadgets, keyboards).
 *
 * Design principles:
 * - Central role: we scan and connect to external HID devices
 * - Minimal RAM footprint with lazy initialization
 * - Clean shutdown to prevent memory leaks
 * - Auto-reconnect support for known devices
 */
class BluetoothManager {
 private:
  BluetoothManager() = default;
  static BluetoothManager instance;

  bool initialized = false;
  bool scanning = false;
  bool connected = false;

#ifdef CONFIG_BT_ENABLED
  NimBLEClient* pClient = nullptr;
  BLEKeyboardHandler* pKeyboardHandler = nullptr;

  static constexpr const char* DEVICE_NAME = "CrossPoint";

  // Standard BLE HID service UUID
  static constexpr uint16_t HID_SERVICE_UUID = 0x1812;

  // Scan parameters (RAM-conscious)
  static constexpr uint32_t SCAN_DURATION_SEC = 5;
  static constexpr uint8_t MAX_SCAN_RESULTS = 10;

  class ClientCallbacks : public NimBLEClientCallbacks {
   public:
    void onConnect(NimBLEClient* pClient) override;
    void onDisconnect(NimBLEClient* pClient, int reason) override;
    void onConnectFail(NimBLEClient* pClient, int reason) override;
  };
#endif

 public:
  BluetoothManager(const BluetoothManager&) = delete;
  BluetoothManager& operator=(const BluetoothManager&) = delete;

  static BluetoothManager& getInstance() { return instance; }

  /**
   * Initialize BLE stack (does not start scanning)
   * @return true if initialization successful
   */
  bool initialize();

  /**
   * Shutdown BLE stack completely and free memory
   */
  void shutdown();

  /**
   * Start scanning for BLE HID devices
   * @return true if scan started
   */
  bool startScan();

  /**
   * Stop an active scan
   */
  void stopScan();

  /**
   * Connect to a discovered BLE device by address
   * @param address BLE address of the device to connect to
   * @param addressType BLE address type (0=public, 1=random)
   * @return true if connection initiated successfully
   */
  bool connectToDevice(const std::string& address, uint8_t addressType = 0);

  /**
   * Disconnect from the currently connected device
   */
  void disconnectDevice();

  /**
   * Check states
   */
  bool isInitialized() const { return initialized; }
  bool isScanning() const { return scanning; }
  bool isConnected() const { return connected; }

  /**
   * Get the name of the currently connected device
   */
  std::string getConnectedDeviceName() const;

  /**
   * Get the address of the currently connected device
   */
  std::string getConnectedDeviceAddress() const;

  /**
   * Get keyboard handler
   */
  BLEKeyboardHandler* getKeyboardHandler() const;

  /**
   * Get memory usage estimate in bytes
   */
  size_t getMemoryUsage() const;

#ifdef CONFIG_BT_ENABLED
  /**
   * Get scan results (only valid while/after scanning)
   */
  NimBLEScanResults getScanResults();
#endif

 private:
#ifdef CONFIG_BT_ENABLED
  bool setupClient();
  bool subscribeToHidReports();
#endif
};

#define BLUETOOTH_MANAGER BluetoothManager::getInstance()
