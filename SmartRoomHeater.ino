/**
   This code provide ability to controll Load with Solid State Relay SSR
   In Smart Heater project used:

        2 * DS1822+   https://datasheets.maximintegrated.com/en/ds/DS1822.pdf  9-12bit  -55-125
        1 * NodeMCU
        1 * Solid state Relay FOTEK SSR-25 DA
        1 * 220v to 5v USB PS
    Security:
        1. On WiFi disconnect the load will be disabled(KEEP mode only)
        2. Delay between disable to enable- default 60 seconds(KEEP mode only)
        3, In case temperature value is anomalous the load will be disabled
        4. Watch inside thermometer, the maximum inside temperature is MAX_POSSIBLE_TMP_INSIDE
        5. Check internet connecitivity, if there is no ping to 8.8.8.8, load will be disabled

    Production Relaease v0.2 12.12.2017


    Author: Andrey Shamis lolnik@gmail.com


*/

//deep sleep include
extern "C" {
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "mem.h"
#include "user_interface.h"
#include "cont.h"
}
#include <Time.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266Ping.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "FS.h"
#include <OneWire.h>
#include <DallasTemperature.h>
/**
 ****************************************************************************************************
*/
// WiFi settings
#define   WIFI_SSID                         "RadiationG"
#define   WIFI_PASS                         "polkalol"

#define   MESSAGE_OPT                       1
// Custom settings
#define   CHECK_TMP_INSIDE                  0                       // For disable validation of seconds thermometer use 0
#define   CHECK_INTERNET_CONNECT            1                       // For disable internet connectiviy check use 0
#define   RECONNECT_AFTER_FAILS             20

// Thermometer and wire settings
#define   ONE_WIRE_BUS                      D4                      // D4 2
#define   LOAD_VCC                          D7                      // D7 13
#define   TEMPERATURE_PRECISION             12                      // Possible value 9-12

// NTP settings
#define   NTP_SERVER                        "0.asia.pool.ntp.org"   // Pool of ntp server http://www.pool.ntp.org/zone/asia
#define   NTP_TIME_OFFSET_SEC               10800                   // Time offset
#define   NTP_UPDATE_INTERVAL_MS            60000                   // NTP Update interval - 1 min

#define   UART_BAUD_RATE                    921600

#define   LOOP_DELAY                        10                      // Wait each loop for LOOP_DELAY
// Unchangeable settings
#define   START_TEMP                        -10                     // Default value for temperature variables on start
#define   INCORRECT_EPOCH                   200000                  // Minimum value for time epoch
/**
   Set delay between load disabled to load enabled in seconds
   When the value is 60, load can be automatically enabled after 1 minutes
   in case keeped temperature is higher of current temp
*/
#define   OFF_ON_DELAY_SEC                  30

/**
   shows counter values identical to one second
   For example loop_delay=10, counter sec will be 100 , when (counter%100 == 0) happens every second
*/
#define COUNTER_IN_LOOP_SECOND              (int)(1000/LOOP_DELAY)
#define CHECK_OUTSIDE_TMP_COUNTER           (int)(COUNTER_IN_LOOP_SECOND*3)
#define CHECK_INTERNAL_COUNTER              (CHECK_OUTSIDE_TMP_COUNTER*5)
#define CHECK_INTERNET_CONNECTIVITY_CTR     (COUNTER_IN_LOOP_SECOND*120)

/**
 ****************************************************************************************************
*/
typedef enum {
  UNDEF     = 0,  //  UNKNOWN
  MANUAL    = 1,  //  Controlled by USER - manual diable, manual enable, secured by MAX_TMP
  AUTO      = 2,  //  Controlled by TIME, secured by MAX_TMP
  KEEP      = 3,  //  Controlled by BOARD, keep temperature between MAX_TMP <> TRASHHOLD_TMP, secured by MAX_TMP
} LoadModeType;

enum LogType {
  INFO      = 0,
  WARNING   = 1,
  ERROR     = 2,
  PASS      = 3,
  FAIL_t    = 4,
  CRITICAL  = 5,
  DEBUG     = 6,
} ;


