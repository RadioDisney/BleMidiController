#include "ChainKeyManager.h"

ChainKeyManager::ChainKeyManager(uint8_t rxPin, uint8_t txPin,
                                 HardwareSerial *serial, unsigned long baud)
{
  serial_ = serial;
  rxPin_ = rxPin;
  txPin_ = txPin;
  baud_ = baud;

  deviceList_ = NULL;
  keyIds_ = NULL;
  prevStatus_ = NULL;
  keyCount_ = 0;
  eventHandler_ = NULL;
  pollingMode_ = false;
  joystickIds_ = NULL;
  joystickCount_ = 0;
  joystickEventHandler_ = NULL;
}

ChainKeyManager::~ChainKeyManager()
{
  if (deviceList_ != NULL)
  {
    if (deviceList_->devices != NULL)
    {
      free(deviceList_->devices);
    }
    free(deviceList_);
    deviceList_ = NULL;
  }
  if (keyIds_ != NULL)
  {
    free(keyIds_);
    keyIds_ = NULL;
  }
  if (joystickIds_ != NULL)
  {
    free(joystickIds_);
    joystickIds_ = NULL;
  }
  if (prevStatus_ != NULL)
  {
    free(prevStatus_);
    prevStatus_ = NULL;
  }
  eventHandler_ = NULL;
}

bool ChainKeyManager::begin()
{
  chain_.begin(serial_, baud_, rxPin_, txPin_);

  if (!chain_.isDeviceConnected())
  {
    Serial.println("[ChainKeyManager] no chain device connected");
    return false;
  }

  enumerateDevices();
  if (keyCount_ == 0 && joystickCount_ == 0)
  {
    Serial.println("[ChainKeyManager] no Chain Key / Joystick device found");
    return false;
  }

  Serial.printf("[ChainKeyManager] found %d key device(s), %d joystick device(s)\r\n",
                keyCount_, joystickCount_);
  for (uint8_t i = 0; i < keyCount_; i++)
  {
    Serial.printf("  key[%d] id=%d\r\n", i, keyIds_[i]);
  }
  for (uint8_t i = 0; i < joystickCount_; i++)
  {
    Serial.printf("  joystick[%d] id=%d\r\n", i, joystickIds_[i]);
  }
  return true;
}

void ChainKeyManager::enumerateDevices()
{
  uint16_t deviceNum = 0;
  chain_status_t status = chain_.getDeviceNum(&deviceNum);
  if (status != CHAIN_OK || deviceNum == 0)
  {
    Serial.printf("[ChainKeyManager] getDeviceNum failed, status=%d\r\n", status);
    return;
  }

  deviceList_ = (device_list_t *)malloc(sizeof(device_list_t));
  deviceList_->count = deviceNum;
  deviceList_->devices = (device_info_t *)malloc(sizeof(device_info_t) * deviceNum);

  if (!chain_.getDeviceList(deviceList_))
  {
    Serial.println("[ChainKeyManager] getDeviceList failed");
    return;
  }

  // Collect every device whose type is Chain Key or Chain Joystick.
  keyIds_ = (uint16_t *)malloc(sizeof(uint16_t) * deviceNum);
  joystickIds_ = (uint16_t *)malloc(sizeof(uint16_t) * deviceNum);
  keyCount_ = 0;
  joystickCount_ = 0;
  for (uint16_t i = 0; i < deviceList_->count; i++)
  {
    if (deviceList_->devices[i].device_type == CHAIN_KEY_TYPE_CODE)
    {
      keyIds_[keyCount_++] = deviceList_->devices[i].id;
    }
    else if (deviceList_->devices[i].device_type == CHAIN_JOYSTICK_TYPE_CODE)
    {
      joystickIds_[joystickCount_++] = deviceList_->devices[i].id;
    }
  }

  // Allocate per-key previous-status storage used by non-report polling.
  prevStatus_ = (uint8_t *)calloc(deviceNum, sizeof(uint8_t));
}

uint16_t ChainKeyManager::keyId(uint8_t index) const
{
  if (index >= keyCount_)
  {
    return 0;
  }
  return keyIds_[index];
}

uint16_t ChainKeyManager::joystickId(uint8_t index) const
{
  if (index >= joystickCount_)
  {
    return 0;
  }
  return joystickIds_[index];
}

bool ChainKeyManager::getJoystickXY(uint16_t deviceId, int8_t &x, int8_t &y)
{
  chain_status_t s = chain_.getJoystickMappedInt8Value(deviceId, &x, &y);
  return (s == CHAIN_OK);
}

