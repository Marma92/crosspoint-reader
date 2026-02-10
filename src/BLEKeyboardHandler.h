#pragma once

#include <stddef.h>
#include <stdint.h>
#include <atomic>

/**
 * BLE Keyboard/HID Report Handler for CrossPoint Reader
 *
 * This class is a pure HID report parser. It receives raw HID keyboard
 * reports (from BluetoothManager's BLE client notifications) and translates
 * them into CrossPoint button presses via MappedInputManager::injectButton().
 *
 * It does NOT manage any BLE resources — that's BluetoothManager's job.
 *
 * Design principles:
 * - Minimal RAM footprint (~64 bytes)
 * - Thread-safe: BLE callbacks run on NimBLE task, injection happens on main loop
 * - Debouncing to avoid duplicate presses on e-ink
 */
class BLEKeyboardHandler {
 public:
  BLEKeyboardHandler() = default;
  ~BLEKeyboardHandler() = default;

  BLEKeyboardHandler(const BLEKeyboardHandler&) = delete;
  BLEKeyboardHandler& operator=(const BLEKeyboardHandler&) = delete;

  /**
   * Process an incoming HID keyboard report (called from BLE notification context).
   * Thread-safe: queues button events for the main loop to process.
   * @param data Raw HID report data
   * @param length Length of the report
   */
  void processKeyboardReport(const uint8_t* data, size_t length);

  /**
   * Process queued button injections on the main loop.
   * Must be called from the main loop() context.
   */
  void update();

  /**
   * Get the timestamp (millis) of the last received HID report activity.
   * Returns 0 if no activity has occurred yet.
   */
   uint32_t getLastActivityTime() const { return lastActivityTime; }

   void setDebounceMs(uint32_t ms) { debounceMs = ms; }
   uint32_t getDebounceMs() const { return debounceMs; }

  /**
   * Get memory usage estimate
   */
  size_t getMemoryUsage() const { return sizeof(*this); }

 private:
   // Thread-safe button event queue (atomic ring buffer)
   // BLE callback writes, main loop reads
   static constexpr int QUEUE_SIZE = 8;
   uint8_t eventQueue[QUEUE_SIZE] = {0};
   std::atomic<uint8_t> queueWriteIdx = 0;
   std::atomic<uint8_t> queueReadIdx = 0;

  // Debounce: last processed report to avoid repeats
   uint8_t lastKeycodes[6] = {0};
   uint32_t lastActivityTime = 0;
   uint32_t debounceMs = DEFAULT_DEBOUNCE_MS;
   static constexpr uint32_t DEFAULT_DEBOUNCE_MS = 80;

  /**
   * Queue a button event (called from BLE context)
   * @param buttonId MappedInputManager::Button value
   */
  void queueButtonEvent(uint8_t buttonId);

  /**
   * Map USB HID scancode to MappedInputManager::Button value
   * @param scancode USB HID scancode
   * @return Button ID or 0xFF if unmapped
   */
  static uint8_t mapScancodeToButton(uint8_t scancode);
};