/**
 ****************************************************************************************************
*/
const char          *ssid                     = WIFI_SSID;
const char          *password                 = WIFI_PASS;
int                 counter                   = 0;
bool                heaterStatus              = 0;
float               MAX_POSSIBLE_TMP          = 26;
float               MAX_POSSIBLE_TMP_INSIDE   = 35;
bool                secure_disabled           = false;
float               temperatureKeep           = 22;
float               current_temp              = START_TEMP;
float               current_temp_inside       = START_TEMP;
int                 outsideThermometerIndex   = 0;
int                 last_disable_epoch        = 0;
bool                internet_access           = 0;
unsigned short      internet_access_failures  = 0;
/**
 ****************************************************************************************************
*/
OneWire             oneWire(ONE_WIRE_BUS);
DallasTemperature   sensor(&oneWire);
ESP8266WebServer    server(80);
DeviceAddress       insideThermometer[2];       // arrays to hold device address
WiFiUDP             ntpUDP;
IPAddress           pingServer (8, 8, 8, 8);    // Ping server
LoadModeType        loadMode = MANUAL;
/**
    You can specify the time server pool and the offset (in seconds, can be changed later with setTimeOffset()).
    Additionaly you can specify the update interval (in milliseconds, can be changed using setUpdateInterval()). */
NTPClient           timeClient(ntpUDP, NTP_SERVER, NTP_TIME_OFFSET_SEC, NTP_UPDATE_INTERVAL_MS);


/**
 ****************************************************************************************************
*/
ADC_MODE(ADC_VCC);
float   getTemperature(const int dev = 0);
/**
 ****************************************************************************************************
 ****************************************************************************************************
 ****************************************************************************************************
*/

/**
  Setup the controller
*/
void setup(void) {
  //ADC_MODE(ADC_VCC);
  pinMode(LOAD_VCC, OUTPUT);
  if (CHECK_INTERNET_CONNECT) {
    internet_access = 0;
  }
  else {
    internet_access = 1;
  }
  disableLoad();
  sensor.begin();
  Serial.begin(UART_BAUD_RATE);
  Serial.println("");
  message("Serial communication started.", PASS);
  message("Starting SPIFFS....", INFO);
  SPIFFS.begin();
  message("SPIFFS startted.", PASS);
  //message("Compile SPIFFS", INFO);
  //  SPIFFS.format();

  wifi_connect();
  server_start();
  timeClient.begin();
  start_thermal();
  message("Checking ouside temperature every " + String(CHECK_OUTSIDE_TMP_COUNTER * LOOP_DELAY / 1000) + " seconds." , INFO);
  //timeClient.update();
  //delay(100);
  timeClient.forceUpdate();
  message(" ----> All started <----", PASS);

}

/**
  /////////////////////// L O O P   F U N C T I O N //////////////////////////
  ////////////////////////////////////////////////////////////////////////////
*/


