#ifndef CHAIN_KEY_MANAGER
#define CHAIN_KEY_MANAGER

#include <Arduino.h>
#include "M5Chain.h"

// Button press event types exposed to the application.
enum ChainKeyEvent
{
  CHAIN_KEY_EVENT_NONE = 0,
  CHAIN_KEY_EVENT_SINGLE_PRESS,
  CHAIN_KEY_EVENT_DOUBLE_PRESS,
  CHAIN_KEY_EVENT_LONG_PRESS,
};

// -----------------------------------------------------------------------------
// ChainKeyManager
//
// A thin wrapper around the M5Chain `Chain` class that takes care of bus
// initialisation, device enumeration and polling of every Chain Key found on
// the daisy-chain bus. Call begin() once in setup() and update() in loop().
// Button events are delivered through a callback in the style of AceButton.
// -----------------------------------------------------------------------------
class ChainKeyManager
{
public:
  // Signature of the button event callback.
  //   deviceId : id (position) of the key device that fired the event
  //   event    : one of the ChainKeyEvent values above
  typedef void (*EventHandler)(uint8_t deviceId, ChainKeyEvent event);

  // rxPin / txPin default to the pins used by the StampS3 Chain port.
  ChainKeyManager(uint8_t rxPin = 5, uint8_t txPin = 6,
                  HardwareSerial *serial = &Serial2,
                  unsigned long baud = 115200);
  ~ChainKeyManager();

  // Initialise the bus and enumerate every connected device.
  // Returns true if at least one Chain Key device was found.
  bool begin();

  // Call from loop(). Drains the button press queue of each key and fires
  // the registered handler for every event.
  void update();

  // Register the button event handler.
  void setEventHandler(EventHandler handler) { eventHandler_ = handler; }

  // Number of Chain Key devices detected on the bus.
  uint8_t keyCount() const { return keyCount_; }

  // Device id (position on the chain, starts at 1) of the n-th key.
  uint16_t keyId(uint8_t index) const;

  // ----- RGB helpers (per key device) -------------------------------------

  // Set RGB brightness (0-100). Returns true on success.
  bool setBrightness(uint16_t deviceId, uint8_t brightness);

  // Set the RGB colour (r, g, b in 0-255). Returns true on success.
  bool setColor(uint16_t deviceId, uint8_t r, uint8_t g, uint8_t b);

  // ----- Button configuration --------------------------------------------

  // Configure the double-click and long-press detection intervals.
  bool setTriggerInterval(uint16_t deviceId,
                          button_double_click_time_t doubleClickTime,
                          button_long_press_time_t longPressTime);

  // Set the button operating mode (report / none-report).
  bool setButtonMode(uint16_t deviceId, chain_button_mode_t mode);

  // Access the underlying Chain instance for anything not covered above.
  Chain &chain() { return chain_; }

private:
  Chain chain_;
  HardwareSerial *serial_;
  uint8_t rxPin_;
  uint8_t txPin_;
  unsigned long baud_;

  device_list_t *deviceList_;   // enumerated device list (owned)
  uint16_t *keyIds_;            // ids of devices that are keys (owned)
  uint8_t keyCount_;

  EventHandler eventHandler_;

  void enumerateDevices();
  void fireButtonEvents(uint16_t deviceId);
};

#endif
