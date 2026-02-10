#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "activities/Activity.h"

/**
 * Bluetooth device pairing activity.
 *
 * State machine flow:
 * SCANNING -> DEVICE_LIST -> CONNECTING -> CONNECTED / CONNECTION_FAILED
 *                          -> FORGET_PROMPT (long press on paired device)
 */
class BluetoothPairingActivity final : public Activity {
 public:
  enum class State { SCANNING, DEVICE_LIST, CONNECTING, CONNECTED, CONNECTION_FAILED, FORGET_PROMPT };

  struct DiscoveredDevice {
    std::string name;
    std::string address;
    uint8_t addressType = 0;
    int rssi;
    bool isPaired;
  };

  BluetoothPairingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            const std::function<void()>& onGoBack)
      : Activity("BluetoothPairing", renderer, mappedInput), onGoBack(onGoBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  State state = State::SCANNING;
  int selectedIndex = 0;
  unsigned long stateStartTime = 0;

  std::vector<DiscoveredDevice> deviceList;
  std::string connectingAddress;
  uint8_t connectingAddressType = 0;
  std::string connectingName;

  const std::function<void()> onGoBack;

  static constexpr unsigned long CONNECTION_TIMEOUT_MS = 15000;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();

  void startScan();
  void processScanResults();
  void selectDevice();
  void attemptConnection(const std::string& address, uint8_t addressType, const std::string& name);

  void render() const;
  void renderScanning() const;
  void renderDeviceList() const;
  void renderConnecting() const;
  void renderConnected() const;
  void renderConnectionFailed() const;
  void renderForgetPrompt() const;
};