void loop(void) {

  server.handleClient();

  // Check temperature for thermometer outside
  if (counter % CHECK_OUTSIDE_TMP_COUNTER == 0) {
    current_temp = getTemperature(outsideThermometerIndex);
  }
  // Check temperature for thermometer inside
  if (counter % CHECK_INTERNAL_COUNTER == 0) {
    current_temp_inside = getTemperature(getInsideThermometer());
  }

  if (!heaterStatus && loadMode == KEEP) {

    if (current_temp < _min(temperatureKeep, MAX_POSSIBLE_TMP) && current_temp > 0) {
      int current_epoch = timeClient.getEpochTime();
      if (last_disable_epoch + OFF_ON_DELAY_SEC < current_epoch)
      {
        message("Keep enabled, enable load", WARNING);
        enableLoad();
      }
      else {
        delay(100);
        //Cannot enable Keep, limited by epoch
        //message("Cannot enable Keep, limited by epoch, will be enabled in " + String((last_disable_epoch + OFF_ON_DELAY_SEC) - current_epoch ) + " seconds.", WARNING);
      }
    }
  }

  if (heaterStatus) {
    if (current_temp > MAX_POSSIBLE_TMP || (loadMode == KEEP && current_temp >= temperatureKeep)) {
      if (current_temp > MAX_POSSIBLE_TMP ) {
        message("Current temperature is bigger of possible maximum. " + String(current_temp) + ">" + String(MAX_POSSIBLE_TMP), WARNING);
      }
      else {
        message("Current temperature is bigger of defined Keep. " + String(current_temp)  + ">" + String(temperatureKeep), WARNING);
      }
      message("Disabling Load", WARNING);
      disableLoad();
      secure_disabled = true;
      //last_disable_epoch = timeClient.getEpochTime();
    }
    if (CHECK_TMP_INSIDE && current_temp_inside > MAX_POSSIBLE_TMP_INSIDE) {
      message("Disabling Load. Current temperature INSIDE is bigger of possible maximum. " + String(current_temp_inside) + ">" + String(MAX_POSSIBLE_TMP_INSIDE), WARNING);
      disableLoad();
      secure_disabled = true;
    }
  }

  if (current_temp < 1) {
    message("Disabling Load. Very LOW temperatute. " + String(current_temp), WARNING);
    disableLoad();
    secure_disabled = true;
  }

  if (CHECK_TMP_INSIDE && current_temp_inside < 1) {
    message("Disabling Load. Very LOW temperatute INSIDE. " + String(current_temp_inside), WARNING);
    disableLoad();
    secure_disabled = true;
  }


  if (WiFi.status() != WL_CONNECTED) {
    internet_access = 0;
    if (heaterStatus) {
      disableLoad();
      secure_disabled = true;
      message("No WiFi Connection. Disabling Load", WARNING);
    }
    delay(2000);

    // Check if not connected , disable all network services, reconnect , enable all network services
    if (WiFi.status() != WL_CONNECTED) {
      message("WIFI DISCONNECTED", FAIL_t);
      reconnect_cnv();
    }
  }

  if (CHECK_INTERNET_CONNECT) {
    if (counter % CHECK_INTERNET_CONNECTIVITY_CTR == 0 || !internet_access)
    {
      internet_access = Ping.ping(pingServer, 2);
      //int avg_time_ms = Ping.averageTime();
      //message("Ping result is " + String(internet_access) + " avg_time_ms:" + String(avg_time_ms), INFO);
      if (!internet_access) {
        internet_access_failures++;
      }
      else{
        internet_access_failures=0;
      }
    }
    if (!internet_access && heaterStatus) {
      disableLoad();
      secure_disabled = true;
      message("No Internet connection. Disabling Load", WARNING);
      delay(3000);
    }
    if (internet_access_failures >= RECONNECT_AFTER_FAILS) {
      internet_access_failures = 0;
      reconnect_cnv();
    }
  }
  if (counter == 100) {
    if (internet_access) {
      update_time();
    }

    print_all_info();
  }
  //ESP.deepSleep(sleepTimeS * 1000000, RF_DEFAULT);
  delay(LOOP_DELAY);
  counter++;
  if (counter >= 12000) {
    counter = 0;
    //printTemperatureToSerial();
  }
}


void reconnect_cnv() {
  close_all_services();
  delay(3000);
  if (wifi_connect()) {
    timeClient.begin();
    server_start();
    delay(200);
    timeClient.update();
  } else {
    message("Cannot reconnect to WIFI... ", FAIL_t);
    delay(3000);
  }
}

/**

*/
//static inline char *stringFromLogType(enum LogType lt)
static const char *stringFromLogType(const enum LogType lt)
{
  static const char *strings[] = {"INFO", "WARN", "ERROR", "PASS", "FAIL", "CRITICAL", "DEBUG"};
  return strings[lt];
}

/**
   Print message to Serial console
*/
void message(const String msg, const enum LogType lt) {
  if (MESSAGE_OPT) {
    if (msg.length() == 0) {
      Serial.println(msg);
    }
    else {
      Serial.println(String(timeClient.getEpochTime()) + " : " + timeClient.getFormattedTime() + " : " + String(stringFromLogType(lt)) + " : " + msg);
    }
  }
}

