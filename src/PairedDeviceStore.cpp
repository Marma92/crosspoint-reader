#include "PairedDeviceStore.h"

#include <HardwareSerial.h>
#include <HalStorage.h>
#include <Serialization.h>

PairedDeviceStore PairedDeviceStore::instance;

namespace {
constexpr uint8_t BT_FILE_VERSION = 2;
constexpr char BT_FILE[] = "/.crosspoint/bt_paired.bin";
}  // namespace

bool PairedDeviceStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  FsFile file;
  if (!Storage.openFileForWrite("BTS", BT_FILE, file)) {
    return false;
  }

  serialization::writePod(file, BT_FILE_VERSION);
  serialization::writePod(file, static_cast<uint8_t>(devices.size()));

  for (const auto& dev : devices) {
    serialization::writeString(file, dev.name);
    serialization::writeString(file, dev.address);
    serialization::writePod(file, dev.addressType);
  }

  file.close();
  Serial.printf("[%lu] [BTS] Saved %zu paired devices\n", millis(), devices.size());
  return true;
}

bool PairedDeviceStore::loadFromFile() {
  FsFile file;
  if (!Storage.openFileForRead("BTS", BT_FILE, file)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != BT_FILE_VERSION && version != 1) {
    Serial.printf("[%lu] [BTS] Unknown file version: %u\n", millis(), version);
    file.close();
    return false;
  }

  uint8_t count;
  serialization::readPod(file, count);

  devices.clear();
  for (uint8_t i = 0; i < count && i < MAX_DEVICES; i++) {
    PairedDevice dev;
    serialization::readString(file, dev.name);
    serialization::readString(file, dev.address);
    if (version >= 2) {
      serialization::readPod(file, dev.addressType);
    }
    devices.push_back(dev);
  }

  file.close();
  Serial.printf("[%lu] [BTS] Loaded %zu paired devices\n", millis(), devices.size());
  return true;
}

bool PairedDeviceStore::addDevice(const std::string& name, const std::string& address, uint8_t addressType) {
  // Update existing device
  for (auto& dev : devices) {
    if (dev.address == address) {
      dev.name = name;
      dev.addressType = addressType;
      Serial.printf("[%lu] [BTS] Updated device: %s (%s)\n", millis(), name.c_str(), address.c_str());
      return saveToFile();
    }
  }

  // Remove oldest if at limit
  if (devices.size() >= MAX_DEVICES) {
    devices.erase(devices.begin());
  }

  PairedDevice newDevice;
  newDevice.name = name;
  newDevice.address = address;
  newDevice.addressType = addressType;
  devices.push_back(newDevice);
  Serial.printf("[%lu] [BTS] Added device: %s (%s)\n", millis(), name.c_str(), address.c_str());
  return saveToFile();
}

bool PairedDeviceStore::removeDevice(const std::string& address) {
  for (auto it = devices.begin(); it != devices.end(); ++it) {
    if (it->address == address) {
      Serial.printf("[%lu] [BTS] Removed device: %s\n", millis(), address.c_str());
      devices.erase(it);
      return saveToFile();
    }
  }
  return false;
}

const PairedDevice* PairedDeviceStore::findDevice(const std::string& address) const {
  for (const auto& dev : devices) {
    if (dev.address == address) {
      return &dev;
    }
  }
  return nullptr;
}

bool PairedDeviceStore::hasPairedDevice(const std::string& address) const {
  return findDevice(address) != nullptr;
}

const PairedDevice* PairedDeviceStore::getLastDevice() const {
  if (devices.empty()) return nullptr;
  return &devices.back();
}

void PairedDeviceStore::clearAll() {
  devices.clear();
  saveToFile();
  Serial.printf("[%lu] [BTS] Cleared all paired devices\n", millis());
}
