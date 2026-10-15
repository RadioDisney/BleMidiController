#include <BLEMIDI_Transport.h>

//#include <hardware/BLEMIDI_ESP32_NimBLE.h>
#include <hardware/BLEMIDI_ESP32.h>
//#include <hardware/BLEMIDI_nRF52.h>
//#include <hardware/BLEMIDI_ArduinoBLE.h>

#include "Handshake.h"
#include "ChainKeyManager.h"

#include <AceButton.h>

#include <esp_log.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 38
#endif

#define KEY1_PIN 0
#define KEY2_PIN 17

#define SERIAL2_MIDI_RATE 31250
#define SERIAL_RATE 115200   // 

// BLE 广播名称与 PartyPedal 保持一致
BLEMIDI_CREATE_INSTANCE("PartyPedal", MIDI)

unsigned long t0 = millis();
bool isConnected = false;

// -----------------------------------------------------------------------------
// 连接握手协议 (与 PartyPedal 相同):
// 客户端连接后发送 SysEx: F0 07 30 7F 20 03 CH F7 (CH = MIDI 通道 0-15)
// 设备解析后将后续所有 MIDI 消息切换到该通道发送。
// (MIDI Library 通道参数为 1-16, 故发送时使用 midiChannel + 1)
// -----------------------------------------------------------------------------
uint8_t midiChannel = 0; // 握手设置的 MIDI 通道 (0-15), 默认 0

void parseHandshakeSysEx(const uint8_t* data, size_t len)
{
  // 格式: F0 07 30 7F 20 03 CH F7
  if (len < 8 || data[0] != 0xF0 || data[len - 1] != 0xF7)
  {
    return;
  }

  if (data[1] == 0x07 && data[2] == 0x30 &&
      data[3] == 0x7F && data[4] == 0x20 &&
      data[5] == 0x03)
  {
    uint8_t channel = data[6];

    // 通道号范围检查 (0-15)
    if (channel <= 15)
    {
      midiChannel = channel;
      Serial.printf("[MIDI] Handshake: MIDI channel set to %d\r\n", channel);
    }
  }
}

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
    if (isConnected)
    {
      MIDI.sendNoteOn(pitch, 100, midiChannel + 1);
    }
    chainKey.setColor(deviceId, 0x00, 0xFF, 0x00); // green while held
    break;
  case CHAIN_KEY_EVENT_RELEASED:
    if (isConnected) {
      MIDI.sendNoteOff(pitch, 0, midiChannel + 1);
    }
    chainKey.setColor(deviceId, 0x20, 0x20, 0x20); // back to idle
    break;
  // Report mode events (kept for when report mode is used):
  case CHAIN_KEY_EVENT_SINGLE_PRESS:
    if (isConnected)
    {
      MIDI.sendNoteOn(pitch, 100, midiChannel + 1);
    }
    chainKey.setColor(deviceId, 0x00, 0xFF, 0x00); // green
    break;
  case CHAIN_KEY_EVENT_DOUBLE_PRESS:
    if (isConnected)
    {
      MIDI.sendNoteOn(pitch + 12, 100, midiChannel + 1);
    }
    chainKey.setColor(deviceId, 0x00, 0x00, 0xFF); // blue
    break;
  case CHAIN_KEY_EVENT_LONG_PRESS:
    if (isConnected) {
      MIDI.sendControlChange(123, 0, midiChannel + 1); // all notes off
    }
    chainKey.setColor(deviceId, 0xFF, 0x00, 0x00); // red
    break;
  default:
    break;
  }
}

// ---------------------------------------------------------------------------
// Chain Joystick: 上下推摇杆 (Y 轴) -> MIDI Pitch Bend (弯音轮)
// 摇杆位置映射到 14bit 弯音原始值 (raw 0..16383, 回中 8192):
//   y = -127 -> 0 (最小弯音), y = 0 -> 8192 (回中), y = 127 -> 16383 (最大弯音).
// 弯音消息共 3 字节: 状态字节 0xEn (n = 通道 0..F, 对应通道 1..16),
// 数据字节1 = LSB (0x00..0x7F), 数据字节2 = MSB (0x00..0x7F);
// 原始值 = (MSB << 7) | LSB.
// 注意: MIDI 库 sendPitchBend(int) 重载接收的是有符号值 -8192..8191 (回中 0),
// 内部会 +8192 转成原始值再拆 LSB/MSB 发送, 故发送前先做 raw - 8192 转换.
// 松手摇杆自动回中, 弯音也随之复位为 8192.
// 摇杆 RGB 作为弯音指示: 回中暗白, 上推偏绿, 下拉偏红, 推得越满越亮.
// ---------------------------------------------------------------------------
#define JOY_Y_DEADZONE 8          // 摇杆 Y 轴死区 (映射值 -127..127), 死区内视为回中
#define JOY_Y_INVERT 0            // 1 = 翻转上下方向
#define JOY_RGB_INTERVAL_MS 100   // RGB 指示更新节流 (ms), 减少串联总线占用