/**
   Start WEB server
*/
void server_start() {
  server.on("/", handleRoot);
  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });
  server.on("/el", []() {
    enableLoad();
    loadMode = MANUAL;
    handleRoot();
  });
  server.on("/dl", []() {
    disableLoad();
    loadMode = MANUAL;
    handleRoot();
  });
  server.on("/setDallasIndex", []() {
    uploadAndSaveOutsideThermometerIndex();
    handleRoot();
  });
  server.on("/keep", []() {
    last_disable_epoch = 0;
    saveLoadMode();
    handleRoot();
  });

  server.onNotFound(handleNotFound);
  message("Staring HTTP server...", INFO);
  server.begin();
  message("HTTP server started", PASS);
}

/**
   Set WiFi connection and connect
*/
bool wifi_connect() {
  WiFi.mode(WIFI_STA);       //  Disable AP Mode - set mode to WIFI_AP, WIFI_STA, or WIFI_AP_STA.
  WiFi.begin(ssid, password);

  // Wait for connection
  message("Connecting to [" + String(ssid) + "][" + String(password) + "]...", INFO);
  int con_counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
    con_counter++;
    if (con_counter % 20 == 0) {
      message("", INFO);
      message("Still connecting...", WARNING);
    }
    if (con_counter == 100) {
      message("", INFO);
      message("Cannot connect to [" + String(ssid) + "] ", FAIL_t);
      WiFi.disconnect();
      message(" ----> Disabling WiFi...", INFO);
      WiFi.mode(WIFI_OFF);
      return false;
    }
  }
  message("", INFO);
  message("Connected to [" + String(ssid) + "]  IP address: " + WiFi.localIP().toString(), PASS);
  //  Serial.print("PASS: );
  //  Serial.println(WiFi.localIP());
  if (MDNS.begin("esp8266")) {
    message("MDNS responder started", PASS);
  }
  message("-----------------------------------", INFO);
  return true;
}

/**
  Close all network services
*/
void close_all_services() {
  message(" ----> Starting close all network services <----", INFO);

  message(" ----> Closing NTP Client...", INFO);
  timeClient.end();

  message(" ----> Closing WEB Server...", INFO);
  server.close();

  message(" ----> Disconnecting WIFI...", INFO);
  WiFi.disconnect();
  message(" ----> Disabling WiFi...", INFO);
  WiFi.mode(WIFI_OFF);
  message(" ----> WiFi disabled...", INFO);

  yield();
  message(" ----> Finished closing all network services <----", INFO);
}

/**

*/
void start_thermal() {
  message("Found " + String(sensor.getDeviceCount()) + " Thermometer Dallas devices.", INFO);
  message("Parasite power is: " + String(sensor.isParasitePowerMode()), INFO);

  for (int i = 0; i < sensor.getDeviceCount(); i++) {
    if (!sensor.getAddress(insideThermometer[i], i)) {
      message("Unable to find address for Device " + String(i) , CRITICAL);
    }
    else {
      // set the resolution to 9 bit (Each Dallas/Maxim device is capable of several different resolutions)
      sensor.setResolution(insideThermometer[i], TEMPERATURE_PRECISION);
      message("Device " + String(i) + " Resolution: " + String(sensor.getResolution(insideThermometer[i])) , INFO);
    }
  }
  if (CHECK_TMP_INSIDE) {
    message("CHECK_TMP_INSIDE = True", INFO);
  }
  else {
    message("CHECK_TMP_INSIDE = False, check of internal thermometer will be disabled!", WARNING);
  }

  String outsideThermometerIndexString = read_setting("/outTmpIndex");

  if (outsideThermometerIndexString == "") {
    saveOutsideThermometerIndex(outsideThermometerIndex);
  }
  else {
    outsideThermometerIndex = outsideThermometerIndexString.toInt();
  }
}