void ChainKeyManager::update()
{
  for (uint8_t i = 0; i < keyCount_; i++)
  {
    if (pollingMode_)
    {
      pollButtonStatus(keyIds_[i], i);
    }
    else
    {
      fireButtonEvents(keyIds_[i]);
    }
  }

  pollJoysticks();
}

void ChainKeyManager::pollJoysticks()
{
  for (uint8_t i = 0; i < joystickCount_; i++)
  {
    int8_t x = 0;
    int8_t y = 0;
    if (!getJoystickXY(joystickIds_[i], x, y))
    {
      // Query failed; skip this cycle to avoid feeding stale data.
      continue;
    }
    if (joystickEventHandler_ != NULL)
    {
      joystickEventHandler_((uint8_t)joystickIds_[i], x, y);
    }
  }
}

void ChainKeyManager::pollButtonStatus(uint16_t deviceId, uint8_t index)
{
  if (prevStatus_ == NULL)
  {
    return;
  }

  uint8_t status = 0;
  if (!getButtonStatus(deviceId, status))
  {
    // Query failed; keep last known state to avoid spurious events.
    return;
  }

  uint8_t prev = prevStatus_[index];
  prevStatus_[index] = status;

  if (eventHandler_ == NULL)
  {
    return;
  }

  // Emit edge events only on level transitions.
  if (status == 1 && prev == 0)
  {
    eventHandler_((uint8_t)deviceId, CHAIN_KEY_EVENT_PRESSED);
  }
  else if (status == 0 && prev == 1)
  {
    eventHandler_((uint8_t)deviceId, CHAIN_KEY_EVENT_RELEASED);
  }
}

void ChainKeyManager::fireButtonEvents(uint16_t deviceId)
{
  if (eventHandler_ == NULL)
  {
    // Still need to drain the incoming queue so it does not grow unbounded.
    chain_button_press_type_t pressType;
    while (chain_.getKeyButtonPressStatus(deviceId, &pressType))
    {
      // discard
    }
    return;
  }

  chain_button_press_type_t pressType;
  while (chain_.getKeyButtonPressStatus(deviceId, &pressType))
  {
    ChainKeyEvent event = CHAIN_KEY_EVENT_NONE;
    switch (pressType)
    {
    case CHAIN_BUTTON_PRESS_SINGLE:
      event = CHAIN_KEY_EVENT_SINGLE_PRESS;
      break;
    case CHAIN_BUTTON_PRESS_DOUBLE:
      event = CHAIN_KEY_EVENT_DOUBLE_PRESS;
      break;
    case CHAIN_BUTTON_PRESS_LONG:
      event = CHAIN_KEY_EVENT_LONG_PRESS;
      break;
    default:
      break;
    }
    if (event != CHAIN_KEY_EVENT_NONE)
    {
      eventHandler_((uint8_t)deviceId, event);
    }
  }
}

bool ChainKeyManager::setBrightness(uint16_t deviceId, uint8_t brightness)
{
  uint8_t operationStatus = 0;
  chain_status_t status = chain_.setRGBLight(deviceId, brightness, &operationStatus);
  return (status == CHAIN_OK && operationStatus == 1);
}

bool ChainKeyManager::setColor(uint16_t deviceId, uint8_t r, uint8_t g, uint8_t b)
{
  uint8_t rgb[3] = {r, g, b};
  uint8_t operationStatus = 0;
  chain_status_t status =
      chain_.setRGBValue(deviceId, 0, 1, rgb, sizeof(rgb), &operationStatus);
  return (status == CHAIN_OK && operationStatus == 1);
}

bool ChainKeyManager::setTriggerInterval(uint16_t deviceId,
                                         button_double_click_time_t doubleClickTime,
                                         button_long_press_time_t longPressTime)
{
  uint8_t operationStatus = 0;
  chain_status_t status =
      chain_.setKeyButtonTriggerInterval(deviceId, doubleClickTime,
                                         longPressTime, &operationStatus);
  return (status == CHAIN_OK && operationStatus == 1);
}

bool ChainKeyManager::setButtonMode(uint16_t deviceId, chain_button_mode_t mode)
{
  uint8_t operationStatus = 0;
  chain_status_t status = chain_.setKeyButtonMode(deviceId, mode, &operationStatus);
  return (status == CHAIN_OK && operationStatus == 1);
}

bool ChainKeyManager::getButtonMode(uint16_t deviceId, chain_button_mode_t &mode)
{
  chain_status_t status = chain_.getKeyButtonMode(deviceId, &mode);
  return (status == CHAIN_OK);
}

bool ChainKeyManager::getButtonStatus(uint16_t deviceId, uint8_t &status)
{
  chain_status_t s = chain_.getKeyButtonStatus(deviceId, &status);
  return (s == CHAIN_OK);
}
