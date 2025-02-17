/*
  xsns_91_vindriktning.ino - IKEA vindriktning particle concentration sensor support for Tasmota

  Copyright (C) 2021  Marcel Ritter and Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_VINDRIKTNING
/*********************************************************************************************\
 * IKEA VINDRIKTNING PM2.5 particle concentration sensor
 *
 * This sensor uses a subset of the PM1006K LED particle sensor
 * To use Tasmota the user needs to add an ESP8266 or ESP32
 * 
 * Jan Zahradnik (https://github.com/jenicek001/Tasmota), 6.7.2022
 * Added support to control PM1006 directly from ESP8266 or ESP32 without need to keep original
 * board. ESP can now generate requests (Tx) to PM1006 and control fan
\*********************************************************************************************/

#define XSNS_91                   91

//#define VINDRIKTNING_SHOW_PM1         // Display undocumented/supposed PM1.0 values
//#define VINDRIKTNING_SHOW_PM10        // Display undocumented/supposed PM10 values

#include <TasmotaSerial.h>

#define VINDRIKTNING_DATASET_SIZE 20

#ifndef VINDRIKTNING_FAN_SECONDS_BEFORE_TELEPERIOD
#define VINDRIKTNING_FAN_SECONDS_BEFORE_TELEPERIOD 60    // Turn on PN1006 fan XX-seconds before tele_period is reached
#endif
#if VINDRIKTNING_FAN_SECONDS_BEFORE_TELEPERIOD < 60 // If there is a temperature sensor inside VINDRIKTNING housing like e.g. SCD30 or SCD40, to get accurate temperature readings (not impacted by ESP8266/ESP32 heat), it needs at least 60 seconds of active fan to get fresh air inside the housing (based on experiments)
#error "Please set VINDRIKTNING_FAN_SECONDS_BEFORE_TELEPERIOD >= 60"
#endif

TasmotaSerial *VindriktningSerial;

struct VINDRIKTNING {
#ifdef VINDRIKTNING_SHOW_PM1
  uint16_t pm1_0 = 0;
#endif  // VINDRIKTNING_SHOW_PM1
  uint16_t pm2_5 = 0;
#ifdef VINDRIKTNING_SHOW_PM10
  uint16_t pm10 = 0;
#endif  // VINDRIKTNING_SHOW_PM10
  uint8_t type = 1;
  uint8_t valid = 0;
  bool discovery_triggered = false;
} Vindriktning;

bool VindriktningReadData(void) {
  if (!VindriktningSerial->available()) {
    return false;
  }
  while ((VindriktningSerial->peek() != 0x16) && VindriktningSerial->available()) {
    VindriktningSerial->read();
  }
  if (VindriktningSerial->available() < VINDRIKTNING_DATASET_SIZE) {
    return false;
  }

  uint8_t buffer[VINDRIKTNING_DATASET_SIZE];
  VindriktningSerial->readBytes(buffer, VINDRIKTNING_DATASET_SIZE);
  VindriktningSerial->flush();  // Make room for another burst

  AddLogBuffer(LOG_LEVEL_DEBUG_MORE, buffer, VINDRIKTNING_DATASET_SIZE);

  uint8_t crc = 0;
  for (uint32_t i = 0; i < VINDRIKTNING_DATASET_SIZE; i++) {
    crc += buffer[i];
  }
  if (crc != 0) {
    AddLog(LOG_LEVEL_DEBUG, PSTR("VDN: " D_CHECKSUM_FAILURE));
    return false;
  }

  // sample data:
  //  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19
  // 16 11 0b 00 00 00 0c 00 00 03 cb 00 00 00 0c 01 00 00 00 e7
  //               |pm2_5|     |pm1_0|     |pm10 |        | CRC |
  Vindriktning.pm2_5 = (buffer[5] << 8) | buffer[6];
#ifdef VINDRIKTNING_SHOW_PM1
  Vindriktning.pm1_0 = (buffer[9] << 8) | buffer[10];
#endif  // VINDRIKTNING_SHOW_PM1
#ifdef VINDRIKTNING_SHOW_PM10
  Vindriktning.pm10 = (buffer[13] << 8) | buffer[14];
#endif  // VINDRIKTNING_SHOW_PM10

  if (!Vindriktning.discovery_triggered) {
    TasmotaGlobal.discovery_counter = 1;      // force TasDiscovery()
    Vindriktning.discovery_triggered = true;
  }
  return true;
}

/*********************************************************************************************/

void VindriktningSendRequest() { // command for PM1006 sensor - request PM values

  uint8_t tx_request[] = {0x11, 0x02, 0x0B, 0x01, 0xE1}; // see https://threadreaderapp.com/thread/1415291684569632768.html

  if (PinUsed(GPIO_VINDRIKTNING_TX)) {
    VindriktningSerial->write(tx_request, sizeof(tx_request));
  } else {
    AddLog(LOG_LEVEL_ERROR, PSTR("VDN: Can not send data to VINDRIKTNING PM1006, VINDRIKTNING Tx pin not configured"));
  }

  AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("VDN: Tx %*_H"), sizeof(tx_request), tx_request);
}

/*********************************************************************************************/