/**

*/
String build_index() {
  //"'current_temperature': '" + String(getTemperature(outsideThermometerIndex)) + "'," +
  //"'inside_temperature': '" + String(getTemperature(getInsideThermometer())) + "'," +
  String ret_js = String("") + "load = \n{" +
                  "'boiler_mode': '" + String(loadMode) + "'," +
                  "'load_mode': '" + String(loadMode) + "'," +
                  "'internet_access': '" + String(internet_access) + "'," +
                  "'load_status': '" + String(heaterStatus) + "'," +
                  "'disbaled_by_watch': '" + String(secure_disabled) + "'," +
                  "'max_temperature': '" + String(MAX_POSSIBLE_TMP) + "'," +
                  "'max_temperature_inside': '" + String(MAX_POSSIBLE_TMP_INSIDE) + "'," +
                  "'keep_temperature': '" + String(temperatureKeep) + "'," +
                  "'current_temperature': '" + String(current_temp) + "'," +
                  "'inside_temperature': '" + String(current_temp_inside) + "'," +
                  "'flash_chip_id': '" + String(ESP.getFlashChipId()) + "'," +
                  "'flash_chip_size': '" + String(ESP.getFlashChipSize()) + "'," +
                  "'flash_chip_speed': '" + String(ESP.getFlashChipSpeed()) + "'," +
                  "'flash_chip_mode': '" + String(ESP.getFlashChipMode()) + "'," +
                  "'core_version': '" + ESP.getCoreVersion() + "'," +
                  "'sdk_version': '" + String(ESP.getSdkVersion()) + "'," +
                  "'boot_version': '" + ESP.getBootVersion() + "'," +
                  "'boot_mode': '" + String(ESP.getBootMode()) + "'," +
                  "'cpu_freq': '" + String(ESP.getCpuFreqMHz()) + "'," +
                  "'mac_addr': '" + WiFi.macAddress() + "'," +
                  "'wifi_channel': '" + String(WiFi.channel()) + "'," +
                  "'rssi': '" + WiFi.RSSI() + "'," +
                  "'sketch_size': '" + String(ESP.getSketchSize()) + "'," +
                  "'free_sketch_size': '" + String(ESP.getFreeSketchSpace()) + "'," +
                  "'temperature_precision': '" + String(TEMPERATURE_PRECISION) + "'," +
                  "'dallas_addr': '" + getAddressString(insideThermometer[outsideThermometerIndex]) + "'," +
                  "'dallas_addrs': '" + get_thermometers_addr() + "'," +
                  "'outside_therm_index': '" + String(outsideThermometerIndex) + "'," +
                  "'time_str': '" + timeClient.getFormattedTime() + "'," +
                  "'time_epoch': '" + timeClient.getEpochTime() + "'," +
                  "'hostname': '" + WiFi.hostname() + "'" +
                  "};\n";
  String ret = String("") + "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><title>Load Info</title></head>" +
               " <script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.0/jquery.min.js'></script>\n" +
               " <script src='http://tm.anshamis.com/js/heater.js'></script>\n" +
               " <link rel='stylesheet' type='text/css' href='http://tm.anshamis.com/css/heater.css'>\n" +
               "<body><script>" + ret_js + "</script>\n" +
               "<div id='content'></div>" +
               "<script>\n " +               "$(document).ready(function(){ onLoadPageLoad(); });</script>\n" +
               "</body></html>";
  return ret;
}

/**
   Update time by NTP client
*/
void update_time() {
  if (timeClient.getEpochTime() < INCORRECT_EPOCH) {
    unsigned short counter_tmp = 0;
    while (timeClient.getEpochTime() < INCORRECT_EPOCH && counter_tmp < 5) {
      message("Incorrect time, trying to update: #:" + String(counter_tmp) , CRITICAL);
      counter_tmp++;
      timeClient.update();
      timeClient.forceUpdate();
      timeClient.update();
      if (timeClient.getEpochTime() < INCORRECT_EPOCH) {
        delay(1000);
      }
      else {
        return;
      }
    }
  }
  else {
    timeClient.update();
  }
}

/**

*/
int getInsideThermometer() {
  return (1 - outsideThermometerIndex); // TODO fix it
}

/***

*/
void saveOutsideThermometerIndex(const int newIndex) {
  save_setting("/outTmpIndex", String(newIndex));
  outsideThermometerIndex = read_setting("/outTmpIndex").toInt();
}