#define JOY_BEND_MIN 0            // 14bit 弯音原始值下限
#define JOY_BEND_CENTER 8192      // 14bit 弯音回中值
#define JOY_BEND_MAX 16383        // 14bit 弯音原始值上限

int lastBend = -1;  // 上次发送的弯音原始值 (初值取不可能值, 保证首次会发送)

void OnChainJoystick(uint8_t deviceId, int8_t x, int8_t y)
{
#if JOY_Y_INVERT
  y = -y;
#endif

  ESP_LOGD("ChainJoystick", "deviceId=%d x=%d y=%d", deviceId, x, y);

  // 死区内钳位为 0 (回中)
  int yc = (abs(y) < JOY_Y_DEADZONE) ? 0 : (int)y;

  // 摇杆位置 -> 14bit 弯音原始值: y -127..127 线性映射到 0..16383 (回中 8192)
  //   y=-127 -> 0, y=0 -> 8192, y=127 算得 16384 超 14bit, 被 constrain 截到 16383
  int bend = JOY_BEND_CENTER + (yc * JOY_BEND_CENTER) / 127;
  bend = constrain(bend, JOY_BEND_MIN, JOY_BEND_MAX);

  if (bend == lastBend)
  {
    return;  // 数值未变化, 不重复发送
  }
  lastBend = bend;

  Serial.printf("Joystick id=%d x=%d y=%d -> Pitch Bend raw=%d (0..16383, center 8192)\r\n",
                deviceId, x, y, bend);

  if (isConnected)
  {
    // 库的 int 重载接收 -8192..8191 (回中 0), 内部 +8192 转回原始值并拆 3 字节:
    // 0xEn | ch, LSB = raw & 0x7F, MSB = (raw >> 7) & 0x7F
    MIDI.sendPitchBend(bend, midiChannel + 1);
  }

  // RGB 指示弯音方向 (节流更新): 回中暗白, 上推绿, 下拉红
  static unsigned long lastRgbMs = 0;
  unsigned long now = millis();
  if (now - lastRgbMs < JOY_RGB_INTERVAL_MS)
  {
    return;
  }
  lastRgbMs = now;

  if (bend > JOY_BEND_CENTER)
  {
    uint8_t g = (uint8_t)map(bend, JOY_BEND_CENTER + 1, JOY_BEND_MAX, 0x20, 0xFF);
    chainKey.setColor(deviceId, 0x00, g, 0x00);
  }
  else if (bend < JOY_BEND_CENTER)
  {
    // 越往下推 raw 越小, 红色越亮: raw 8191 -> 0x20, raw 0 -> 0xFF
    uint8_t r = (uint8_t)map(bend, JOY_BEND_CENTER - 1, JOY_BEND_MIN, 0x20, 0xFF);
    chainKey.setColor(deviceId, r, 0x00, 0x00);
  }
  else
  {
    chainKey.setColor(deviceId, 0x20, 0x20, 0x20);
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
                            // PartyPedal 握手协议: 解析通道设置 SysEx
                            parseHandshakeSysEx((const uint8_t *)message.sysexArray, message.length);
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
  chainKey.setJoystickEventHandler(OnChainJoystick);
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

    // ----- Chain Joystick setup ----------------------------------------------
    for (uint8_t i = 0; i < chainKey.joystickCount(); i++)
    {
      uint16_t id = chainKey.joystickId(i);
      chainKey.setBrightness(id, 30);
      // 暗白 = 弯音回中 (OnChainJoystick 会随弯音方向变色)
      chainKey.setColor(id, 0x20, 0x20, 0x20);
      Serial.printf("Chain Joystick id=%d ready (push up/down to pitch bend)\r\n", id);
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
      MIDI.sendNoteOn(pitch, 100, midiChannel + 1);
      break;
    case AceButton::kEventReleased:
      MIDI.sendNoteOff(pitch, 0, midiChannel + 1);
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
