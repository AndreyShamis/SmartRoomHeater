
# Smart Room Heater
This code provide ability to controll Load with Solid State Relay SSR
# Continuous Integration
[![Build Status](https://travis-ci.org/AndreyShamis/SmartRoomHeater.svg?branch=master)](https://travis-ci.org/AndreyShamis/SmartRoomHeater)

## Requirements
### Software
* **DallasTemperature** https://github.com/milesburton/Arduino-Temperature-Control-Library
* **NTPClient** https://github.com/arduino-libraries/NTPClient
* **ESP8266Ping** https://github.com/dancol90/ESP8266Ping
### Scheme
https://easyeda.com/normal/SmartRoomHeater_Scheme-ce48bbb9c10f4db7baa1a9dd215906da
https://easyeda.com/editor#id=ce48bbb9c10f4db7baa1a9dd215906da

#### Software installation
Put folders from downloaded archives under _/home/USER/Arduino/libraries/_

### Hardware
In Smart Heater project used:
* 2 * DS1822+   https://datasheets.maximintegrated.com/en/ds/DS1822.pdf  9-12bit  -55-125
* 1 * NodeMCU
* 1 * Solid state Relay FOTEK SSR-25 DA
* 1 * 220v to 5v USB PS
## Security:
1. On WiFi disconnect the load will be disabled(KEEP mode only)
2. Delay between disable to enable- default 60 seconds(KEEP mode only)
3. In case temperature value is anomalous the load will be disabled
4. Watch inside thermometer, the maximum inside temperature is MAX_POSSIBLE_TMP_INSIDE
5. Check internet connecitivity, if there is no ping to 8.8.8.8, load will be disabled
6. Add reconnect after X pings failures

## Release
- Prerelease          v0.3 16.12.2017
  Integration with https://review.gerrithub.io/
- Production Relaease v0.2 12.12.2017
- Production Relaease v0.1 29.11.2017

## Code review
For code review used https://review.gerrithub.io

Integrated with TRAVIS CI
Author: Andrey Shamis lolnik@gmail.com
@andreyshamis
