#pragma once
#include <string>
#include <vector>

struct PairedDevice {
  std::string name;
  std::string address;  // BLE address as string (e.g., "aa:bb:cc:dd:ee:ff")
  uint8_t addressType = 0;  // BLE address type (0=public, 1=random, etc.)
};

/**
 * Singleton store for paired Bluetooth device records on SD card.
 * Modeled after WifiCredentialStore.
 */
class PairedDeviceStore {
 private:
  static PairedDeviceStore instance;
  std::vector<PairedDevice> devices;

  static constexpr size_t MAX_DEVICES = 4;

  PairedDeviceStore() = default;

 public:
  PairedDeviceStore(const PairedDeviceStore&) = delete;
  PairedDeviceStore& operator=(const PairedDeviceStore&) = delete;

  static PairedDeviceStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();

  bool addDevice(const std::string& name, const std::string& address, uint8_t addressType = 0);
  bool removeDevice(const std::string& address);
  const PairedDevice* findDevice(const std::string& address) const;
  const std::vector<PairedDevice>& getDevices() const { return devices; }
  bool hasPairedDevice(const std::string& address) const;
  bool hasAnyDevice() const { return !devices.empty(); }

  /** Get the most recently added device (for auto-reconnect) */
  const PairedDevice* getLastDevice() const;

  void clearAll();
};

#define PAIRED_DEVICES PairedDeviceStore::getInstance()
