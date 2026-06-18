#include <BLEMIDI_Transport.h>

//#include <hardware/BLEMIDI_ESP32_NimBLE.h>
#include <hardware/BLEMIDI_ESP32.h>
//#include <hardware/BLEMIDI_nRF52.h>
//#include <hardware/BLEMIDI_ArduinoBLE.h>

#include "Handshake.h"
#include "ChainKeyManager.h"

#include <AceButton.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 38
#endif

#define KEY1_PIN 0
#define KEY2_PIN 17

#define SERIAL2_MIDI_RATE 31250
#define SERIAL_RATE 115200   // 

BLEMIDI_CREATE_DEFAULT_INSTANCE()

unsigned long t0 = millis();
bool isConnected = false;

char bleAddressString[80];
// Handshake *handshake_;

using namespace ace_button;

AceButton key1((uint8_t)KEY1_PIN), key2((uint8_t)KEY2_PIN);

void OnKey(AceButton* button, uint8_t eventType, uint8_t buttonState);

// -----------------------------------------------------------------------------
// ChainKeyManager
// -----------------------------------------------------------------------------
ChainKeyManager chainKey;

void OnChainKey(uint8_t deviceId, ChainKeyEvent event)
{
  // Use the key's position on the chain to derive a MIDI note.
  midi::DataByte pitch = 60 + deviceId; // base C4 + position offset

  switch (event)
  {
  case CHAIN_KEY_EVENT_SINGLE_PRESS:
    if (isConnected) {
      MIDI.sendNoteOn(pitch, 100, 1);
    }
    // visual feedback on the key's RGB
    chainKey.setColor(deviceId, 0x00, 0xFF, 0x00); // green
    break;
  case CHAIN_KEY_EVENT_DOUBLE_PRESS:
    if (isConnected) {
      MIDI.sendNoteOn(pitch + 12, 100, 1);
    }
    chainKey.setColor(deviceId, 0x00, 0x00, 0xFF); // blue
    break;
  case CHAIN_KEY_EVENT_LONG_PRESS:
    if (isConnected) {
      MIDI.sendControlChange(123, 0, 1); // all notes off
    }
    chainKey.setColor(deviceId, 0xFF, 0x00, 0x00); // red
    break;
  default:
    break;
  }
}

// -----------------------------------------------------------------------------
// When BLE connected, LED will turn on (indication that connection was successful)
// When receiving a NoteOn, LED will go out, on NoteOff, light comes back on.
// This is an easy and conveniant way to show that the connection is alive and working. 
// -----------------------------------------------------------------------------
void setup()
{ 
  Serial.begin(SERIAL_RATE);
  delay(5*1000); // wait for 10 seconds to allow time to open the serial monitor and see the BLE address for pairing
  Serial.println("BLE MIDI Chain Key");

  MIDI.begin();

  BLEMIDI.setHandleConnected([]() {
    isConnected = true;
  });

  BLEMIDI.setHandleDisconnected([]() {
    isConnected = false;
  });

  BLEAddress bleAddress = BLEDevice::getAddress();
  
  sprintf(bleAddressString, "\xF0%s\xF7", bleAddress.toString().c_str());

  // handshake_ = new Handshake(4);
  // handshake_->onConnected_ = OnConnected;

  pinMode(KEY1_PIN, INPUT_PULLUP);
  pinMode(KEY2_PIN, INPUT_PULLUP);

  ButtonConfig* buttonConfig = ButtonConfig::getSystemButtonConfig();
  buttonConfig->setEventHandler(OnKey);

  // ----- Chain Key setup ---------------------------------------------------
  chainKey.setEventHandler(OnChainKey);
  if (chainKey.begin())
  {
    for (uint8_t i = 0; i < chainKey.keyCount(); i++)
    {
      uint16_t id = chainKey.keyId(i);
      // dim white indicator so we know the key is ready
      chainKey.setBrightness(id, 30);
      chainKey.setColor(id, 0x20, 0x20, 0x20);
      // configure double-click (200ms) and long-press (3s) detection
      chainKey.setTriggerInterval(id, BUTTON_DOUBLE_CLICK_TIME_200MS,
                                  BUTTON_LONG_PRESS_TIME_3S);
      // enable event reporting mode
      chainKey.setButtonMode(id, CHAIN_BUTTON_REPORT_MODE);
    }
  }
}

void OnKey(AceButton* button, uint8_t eventType, uint8_t buttonState)
{
  if (isConnected)
  {
    midi::DataByte pitch = 64;
    pitch  = button == &key1 ? 31 : 63;
    switch (eventType)
    {
    case AceButton::kEventPressed:
      MIDI.sendNoteOn(pitch, 100, 1); 
      break;
    case AceButton::kEventReleased:
      MIDI.sendNoteOff(pitch, 0, 1);
      break;
    default:
      break;
    }
  }
}

void loop()
{
  // Serial.println(bleAddressString);

  MIDI.read();

  // keep watching the push button:
  key1.check();
  key2.check();

  // poll Chain Key devices for new button events
  chainKey.update();

  // You can implement other code in here or just wait a while
  delay(10);
}
