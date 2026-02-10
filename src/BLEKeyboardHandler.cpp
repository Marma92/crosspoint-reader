#include "BLEKeyboardHandler.h"

#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#include "MappedInputManager.h"
#endif

void BLEKeyboardHandler::processKeyboardReport(const uint8_t* data, size_t length) {
  if (!data || length < 3) {
    return;
  }

  // Standard HID keyboard report: [modifier, reserved, key1, key2, key3, key4, key5, key6]
  // Boot protocol report may be 8 bytes, some page-turners send shorter reports.
  // We handle both: if length >= 8, use full format. If shorter, keys start at offset 2.
  const int keyOffset = 2;
  const int numKeys = (length >= 8) ? 6 : static_cast<int>(length - keyOffset);

  // Check if this is a key-release report (all zeros after modifier)
  bool allZero = true;
  for (int i = keyOffset; i < keyOffset + numKeys && i < static_cast<int>(length); i++) {
    if (data[i] != 0) {
      allZero = false;
      break;
    }
  }

  if (allZero) {
    // Key release — clear last keycodes
    memset(lastKeycodes, 0, sizeof(lastKeycodes));
    return;
  }

  // Process each keycode
  for (int i = 0; i < numKeys && (keyOffset + i) < static_cast<int>(length); i++) {
    uint8_t keycode = data[keyOffset + i];
    if (keycode == 0) continue;

    // Check if this key was already in the last report (held key, not new press)
    bool alreadyPressed = false;
    for (int j = 0; j < 6; j++) {
      if (lastKeycodes[j] == keycode) {
        alreadyPressed = true;
        break;
      }
    }

    if (!alreadyPressed) {
      uint8_t buttonId = mapScancodeToButton(keycode);
      if (buttonId != 0xFF) {
        queueButtonEvent(buttonId);
      }
    }
  }

  // Store current keycodes for next comparison
  memset(lastKeycodes, 0, sizeof(lastKeycodes));
  for (int i = 0; i < numKeys && i < 6; i++) {
    lastKeycodes[i] = data[keyOffset + i];
  }
}

void BLEKeyboardHandler::queueButtonEvent(uint8_t buttonId) {
  // Simple ring buffer write (single producer from BLE task)
  uint8_t nextWrite = (queueWriteIdx + 1) % QUEUE_SIZE;
  if (nextWrite == queueReadIdx) {
    // Queue full, drop oldest event
    return;
  }
  eventQueue[queueWriteIdx] = buttonId;
  queueWriteIdx = nextWrite;
}

void BLEKeyboardHandler::update() {
  // Process all queued events on the main loop (single consumer)
  // This runs AFTER InputManager::update() clears pressedEvents,
  // so injected presses will be visible until the next update() cycle.
#ifdef ARDUINO
  extern MappedInputManager mappedInputManager;

  while (queueReadIdx != queueWriteIdx) {
    uint8_t buttonId = eventQueue[queueReadIdx];
    queueReadIdx = (queueReadIdx + 1) % QUEUE_SIZE;

    // Debounce: don't inject too rapidly (e-ink can't keep up)
    uint32_t now = millis();
    if (now - lastActivityTime < DEBOUNCE_MS) {
      continue;
    }
    lastActivityTime = now;

    mappedInputManager.injectButton(static_cast<MappedInputManager::Button>(buttonId));
  }
#endif
}

uint8_t BLEKeyboardHandler::mapScancodeToButton(uint8_t scancode) {
  // Map USB HID scancodes to MappedInputManager::Button enum values
  // Button: Back=0, Confirm=1, Left=2, Right=3, Up=4, Down=5, Power=6, PageBack=7, PageForward=8
  switch (scancode) {
    // --- Page turning (primary use case for BLE gadgets) ---
    case 0x4E:  // PAGE DOWN -> Page Forward
    case 0x2C:  // SPACE -> Page Forward
    case 0x4F:  // RIGHT ARROW -> Page Forward (many page-turners use this)
      return 8;  // PageForward

    case 0x4B:  // PAGE UP -> Page Back
    case 0x2A:  // BACKSPACE -> Page Back
    case 0x50:  // LEFT ARROW -> Page Back (many page-turners use this)
      return 7;  // PageBack

    // --- Navigation (for full keyboard support) ---
    case 0x52:  // UP ARROW
      return 4;  // Up

    case 0x51:  // DOWN ARROW
      return 5;  // Down

    case 0x28:  // RETURN/ENTER -> Confirm
      return 1;  // Confirm

    case 0x29:  // ESCAPE -> Back
      return 0;  // Back

    default:
      return 0xFF;  // Unmapped
  }
}