/**
   Get Temperature
*/
float getTemperature(const int dev/*=0*/) {
  //message("Requesting device " + String(dev), DEBUG);
  sensor.setWaitForConversion(false);   // makes it async
  sensor.requestTemperatures();
  sensor.setWaitForConversion(true);    // makes it async
  return sensor.getTempCByIndex(dev);
  //return sensor.getTempC(insideThermometer[dev]);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/**
   Enable Load
*/
void enableLoad() {
  float current_temp_tmp = getTemperature(outsideThermometerIndex);
  if (current_temp_tmp > MAX_POSSIBLE_TMP) {
    message("Current temperature is bigger of possible maximum. " + String(current_temp_tmp) + ">" + String(MAX_POSSIBLE_TMP), ERROR);
  }
  else {
    secure_disabled = false;
    heaterStatus = 1;
    digitalWrite(LOAD_VCC, 1);
  }
}

/**
   Disable Load
*/
void disableLoad() {
  heaterStatus = 0;
  last_disable_epoch = timeClient.getEpochTime();
  digitalWrite(LOAD_VCC, 0);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/***
  WEB Server function
*/
void uploadAndSaveOutsideThermometerIndex() {
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "outTmpIndex") {
      saveOutsideThermometerIndex(server.arg(i).toInt());
      message("saveOutsideThermometerIndex " + String(server.arg(i)), INFO);
    }
  }
}

/***
  WEB Server function
*/
void saveLoadMode() {
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "temperatureKeep") {
      temperatureKeep = server.arg(i).toFloat();
      loadMode = KEEP;
      message("Keep temperature ", INFO);
      if (temperatureKeep > MAX_POSSIBLE_TMP) {
        temperatureKeep = MAX_POSSIBLE_TMP;
        message("Override Keep temperature to MAX_POSSIBLE_TMP " + String(temperatureKeep), INFO);
      }
    }
  }
}

/**
  WEB Server function
*/
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: " + server.uri() + "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args() + "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

