#include <BLEMIDI_Transport.h>

//#include <hardware/BLEMIDI_ESP32_NimBLE.h>
#include <hardware/BLEMIDI_ESP32.h>
//#include <hardware/BLEMIDI_nRF52.h>
//#include <hardware/BLEMIDI_ArduinoBLE.h>

#include "Handshake.h"

#include <OneButton.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 38
#endif

#define SERIAL2_MIDI_RATE 31250
#define SERIAL_RATE 115200   // 

BLEMIDI_CREATE_DEFAULT_INSTANCE()

unsigned long t0 = millis();
bool isConnected = false;

char bleAddressString[80];
// Handshake *handshake_;

OneButton key1, key2;

void OnKey1();
void OnKey2();

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

  key1.setup(0, INPUT_PULLUP, true);
  key1.attachPress(OnKey1);
  key1.attachClick(OnKey1);

  key2.setup(17, INPUT_PULLUP, true);
  key2.attachPress((OnKey2));
  key2.attachClick((OnKey2));
}

void OnKey1()
{
  if (isConnected)
  {
    MIDI.sendNoteOn(60, 100, 1); // note 60, velocity 100 on channel 1
  }
}

void OnKey2()
{
  if (isConnected)
  {
    MIDI.sendNoteOff(60, 0, 1); // note 60, velocity 0 on channel 1
  }
}

void loop()
{
  MIDI.read();

  // keep watching the push button:
  key1.tick();
  key2.tick();

  // You can implement other code in here or just wait a while
  delay(10);
}