void VindriktningSecond(void) {                // Every second

  if (PinUsed(GPIO_VINDRIKTNING_TX)) { // commands for PM1006 has to be sent by Tasmota ESPxx - no original VINDRIKTNING microcontroller available
    if (PinUsed(GPIO_VINDRIKTNING_FAN)) {
      if (TasmotaGlobal.tele_period == Settings->tele_period - VINDRIKTNING_FAN_SECONDS_BEFORE_TELEPERIOD) { // VINDRIKTNING PM1006 fan control - switch fan on VINDRIKTNING_FAN_SECONDS_BEFORE_TELEPERIOD seconds before tele_period measurement
        digitalWrite(Pin(GPIO_VINDRIKTNING_FAN), HIGH);
      } else if (TasmotaGlobal.tele_period == 1 && Settings->tele_period > (VINDRIKTNING_FAN_SECONDS_BEFORE_TELEPERIOD + 1)) { // VINDRIKTNING PM1006 fan control - switch fan off 1 second after tele_period measurement, for short tele_periods do not switch fan off at all
        digitalWrite(Pin(GPIO_VINDRIKTNING_FAN), LOW);
      }
    } // if (PinUsed(GPIO_VINDRIKTNING_FAN))

    VindriktningSendRequest(); // request fresh value from VINDRIKTNING PM1006 every second, to get fresh values on web immediately (as values from other sensors)
  } // if (PinUsed(GPIO_VINDRIKTNING_TX))

  if (VindriktningReadData()) {
    Vindriktning.valid = Settings->tele_period;
  } else {
    if (Vindriktning.valid) {
      Vindriktning.valid--;
    }
  }
}

/*********************************************************************************************/

void VindriktningInit(void) {
  Vindriktning.type = 0;
  if (PinUsed(GPIO_VINDRIKTNING_RX)) {
    if (PinUsed(GPIO_VINDRIKTNING_TX)) {        // both RX and TX are used - CUBIC PM1006 sensor triggered by Tasmota ESPxx (original VINDRIKGNING MCU fully replaced by ESPxx)
      VindriktningSerial = new TasmotaSerial(Pin(GPIO_VINDRIKTNING_RX), Pin(GPIO_VINDRIKTNING_TX), 1);
      if (PinUsed(GPIO_VINDRIKTNING_FAN)) {
        pinMode(Pin(GPIO_VINDRIKTNING_FAN), OUTPUT);
      }

    } else {                                    // only RX are used - for cases CUBIC PM1006 sensor is triggered by original VINDRIKTNING MCU, Tasmota ESPxx is just reading measured value from serial port
      VindriktningSerial = new TasmotaSerial(Pin(GPIO_VINDRIKTNING_RX), -1, 1);
    }
    if (VindriktningSerial->begin(9600)) {
      if (VindriktningSerial->hardwareSerial()) { ClaimSerial(); }
      Vindriktning.type = 1;
    }
  }
}

void VindriktningShow(bool json) {
  if (Vindriktning.valid) {
    char types[16];
    strcpy_P(types, PSTR("VINDRIKTNING"));

    if (json) {
      ResponseAppend_P(PSTR(",\"%s\":{"), types);
#ifdef VINDRIKTNING_SHOW_PM1
      ResponseAppend_P(PSTR("\"PM1\":%d,"), Vindriktning.pm1_0);
#endif  // VINDRIKTNING_SHOW_PM1
      ResponseAppend_P(PSTR("\"PM2.5\":%d"), Vindriktning.pm2_5);
#ifdef VINDRIKTNING_SHOW_PM10
      ResponseAppend_P(PSTR(",\"PM10\":%d"), Vindriktning.pm10);
#endif  // VINDRIKTNING_SHOW_PM10
      ResponseJsonEnd();
#ifdef USE_DOMOTICZ
      if (0 == TasmotaGlobal.tele_period) {
#ifdef VINDRIKTNING_SHOW_PM1
        DomoticzSensor(DZ_COUNT, Vindriktning.pm1_0);	   // PM1.0
#endif  // VINDRIKTNING_SHOW_PM1
        DomoticzSensor(DZ_VOLTAGE, Vindriktning.pm2_5);	 // PM2.5
#ifdef VINDRIKTNING_SHOW_PM10
        DomoticzSensor(DZ_CURRENT, Vindriktning.pm10);	 // PM10
#endif  // VINDRIKTNING_SHOW_PM10
      }
#endif  // USE_DOMOTICZ
#ifdef USE_WEBSERVER
    } else {
#ifdef VINDRIKTNING_SHOW_PM1
        WSContentSend_PD(HTTP_SNS_ENVIRONMENTAL_CONCENTRATION, types, "1", Vindriktning.pm1_0);
#endif  // VINDRIKTNING_SHOW_PM1
        WSContentSend_PD(HTTP_SNS_ENVIRONMENTAL_CONCENTRATION, types, "2.5", Vindriktning.pm2_5);
#ifdef VINDRIKTNING_SHOW_PM10
        WSContentSend_PD(HTTP_SNS_ENVIRONMENTAL_CONCENTRATION, types, "10", Vindriktning.pm10);
#endif  // VINDRIKTNING_SHOW_PM10
#endif  // USE_WEBSERVER
    }
  }
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns91(uint32_t function) {
  bool result = false;

  if (Vindriktning.type) {
    switch (function) {
      case FUNC_EVERY_SECOND:
        VindriktningSecond();
        break;
      case FUNC_JSON_APPEND:
        VindriktningShow(1);
        break;
#ifdef USE_WEBSERVER
      case FUNC_WEB_SENSOR:
	      VindriktningShow(0);
        break;
#endif  // USE_WEBSERVER
      case FUNC_INIT:
        VindriktningInit();
        break;
    }
  }
  return result;
}

#endif  // USE_VINDRIKTNING