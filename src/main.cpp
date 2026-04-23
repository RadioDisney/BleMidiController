#include <BLEMIDI_Transport.h>

//#include <hardware/BLEMIDI_ESP32_NimBLE.h>
#include <hardware/BLEMIDI_ESP32.h>
//#include <hardware/BLEMIDI_nRF52.h>
//#include <hardware/BLEMIDI_ArduinoBLE.h>

#include "Handshake.h"

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

void OnConnected();

volatile bool isWireConnected = false;

// -----------------------------------------------------------------------------
// When BLE connected, LED will turn on (indication that connection was successful)
// When receiving a NoteOn, LED will go out, on NoteOff, light comes back on.
// This is an easy and conveniant way to show that the connection is alive and working. 
// -----------------------------------------------------------------------------
void setup()
{ 
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial2.begin(SERIAL2_MIDI_RATE, SERIAL_8N1, G1, G2);
  Serial.begin(SERIAL_RATE);

  MIDI.begin();

  BLEMIDI.setHandleConnected([]() {
    isConnected = true;
    digitalWrite(LED_BUILTIN, HIGH);
    isWireConnected = false;
  });

  BLEMIDI.setHandleDisconnected([]() {
    digitalWrite(LED_BUILTIN, LOW);
    isConnected = false;
  });

  MIDI.setHandleNoteOn([](byte channel, byte note, byte velocity) {
    // digitalWrite(LED_BUILTIN, LOW);
  });
  MIDI.setHandleNoteOff([](byte channel, byte note, byte velocity) {
    // digitalWrite(LED_BUILTIN, HIGH);
  });

  BLEAddress bleAddress = BLEDevice::getAddress();
  
  sprintf(bleAddressString, "\xF0%s\xF7", bleAddress.toString().c_str());

  // handshake_ = new Handshake(4);
  // handshake_->onConnected_ = OnConnected;

  attachInterrupt(digitalPinToInterrupt(4), OnConnected, RISING);
}



void SerialWriteData(byte* data, int length)
{
  for (int i = 0; i < length; i++)
      Serial.write(*(data+i));
}


byte midiSerialReadBuffer_[10];
byte *midiSerialReadPos_;

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
bool isOn_ = false;
void loop()
{
  // OnConnected();

  MIDI.read();

  if (Serial2.available())
  {
      byte b = Serial2.read();

      if (b >= 0x80u)
      {
        midiSerialReadPos_ = midiSerialReadBuffer_;
      }
      *midiSerialReadPos_++ = b;     
     
      if (isConnected)
      {
        if (midiSerialReadPos_ - midiSerialReadBuffer_ >= 3)
        {
          switch (midiSerialReadBuffer_[0] & 0xF0u)
          {
            case 0x80:
              {
                MIDI.sendNoteOff(midiSerialReadBuffer_[1], midiSerialReadBuffer_[2], (midiSerialReadBuffer_[0] & 0x0Fu) + 1);
                // MIDI.sendNoteOff (60, 0, 1);
                digitalWrite(LED_BUILTIN, HIGH);
              }
              break;
            case 0x90:
              {
                MIDI.sendNoteOn(midiSerialReadBuffer_[1], midiSerialReadBuffer_[2], (midiSerialReadBuffer_[0] & 0x0Fu) + 1);
                // MIDI.sendNoteOn (60, 100, 1);
                digitalWrite(LED_BUILTIN, LOW);
              }
              break;
          }

          midiSerialReadPos_ = midiSerialReadBuffer_;     
      }
     }
  }

  // if (isConnected && (millis() - t0) > 1000)
  // if ((millis() - t0) > 1000)
  // {
  //   t0 = millis();

  //   if (isOn_)
  //   {
  //     // MIDI.sendNoteOff (60, 0, 1);
  //     Serial2.write(0x80);
  //     Serial2.write(0x3C);
  //     Serial2.write(0x01);
  //     digitalWrite(LED_BUILTIN, HIGH);
  //   }
  //   else
  //   {
  //     // MIDI.sendNoteOn (60, 100, 1); // note 60, velocity 100 on channel 1
  //     Serial2.write(0x90);
  //     Serial2.write(0x3C);
  //     Serial2.write(0x40);
  //     digitalWrite(LED_BUILTIN, LOW);
  //   }

  //   isOn_ = !isOn_;
  // }

  if (Serial.availableForWrite())
  {
      if (isWireConnected)
        SerialWriteData((byte*)bleAddressString, strlen(bleAddressString));
  }

  // handshake_->Update();
}

void OnConnected()
{
    isWireConnected = true;
}