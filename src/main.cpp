#include <BLEMIDI_Transport.h>

//#include <hardware/BLEMIDI_ESP32_NimBLE.h>
#include <hardware/BLEMIDI_ESP32.h>
//#include <hardware/BLEMIDI_nRF52.h>
//#include <hardware/BLEMIDI_ArduinoBLE.h>

#include "Handshake.h"

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
// When BLE connected, LED will turn on (indication that connection was successful)
// When receiving a NoteOn, LED will go out, on NoteOff, light comes back on.
// This is an easy and conveniant way to show that the connection is alive and working. 
// -----------------------------------------------------------------------------
void setup()
{ 
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
  MIDI.read();

  // keep watching the push button:
  key1.check();
  key2.check();

  // You can implement other code in here or just wait a while
  delay(10);
}
