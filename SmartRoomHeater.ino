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
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "FS.h"
#include <OneWire.h>
#include <DallasTemperature.h>

typedef enum {
  UNDEF   = 0,  //  UNKNOWN
  MANUAL  = 1,  //  Controlled by USER - manual diable, manual enable, secured by MAX_TMP
  AUTO    = 2,  //  Controlled by TIME, secured by MAX_TMP
  KEEP    = 3,  //  Controlled by BOARD, keep temperature between MAX_TMP <> TRASHHOLD_TMP, secured by MAX_TMP
} LoadModeType;

#define   ONE_WIRE_BUS              D4 //D4 2
#define   HEATER_VCC                D7 //D7 13
#define   NUMBER_OF_DEVICES         1
#define   CS_PIN                    D3
#define   TEMPERATURE_PRECISION     11
const char  *ssid                   = "RadiationG";
const char  *password               = "polkalol";

const int   sleepTimeS              = 10;  // Time to sleep (in seconds):
int         counter                 = 0;
bool        heaterStatus            = 0;
float       MAX_POSSIBLE_TMP        = 26;
bool        secure_disabled         = false;
float       temperatureKeep         = 22;
float       current_temp            = -10;
OneWire             oneWire(ONE_WIRE_BUS);
DallasTemperature   sensor(&oneWire);
ESP8266WebServer    server(80);
DeviceAddress       insideThermometer[2]; // arrays to hold device address
WiFiUDP ntpUDP;


// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionaly you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 10800, 60000);

LoadModeType loadMode = MANUAL;
//enum ADCMode {
//    ADC_TOUT = 33,
//    ADC_TOUT_3V3 = 33,
//    ADC_VCC = 255,
//    ADC_VDD = 255
//};
ADC_MODE(ADC_VCC);
String  getAddressString(DeviceAddress deviceAddress);
void    disableHeater();
void    enableHeater();
float   getTemperature(int dev = 0);
String  printTemperatureToSerial();
String  read_setting(const char* fname);
void    save_setting(const char* fname, String value);


////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/**

*/
void setup(void) {
  //ADC_MODE(ADC_VCC);
  pinMode(HEATER_VCC, OUTPUT);
  disableHeater();
  sensor.begin();
  Serial.begin(921600);
  Serial.println("");
  Serial.println("PASS: Serial communication started.");
  Serial.println("INFO: Starting SPIFFS...");
  SPIFFS.begin();
  Serial.println("PASS: SPIFFS startted.");
  //  Serial.println("INFO: Compile SPIFFS");
  //  SPIFFS.format();

  WiFi.mode(WIFI_AP_STA);       //  Disable AP Mode
  WiFi.begin(ssid, password);

  // Wait for connection
  Serial.print("INFO: Connecting to ");
  Serial.print(ssid);
  Serial.print(password);
  Serial.println("...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("PASS: Connected to ");
  Serial.println(ssid);
  Serial.print("PASS:  IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp8266")) {
    Serial.println("PASS: MDNS responder started");
  }
  Serial.println("-----------------------------------");
  Serial.print("Found ");
  Serial.print(sensor.getDeviceCount(), DEC);
  Serial.println(" devices.");
  Serial.print("Parasite power is: ");
  if (sensor.isParasitePowerMode()) {
    Serial.println("INFO: isParasitePowerMode ON");
  }
  else {
    Serial.println("INFO: isParasitePowerMode OFF");
  }
  for (int i = 0; i < sensor.getDeviceCount(); i++) {
    if (!sensor.getAddress(insideThermometer[i], i)) {
      Serial.println("Unable to find address for Device 0");
    }
    else {
      // set the resolution to 9 bit (Each Dallas/Maxim device is capable of several different resolutions)
      sensor.setResolution(insideThermometer[i], TEMPERATURE_PRECISION);
    }
  }

  //  if (!sensor.getAddress(insideThermometer[1], 0)) {
  //    Serial.println("Unable to find address for Device 0");
  //  }
  //  sensor.setResolution(insideThermometer[0], 11);;
  //  sensor.setResolution(insideThermometer[1], 11);
  server.on("/", handleRoot);
  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });
  server.on("/el", []() {
    enableHeater();
    loadMode = MANUAL;
    handleRoot();
  });
  server.on("/dl", []() {
    disableHeater();
    loadMode = MANUAL;
    handleRoot();
  });
  server.on("/keep", []() {
    saveLoadMode();
    handleRoot();
  });

  server.onNotFound(handleNotFound);
  Serial.println("INFO: Staring HTTP server...");
  server.begin();
  Serial.println("PASS: HTTP server started");
  //  save_setting("/ssid", ssid);
  //  save_setting("/password", password);
  Serial.println(read_setting("/ssid"));
  Serial.println(read_setting("/password"));
  // Sleep
  //Serial.println("ESP8266 in sleep mode");
  //ESP.deepSleep(sleepTimeS * 1000000, RF_DEFAULT);
  timeClient.begin();
  timeClient.update();

}

