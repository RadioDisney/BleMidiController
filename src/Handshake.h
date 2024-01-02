#ifndef HANDSHAKE
#define HANDSHAKE

#include <Arduino.h>

enum ConnectedState
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
};

class Handshake
{
private:
    byte interruptPin_ = 4;

    int pinHighCount_ = 0;

    static volatile ConnectedState connectState_;
    static void serialConnect();

public:
    Handshake(uint8_t interruputPin);
    ~Handshake();

    void (*onConnected_)() = NULL;

    void Update();
};

#endif
