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

uint8_t keyId2Pitch[4] = 
{
  64, // key 1 -> MIDI note 60 (C4)
  67, // key 2 -> MIDI note 62 (D4)
  69, // key 3 -> MIDI note 64 (E4)
  72, // key 4 -> MIDI note 65 (F4)
};

void OnChainKey(uint8_t deviceId, ChainKeyEvent event)
{
  Serial.printf("Chain Key event: deviceId=%d, event=%d\r\n", deviceId, event);
  // Use the key's position on the chain to derive a MIDI note.
  if (deviceId < 1 || deviceId > 4)
  {
    Serial.printf("Chain Key event: deviceId=%d out of range\r\n", deviceId);
    return;
  }
  midi::DataByte pitch = keyId2Pitch[deviceId - 1]; // deviceId starts at 1, array index starts at 0
  

  switch (event)
  {
  // Non-report (polling) mode: the device only returns the raw pressed level
  // via cmd 0xE1, so we get pressed/released edges. Map them to NoteOn/NoteOff.
  case CHAIN_KEY_EVENT_PRESSED:
    if (isConnected) {
      MIDI.sendNoteOn(pitch, 100, 1);
    }
    chainKey.setColor(deviceId, 0x00, 0xFF, 0x00); // green while held
    break;
  case CHAIN_KEY_EVENT_RELEASED:
    if (isConnected) {
      MIDI.sendNoteOff(pitch, 0, 1);
    }
    chainKey.setColor(deviceId, 0x20, 0x20, 0x20); // back to idle
    break;
  // Report mode events (kept for when report mode is used):
  case CHAIN_KEY_EVENT_SINGLE_PRESS:
    if (isConnected) {
      MIDI.sendNoteOn(pitch, 100, 1);
    }
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

  // setHandleMessage belongs to MidiInterface (the global MIDI object),
  // not to the BLEMIDI transport. MidiMessage is a nested typedef of
  // MidiInterface, so reach it via decltype(MIDI)::MidiMessage.
  MIDI.setHandleMessage([](const decltype(MIDI)::MidiMessage &message)
                        {
                          switch (message.type)
                          {
                          case midi::SystemExclusive:
                            Serial.printf("SysEx : ");
                            for (int i = 0; i < message.length; i++)
                            {
                              Serial.printf("%02X ", message.sysexArray[i]);
                            }
                            Serial.println();

                            break;

                          default:
                            Serial.printf("MIDI msg: type=%d channel=%d data1=%d data2=%d\r\n",
                                          message.type, message.channel, message.data1, message.data2);
                            break;
                          }
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
    // Switch the whole bus into non-report mode: the keys will NOT push 0xE0
    // press packets. Instead the host must poll each key with the 0xE1 query
    // (CHAIN_BUTTON_GET_STATUS) and the key answers with its pressed level.
    chainKey.setPollingMode(true);

    for (uint8_t i = 0; i < chainKey.keyCount(); i++)
    {
      uint16_t id = chainKey.keyId(i);
      // dim white indicator so we know the key is ready
      chainKey.setBrightness(id, 30);
      chainKey.setColor(id, 0x20, 0x20, 0x20);
      // configure double-click (200ms) and long-press (3s) detection
      chainKey.setTriggerInterval(id, BUTTON_DOUBLE_CLICK_TIME_200MS,
                                  BUTTON_LONG_PRESS_TIME_3S);
      // non-report mode: host polls button status via cmd 0xE1
      chainKey.setButtonMode(id, CHAIN_BUTTON_NONE_REPORT_MODE);

      // Read back the button mode (protocol cmd 0xE5) to verify it was applied.
      // 0 = CHAIN_BUTTON_NONE_REPORT_MODE (non reporting), 1 = CHAIN_BUTTON_REPORT_MODE
      chain_button_mode_t mode = CHAIN_BUTTON_REPORT_MODE;
      if (chainKey.getButtonMode(id, mode))
      {
        Serial.printf("Chain Key id=%d mode readback=%d (%s)\r\n", id, mode,
                      mode == CHAIN_BUTTON_NONE_REPORT_MODE   ? "none-report"
                      : mode == CHAIN_BUTTON_REPORT_MODE      ? "report"
                                                               : "unknown");
      }
      else
      {
        Serial.printf("Chain Key id=%d getButtonMode failed\r\n", id);
      }
    }
  }
}

void OnKey(AceButton* button, uint8_t eventType, uint8_t buttonState)
{
  Serial.printf("Button event: button=%p, eventType=%d, buttonState=%d\r\n",
                button, eventType, buttonState);
  if (isConnected)
  {
    midi::DataByte pitch = 64;
    pitch  = button == &key1 ? 62 : 60;
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