/**
  WEB Server function
*/
void handleRoot() {

  //  for (uint8_t i=0; i<server.args(); i++){
  //    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  //  }
  //  message += server.client();
  String message = build_index();
  server.send(200, "text/html", message);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////



/**
   print temperature to serial
*/
void printTemperatureToSerial() {
  int dc = sensor.getDeviceCount();
  for (int i = 0 ; i < dc; i++) {
    message("Temperature[" + String(i) + "] C: " + String(getTemperature(i)), INFO);
    //Serial.println("INFO: Temperature[" + String(i) + "] C: " + String(getTemperature(i)));
  }
}

/**

*/
String get_thermometers_addr() {
  String data = "[";
  int i = 0;
  int dev_counter = sensor.getDeviceCount();
  for (i = 0; i < dev_counter; i++) {
    data = data + String("\"") + String(getAddressString(insideThermometer[i])) + String("\" , ");
  }
  data = data + "]";
  return data;
}

/**
  Convert Dallas Address to String
*/
String getAddressString(const DeviceAddress deviceAddress) {
  String ret = "";
  uint8_t i;
  for (i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) {
      ret += "0";
    }
    ret += String(deviceAddress[i], HEX);
    if (i < 7) {
      ret += ":";
    }
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/**
  Write to file content on SPIFFS
*/
void save_setting(const char* fname, String value) {
  File f = SPIFFS.open(fname, "w");
  if (!f) {
    Serial.print("Cannot open file:");
    Serial.println(fname);
    return;
  }
  f.println(value);
  Serial.print("Written:");
  Serial.println(value);
  f.close();
}

/**
  Read file content from SPIFFS
*/
String read_setting(const char* fname) {
  String s      = "";
  File f = SPIFFS.open(fname , "r");
  if (!f) {
    Serial.print("file open failed:");
    Serial.println(fname);
  }
  else {
    s = f.readStringUntil('\n');
    f.close();
  }
  return s;
}

/**
 ****************************************************************************************************
*/

/**

*/
void print_all_info() {
  message("", INFO);
  message("Heater status: " + String(heaterStatus), INFO);
  message("Flash Chip Id/Size/Speed/Mode: " + String(ESP.getFlashChipId()) + "/" + String(ESP.getFlashChipSize()) + "/" + String(ESP.getFlashChipSpeed()) + "/" + String(ESP.getFlashChipMode()), INFO);
  message("SdkVersion: " + String(ESP.getSdkVersion()) + "\tCoreVersion: " + ESP.getCoreVersion() + "\tBootVersion: " + ESP.getBootVersion(), INFO);
  message("CpuFreqMHz: " + String(ESP.getCpuFreqMHz()) + " \tBootMode: " + String(ESP.getBootMode()) + "\tSketchSize: " + String(ESP.getSketchSize()) + "\tFreeSketchSpace: " + String(ESP.getFreeSketchSpace()), INFO);
  message("HostName: " + WiFi.hostname() + "\tChannel: " + String(WiFi.channel()) + "\tRSSI: " + WiFi.RSSI() + "\tmacAddress: " + WiFi.macAddress(), INFO);


  //message("getResetReason: " + ESP.getResetReason(), INFO);
  //message("getResetInfo: " + ESP.getResetInfo(), INFO);
  //message("Address : " + getAddressString(insideThermometer[0]), INFO);
  //Serial.println(timeClient.getFormattedTime()); //Serial.println(timeClient.getEpochTime());
  //Serial.println("INFO: -----------------------------------------------------------------------");
  //Serial.println("Heater status: " + String(heaterStatus));
  //Serial.println("getFlashChipId: " + String(ESP.getFlashChipId()) + "\t\t getFlashChipSize: " + String(ESP.getFlashChipSize()));
  //Serial.println("getFlashChipSpeed: " + String(ESP.getFlashChipSpeed()) + "\t getFlashChipMode: " + String(ESP.getFlashChipMode()));
  //Serial.println("getSdkVersion: " + String(ESP.getSdkVersion()) + "\t getCoreVersion: " + ESP.getCoreVersion() + "\t\t getBootVersion: " + ESP.getBootVersion());
  //Serial.println("getCpuFreqMHz: " + String(ESP.getCpuFreqMHz()) + " \t getBootMode: " + String(ESP.getBootMode()));
  //Serial.println("HostName :" + WiFi.hostname() + "\tmacAddress: " + WiFi.macAddress() + "\t Channel : " + String(WiFi.channel()) + "\t\t\t RSSI: " + WiFi.RSSI());
  //Serial.println("getSketchSize: " + String(ESP.getSketchSize()) + "\t\t getFreeSketchSpace: " + String(ESP.getFreeSketchSpace()));
  //Serial.println("getResetReason: " + ESP.getResetReason());
  //Serial.println("getResetInfo: " + ESP.getResetInfo());
  //Serial.println("Address : " + getAddressString(insideThermometer[0]));
}

/**
 ****************************************************************************************************
*/
//const int   sleepTimeS              = 10;  // Time to sleep (in seconds):
//enum ADCMode {
//    ADC_TOUT = 33,
//    ADC_TOUT_3V3 = 33,
//    ADC_VCC = 255,
//    ADC_VDD = 255
//};

///**
//
//*/
//String build_device_info() {
//  String ret = "<pre>\t\t\t Heater status: " + String(heaterStatus);
//  ret += "\ngetFlashChipId: " + String(ESP.getFlashChipId()) + "\t\t getFlashChipSize: " + String(ESP.getFlashChipSize());
//  ret += "\ngetFlashChipSpeed: " + String(ESP.getFlashChipSpeed()) + "\t getFlashChipMode: " + String(ESP.getFlashChipMode());
//  ret += "\ngetSdkVersion: " + String(ESP.getSdkVersion()) + "\t getCoreVersion: " + ESP.getCoreVersion() + "\t\t getBootVersion: " + ESP.getBootVersion();
//  ret += "\ngetBootMode: " + String(ESP.getBootMode());
//  ret += "\ngetCpuFreqMHz: " + String(ESP.getCpuFreqMHz());
//  ret += "\nmacAddress: " + WiFi.macAddress() + "\t Channel : " + String(WiFi.channel()) + "\t\t\t RSSI: " + WiFi.RSSI();
//  ret += "\ngetSketchSize: " + String(ESP.getSketchSize()) + "\t\t getFreeSketchSpace: " + String(ESP.getFreeSketchSpace());
//  //ret += "\ngetResetReason: " + ESP.getResetReason();
//  //ret += "\ngetResetInfo: " + ESP.getResetInfo();
//  ret += "\nAddress : " + getAddressString(insideThermometer[0]) + "</pre>";
//  return ret;
//}

