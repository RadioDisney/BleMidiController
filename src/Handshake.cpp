#include "Handshake.h"

volatile ConnectedState Handshake::connectState_ = DISCONNECTED;

Handshake::Handshake(uint8_t interruputPin)
{
  interruptPin_ = interruputPin;
  attachInterrupt(digitalPinToInterrupt(interruptPin_), serialConnect, RISING);
}

void Handshake::serialConnect()
{
  if (connectState_ == DISCONNECTED)
  {
    connectState_ = CONNECTING;
  }
}

Handshake::~Handshake()
{
  onConnected_ = NULL;
}

void Handshake::Update()
{
  switch (connectState_)
  {
    case DISCONNECTED:
    {
      pinHighCount_ = 0;  
    }
    case CONNECTING:
    {
      if (pinHighCount_ < 2)
      {
        if (digitalRead(interruptPin_))
        {
          delay(5);
          pinHighCount_++;
        }
        else
        {
          pinHighCount_ = 0;
        }    
      }
      else
      {
        connectState_ = CONNECTED;
        if (onConnected_ != NULL)
        {
          onConnected_();
        }
      }
    }
    break;
    case CONNECTED:
    {
      delay(5);
      if (digitalRead(interruptPin_) == LOW)
      {
        connectState_ = DISCONNECTED;
      }
    }
    break;
  }
}