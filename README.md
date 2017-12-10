# Smart Room Heater
This code provide ability to controll Load with Solid State Relay SSR


## Requirements
### Software
* **Time** https://github.com/PaulStoffregen/Time
* **DallasTemperature** https://github.com/milesburton/Arduino-Temperature-Control-Library
* **NTPClient** https://github.com/arduino-libraries/NTPClient

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

## Release
- Production Relaease 29.11.2017

Author: Andrey Shamis lolnik@gmail.com
@andreyshamis