/**
  ////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////
*/
void loop(void) {

  server.handleClient();
  if (counter % 50 == 0) {
    current_temp = getTemperature();
  }
  if (loadMode == KEEP && !heaterStatus) {
    if (current_temp < temperatureKeep &&  current_temp < MAX_POSSIBLE_TMP && current_temp > 0) {
      Serial.println("WARNING: Keep enabled, enable load");
      enableHeater();
    }
  }
  if (heaterStatus) {
    if (current_temp > MAX_POSSIBLE_TMP || (loadMode == KEEP && current_temp > temperatureKeep)) {
      Serial.println("WARNING: Current temperature is bigger of possible maximum. " + String(current_temp) + ">" + String(MAX_POSSIBLE_TMP));
      Serial.println("WARNING: Disabling Load");
      disableHeater();
      secure_disabled = true;
    }
  }

  if (current_temp < 1) {
    Serial.println("WARNING: Very LOW temperatute. " + String(current_temp));
    Serial.println("WARNING: Disabling Load");
    disableHeater();
    secure_disabled = true;
  }

  if (counter > 1000) {
    counter = 0;
    printTemperatureToSerial();
  }
  if (counter == 5) {
    Serial.println("INFO: -----------------------------------------------------------------------");
    Serial.println("Heater status: " + String(heaterStatus));
    Serial.println("getFlashChipId: " + String(ESP.getFlashChipId()) + "\t\t getFlashChipSize: " + String(ESP.getFlashChipSize()));
    Serial.println("getFlashChipSpeed: " + String(ESP.getFlashChipSpeed()) + "\t getFlashChipMode: " + String(ESP.getFlashChipMode()));
    Serial.println("getSdkVersion: " + String(ESP.getSdkVersion()) + "\t getCoreVersion: " + ESP.getCoreVersion() + "\t\t getBootVersion: " + ESP.getBootVersion());
    Serial.println("getBootMode: " + String(ESP.getBootMode()));
    Serial.println("getCpuFreqMHz: " + String(ESP.getCpuFreqMHz()));
    Serial.println("HostName :" + WiFi.hostname() + "\tmacAddress: " + WiFi.macAddress() + "\t Channel : " + String(WiFi.channel()) + "\t\t\t RSSI: " + WiFi.RSSI());
    Serial.println("getSketchSize: " + String(ESP.getSketchSize()) + "\t\t getFreeSketchSpace: " + String(ESP.getFreeSketchSpace()));
    //Serial.println("getResetReason: " + ESP.getResetReason());
    //Serial.println("getResetInfo: " + ESP.getResetInfo());
    Serial.println("Address : " + getAddressString(insideThermometer[0]));
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WIFI DISCONNECTED");
    delay(500);
  }

  //ESP.deepSleep(sleepTimeS * 1000000, RF_DEFAULT);
  delay(100);
  counter++;
  if (counter % 200 == 0) {

    Serial.println(timeClient.getFormattedTime());
    //Serial.println(timeClient.getEpochTime());
  }

}

String get_thermometers_addr() {
  String data = "[";
  int i = 0;
  int counter = sensor.getDeviceCount();
  for (i = 0; i < counter; i++) {
    data = data + String("\"") + String(getAddressString(insideThermometer[i])) + String("\" , ");
    #Serial.println("Build  " + String(i) + " : " + String(getAddressString(insideThermometer[i])) + " "  + data);
  }
  data = data + "]";
  return data;
}

String build_index() {
  String ret_js = String("") + "load = \n{" +
                  "'boiler_mode': '" + String(loadMode) + "'," +
                  "'load_mode': '" + String(loadMode) + "'," +
                  "'load_status': '" + String(heaterStatus) + "'," +
                  "'disbaled_by_watch': '" + String(secure_disabled) + "'," +
                  "'max_temperature': '" + String(MAX_POSSIBLE_TMP) + "'," +
                  "'keep_temperature': '" + String((int)temperatureKeep) + "'," +
                  "'current_temperature': '" + String(printTemperatureToSerial()) + "'," +
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
                  "'dallas_addr': '" + getAddressString(insideThermometer[0]) + "'," +
                  "'dallas_addrs': '" + get_thermometers_addr() + "'," +
                  "'time_str': '" + timeClient.getFormattedTime() + "'," +
                  "'time_epoch': '" + timeClient.getEpochTime() + "'," +
                  "'hostname': '" + WiFi.hostname() + "'" +
                  "};\n";
  String ret = String("") + "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><title>Load Info</title></head>" +
               " <script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.0/jquery.min.js'></script>\n" +
               " <script src='http://tm.anshamis.com/js/heater.js'></script>\n" +
               " <link rel='stylesheet' type='text/css' href='http://tm.anshamis.com/css/boiler.css'>\n" +
               "<body><script>" + ret_js + "</script>\n" +
               "<div id='content'></div>" +
               "<script>\n " +               "$(document).ready(function(){ onLoadPageLoad(); });</script>\n" +
               "</body></html>";
  return ret;
}

/**

*/
String build_device_info() {
  String ret = "<pre>\t\t\t Heater status: " + String(heaterStatus);
  ret += "\ngetFlashChipId: " + String(ESP.getFlashChipId()) + "\t\t getFlashChipSize: " + String(ESP.getFlashChipSize());
  ret += "\ngetFlashChipSpeed: " + String(ESP.getFlashChipSpeed()) + "\t getFlashChipMode: " + String(ESP.getFlashChipMode());
  ret += "\ngetSdkVersion: " + String(ESP.getSdkVersion()) + "\t getCoreVersion: " + ESP.getCoreVersion() + "\t\t getBootVersion: " + ESP.getBootVersion();
  ret += "\ngetBootMode: " + String(ESP.getBootMode());
  ret += "\ngetCpuFreqMHz: " + String(ESP.getCpuFreqMHz());
  ret += "\nmacAddress: " + WiFi.macAddress() + "\t Channel : " + String(WiFi.channel()) + "\t\t\t RSSI: " + WiFi.RSSI();
  ret += "\ngetSketchSize: " + String(ESP.getSketchSize()) + "\t\t getFreeSketchSpace: " + String(ESP.getFreeSketchSpace());
  //ret += "\ngetResetReason: " + ESP.getResetReason();
  //ret += "\ngetResetInfo: " + ESP.getResetInfo();
  ret += "\nAddress : " + getAddressString(insideThermometer[0]) + "</pre>";
  return ret;
}

/**

*/
void handleRoot() {

  //  for (uint8_t i=0; i<server.args(); i++){
  //    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  //  }
  //  message += server.client();
  String message = build_index();
  server.send(200, "text/html", message);
}

/***

*/
void saveLoadMode() {
  String message = "Saving Load Mode\n\n";
  message += "URI: " + server.uri() + "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args() + "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    if (server.argName(i) == "temperatureKeep") {
      temperatureKeep = server.arg(i).toFloat();
      loadMode = KEEP;
      Serial.println("Keep temperature " + String(temperatureKeep));
      if (temperatureKeep > MAX_POSSIBLE_TMP) {
        temperatureKeep = MAX_POSSIBLE_TMP;
        Serial.println("Override Keep temperature to MAX_POSSIBLE_TMP " + String(temperatureKeep));
      }
    }
  }
}

/**

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


////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/**

*/
String getAddressString(DeviceAddress deviceAddress) {
  String ret = "";
  for (uint8_t i = 0; i < 8; i++) {
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

/**
   Enable Heater
*/
void enableHeater() {
  float current_temp = getTemperature();
  if (current_temp > MAX_POSSIBLE_TMP) {
    Serial.println("ERROR: Current temperature is bigger of possible maximum. " + String(current_temp) + ">" + String(MAX_POSSIBLE_TMP));
  }
  else {
    secure_disabled = false;
    heaterStatus = 1;
    digitalWrite(HEATER_VCC, 1);
  }
}

/**
   Disable Heater
*/
void disableHeater() {
  heaterStatus = 0;
  digitalWrite(HEATER_VCC, 0);
}


/**
   Get Temperature
*/
float getTemperature(int dev/*=0*/) {
  Serial.println("DEBUG: Requesting device " + String(dev));
  sensor.setWaitForConversion(false);  // makes it async
  sensor.requestTemperatures();
  sensor.setWaitForConversion(true);  // makes it async
  return sensor.getTempCByIndex(dev);
  //return sensor.getTempC(insideThermometer[dev]);
}

/**
   Get end print temperature to serial
*/
String printTemperatureToSerial() {
  int dc = sensor.getDeviceCount();
  for (int i = 0 ; i < dc; i++) {
    current_temp       = getTemperature(i);
    Serial.print("INFO: Temperature[" + String(i) + "] C: ");
    Serial.println(current_temp);
  }
  return String(current_temp);
}


/**

*/
void save_setting(const char* fname, String value) {
  File f = SPIFFS.open(fname, "w");
  if (!f) {
    Serial.print("Cannot open file:");
    Serial.println(fname);
    return;
  }
  f.println(value);
  Serial.print("Writed:" );
  Serial.println(value);
  f.close();
}

/**

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
