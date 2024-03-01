

#include <Arduino.h>
#include <Logger.h>
#define LOG_LEVEL 4
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans9pt7b.h>
#include <ArduinoJson.h>

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}

#include <AsyncMQTT_ESP32.h>

String hostname = "floortherm";

// ********************* Display Parameters ************************
#define LED_PIN 2

#define SCREEN_WIDTH 128 /// OLED display width, in pixels
#define SCREEN_HEIGHT 64 /// OLED display height, in pixels
#define OLED_RESET -1    ///-1 => no reset on 4 pin SSD1315 OLED

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
/// define screen params

#define I2C_SDA 21          /// ESP8266 NodeMCU SDA pin GPIO4 = D2
#define I2C_SCL 22          /// ESP8266 NodeMCU SCL pin GPIO5 = D1
#define SCREEN_ADDRESS 0x3C /// 0x3C for SSD1315 OLED

// ********************* WiFi Parameters ************************
#define WIFI_SSID "vtap"
#define WIFI_PASSWORD "things1250"
TimerHandle_t wifiReconnectTimer;

// ********** Time/NTP Parameters **********
const char *ntpServer = "time.venkat.com";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;
// struct tm timeinfo;

// ********************* MQTT Parameters ************************
#define MQTT_HOST IPAddress(192, 168, 0, 13)
#define MQTT_PORT 1883

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t mqttRegisterIDTimer;

// Published Topics
const char *mainPubTopic = "floortherm/"; // Topic to publish
const char *aliveTopic = "floortherm/online";

const char *willTopic = "floortherm/offline";
const char *statusTopic = "floortherm/status";

const char *alarmTopic = "floortherm/alarm";

// Subscribed Topics
const char *SubTopic = "floortherm/#";
const char *setTempTopic = "floortherm/#/set";
const char *enableHeatTopic = "floortherm/#/enable";
const char *getStatusTopic = "floortherm/get";
const char *onlineTopic = "floortherm/online";

// Topic Commands
const char *enableCommand = "enable";
const char *enCommand = "en";
const char *setCommand = "set";

const char *getCommand = "get";
const char *statusReport = "status";

char tempTopics[5][18];
char enableTopics[5][21];

// ********************* App Parameters ************************
Preferences preferences;

// App Constants
float Rref = 10000.0;
float Beta = 3894; // 3950.0;
float To = 298.15;
float Ro = 10000.0;
float adcMax = 4096;
float Vs = 3.3;

// Core System Parameters
int zoneSetTemp[] = {72, 72, 72, 72, 72};
bool zoneHeatEnable[] = {false, false, false, false, false};

// Zone Data
const char *zoneFriendlyNames[] = {"Master Bedroom", "Narayan's Room", "Office", "Shanti's Room", "Maya's Room"};
const char *zoneNames[] = {"MBR", "NAV", "OFC", "SMV", "MAV"};
int inPins[] = {32, 33, 34, 35, 36};
int outPins[] = {16, 17, 18, 19, 23};

float zoneActualTemp[] = {72.0, 72.0, 72.0, 72.0, 72.0};
int zoneReadVal[] = {2048, 2048, 2048, 2048, 2048};

bool zoneHeating[] = {false, false, false, false, false};
String zoneHeatingMode[] = {"OFF", "OFF", "OFF", "OFF", "OFF"};
int zoneHeatArrowCounter[] = {0, 0, 0, 0, 0};

unsigned long lastStatusBroadcast = 0;

bool ledOn = true;

const char delim[2] = "/";

const int docCapacity = JSON_OBJECT_SIZE(5) + 5 * JSON_OBJECT_SIZE(4);
const int roomDocCapacity = JSON_OBJECT_SIZE(4);
int logDisplayCounter = 0;
int maxOtherIndex = 0;
int myIndex = 0;
bool indexWaitDone = false;

// ********************* Debug Parameters ************************
String methodName = "FloorTherm";

bool isNullorEmpty(char *str)
{
  if ((str == NULL) || (str[0] == '\0'))
    return true;
  else
    return false;
}

bool isNullorEmpty(String str)
{
  return isNullorEmpty(str.c_str());
}

void printTimestamp(Print *_logOutput, int x)
{
  char c[20];
  time_t rawtime;
  struct tm *timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  char tim[20];

  if (timeinfo->tm_year == 70)
  {
    sprintf(c, "%10lu ", millis());
  }
  else
  {
    strftime(c, 20, "%Y%m%d %H:%M:%S", timeinfo);
  }
  _logOutput->print(c);
  _logOutput->print(": ");
  _logOutput->print(methodName);
  _logOutput->print(": ");
}

void storePrefs()
{
  String oldMethodName = methodName;
  methodName = "storePrefs()";
  Log.verboseln("Entering...");

  preferences.putInt("Z0SetTemp", zoneSetTemp[0]);
  preferences.putInt("Z1SetTemp", zoneSetTemp[1]);
  preferences.putInt("Z2SetTemp", zoneSetTemp[2]);
  preferences.putInt("Z3SetTemp", zoneSetTemp[3]);
  preferences.putInt("Z4SetTemp", zoneSetTemp[4]);

  preferences.putBool("Z0Enabled", zoneHeatEnable[0]);
  preferences.putBool("Z1Enabled", zoneHeatEnable[1]);
  preferences.putBool("Z2Enabled", zoneHeatEnable[2]);
  preferences.putBool("Z3Enabled", zoneHeatEnable[3]);
  preferences.putBool("Z4Enabled", zoneHeatEnable[4]);

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void loadPrefs()
{
  String oldMethodName = methodName;
  methodName = "loadPrefs()";
  Log.verboseln("Entering...");

  bool doesExist = preferences.isKey("Z0SetTemp");

  if (doesExist)
  {
    zoneSetTemp[0] = preferences.getInt("Z0SetTemp");
    zoneSetTemp[1] = preferences.getInt("Z1SetTemp");
    zoneSetTemp[2] = preferences.getInt("Z2SetTemp");
    zoneSetTemp[3] = preferences.getInt("Z3SetTemp");
    zoneSetTemp[4] = preferences.getInt("Z4SetTemp");

    zoneHeatEnable[0] = preferences.getBool("Z0Enabled");
    zoneHeatEnable[1] = preferences.getBool("Z1Enabled");
    zoneHeatEnable[2] = preferences.getBool("Z2Enabled");
    zoneHeatEnable[3] = preferences.getBool("Z3Enabled");
    zoneHeatEnable[4] = preferences.getBool("Z4Enabled");
  }
  else
  {
    Log.warningln("Could not find Preferences!");
    storePrefs();
  }

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void buildCommandTopics()
{
  String oldMethodName = methodName;
  methodName = "buildCommandTopics()";
  Log.verboseln("Entering...");

  for (int i = 0; i < 5; i++)
  {
    sprintf(tempTopics[i], "floortherm/%s/set", zoneNames[i]);
    sprintf(enableTopics[i], "floortherm/%s/enable", zoneNames[i]);
  }

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void setIndex()
{
  String oldMethodName = methodName;
  methodName = "setIndex()";
  Log.verboseln("Entering...");

  indexWaitDone = true;
  char idx[1];
  sprintf(idx, "%d", maxOtherIndex + 1);
  Log.infoln("Publishing FloorTherm Index %s at QoS 0", idx);
  mqttClient.publish(aliveTopic, 1, false, idx);

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void connectToWifi()
{
  String oldMethodName = methodName;
  String methodName = "connectToWifi()";

  Log.infoln("Connecting to Wi-Fi...");
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname.c_str());
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  methodName = oldMethodName;
}

void connectToMqtt()
{
  String oldMethodName = methodName;
  String methodName = "connectToMqtt()";

  Log.infoln("Connecting to MQTT...");
  mqttClient.connect();

  methodName = oldMethodName;
}

void WiFiEvent(WiFiEvent_t event)
{
  String oldMethodName = methodName;
  methodName = "WiFiEvent(WiFiEvent_t event)";
  Log.verboseln("Entering...");

  switch (event)
  {
#if USING_CORE_ESP32_CORE_V200_PLUS

  case ARDUINO_EVENT_WIFI_READY:
    Log.infoln("WiFi ready");
    break;

  case ARDUINO_EVENT_WIFI_STA_START:
    Log.infoln("WiFi STA starting");
    break;

  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    Log.infoln("WiFi STA connected");
    break;

  case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    Log.infoln("Connected to Wi-Fi. IP address: %p", WiFi.localIP());
    Log.infoln("Connecting to NTP Server...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Log.infoln("Connected to NTP Server!");
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    char tim[20];
    strftime(tim, 20, "%d/%m/%Y %H:%M:%S", timeinfo);
    Log.infoln("Local Time: %s", tim);

    Log.infoln("Connecting to MQTT Broker...");
    connectToMqtt();
    break;

  case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    Log.infoln("WiFi lost IP");
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    Log.infoln("Disconnected from Wi-Fi. (Lost connection to WiFi)");
    Log.infoln("Stop mqttReconnectTimer");

    xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
    Log.infoln("Reconnecting to WiFi...");
    xTimerStart(wifiReconnectTimer, 0);
    break;
#else

  case SYSTEM_EVENT_STA_GOT_IP:
    Log.infoln("WiFi connected");
    Log.infoln("IP address: ");
    Log.infoln(WiFi.localIP());
    connectToMqtt();
    break;

  case SYSTEM_EVENT_STA_DISCONNECTED:
    Log.infoln("WiFi lost connection");
    xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
    xTimerStart(wifiReconnectTimer, 0);
    break;
#endif

  default:
    break;
  }

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void printSeparationLine()
{
  Log.infoln("************************************************");
}

void onMqttConnect(bool sessionPresent)
{
  String oldMethodName = methodName;
  methodName = "onMqttConnect(bool sessionPresent)";
  Log.verboseln("Entering...");

  Log.infoln("Connected to MQTT broker: %p , port: %d", MQTT_HOST, MQTT_PORT);
  Log.infoln("PubTopic:  %s", mainPubTopic);

  // printSeparationLine();
  Log.infoln("Session present: %T", sessionPresent);

  uint16_t packetIdSub = mqttClient.subscribe(SubTopic, 2);
  Log.infoln("Subscribing at QoS 2, packetId: %u", packetIdSub);

  mqttClient.setWill(willTopic, 1, true, "1");
  Log.infoln("Set Last Will and Testament message.");

  xTimerStart(mqttRegisterIDTimer, 0);

  // printSeparationLine();

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  String oldMethodName = methodName;
  methodName = "onMqttDisconnect(AsyncMqttClientDisconnectReason reason)";
  Log.verboseln("Entering...");

  (void)reason;

  Log.warningln("Disconnected from MQTT.");

  if (WiFi.isConnected())
  {
    Log.infoln("Reconnecting to MQTT broker.");
    xTimerStart(mqttReconnectTimer, 0);
  }

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void onMqttSubscribe(const uint16_t &packetId, const uint8_t &qos)
{
  String oldMethodName = methodName;
  methodName = "onMqttSubscribe(const uint16_t &packetId, const uint8_t &qos)";
  // methodName = __PRETTY_FUNCTION__;

  Log.infoln("Subscribe acknowledged.");
  // Log.infoln("  packetId: %u    qos:  %u", packetId, qos);

  methodName = oldMethodName;
}

void onMqttUnsubscribe(const uint16_t &packetId)
{
  String oldMethodName = methodName;
  methodName = "onMqttUnsubscribe(const uint16_t &packetId)";

  // Log.infoln("Unsubscribe acknowledged.  packetId: %u", packetId);

  methodName = oldMethodName;
}

void publishZoneAlarmMessage(char *zoneName, char *message)
{
  String oldMethodName = methodName;
  methodName = "publishZoneAlarmMessage()";

  const char *slash = "/";

  char *topic = strdup(alarmTopic);
  strcat(topic, slash);
  strcat(topic, zoneName);

  char *alarmMessage = message;
  const char *msg = alarmMessage;
  mqttClient.publish(topic, 0, false, alarmMessage);

  methodName = oldMethodName;
}

void logMQTTMessage(char *topic, int len, char *payload)
{
  String oldMethodName = methodName;
  methodName = "logMQTTMessage(char *topic, int len, char *payload)";
  Log.infoln("Topic: %s", topic);

  // Log.infoln("Payload Length: %d", len);

  if (!isNullorEmpty(payload))
  {
    Log.verbose("Payload: " CR);
    Log.verboseln("%s", payload);
  }

  methodName = oldMethodName;
}

String getRoomStatusJson(int i)
{
  String oldMethodName = methodName;
  methodName = "getRoomStatusJson()";
  Log.verboseln("Entering...");

  StaticJsonDocument<roomDocCapacity> doc;
  String payload;

  doc["CurrentTemp"] = zoneActualTemp[i];
  doc["Enabled"] = zoneHeatEnable[i];
  doc["SetTemp"] = zoneSetTemp[i];
  doc["Heating"] = zoneHeating[i];

  Log.infoln("Serializing Status JSON");
  serializeJson(doc, payload);

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
  return payload;
}

String getStatusJson()
{
  String oldMethodName = methodName;
  methodName = "getStatusJson()";
  Log.verboseln("Entering...");

  StaticJsonDocument<docCapacity> doc;
  String payload;

  for (int i = 0; i < 5; i++)
  {
    doc[zoneNames[i]]["CurrentTemp"] = zoneActualTemp[i];
    doc[zoneNames[i]]["Enabled"] = zoneHeatEnable[i];
    doc[zoneNames[i]]["SetTemp"] = zoneSetTemp[i];
    doc[zoneNames[i]]["Heating"] = zoneHeating[i];
  }

  Log.infoln("Serializing Status JSON");
  serializeJson(doc, payload);

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
  return payload;
}

void publishHeatingStatus()
{
  String oldMethodName = methodName;
  methodName = "publishHeatingStatus()";
  Log.verboseln("Entering...");

  // Publish Status
  String oDoc = getStatusJson();
  const char *doc = oDoc.c_str();
  logMQTTMessage((char *)statusTopic, strlen(doc), (char *)doc);
  Log.infoln("Publishing Status at QoS 0");
  mqttClient.publish(statusTopic, 0, false, doc);
  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void onMqttMessage(char *topic, char *payload, const AsyncMqttClientMessageProperties &properties,
                   const size_t &len, const size_t &index, const size_t &total)
{
  String oldMethodName = methodName;
  methodName = "onMqttMessage()";
  Log.verboseln("Entering...");

  // This safely extracts the proper payload message
  //(void)payload;
  char msg[len + 1];
  memcpy(msg, payload, len);
  msg[len] = 0;

  String recTopic = String(topic);
  bool foundZone = false;

  if (strcmp(topic, statusTopic) == 0)
  {
  }
  else
  {
    logMQTTMessage(topic, len, msg);

    if (strcmp(topic, getStatusTopic) == 0) // This is a request for status
    {
      Log.infoln("Processing GET command!");
      // Publish Heating Status
      publishHeatingStatus();
    }
    else if (strcmp(topic, onlineTopic) == 0) // This is a request for status
    {
      int otherIndex = 0;
      otherIndex = atoi(msg);
      if (indexWaitDone)
      {
        Log.infoln("Received own index: %d", otherIndex);
      }
      else
      {
        Log.infoln("Found other floortherm with index: %d", otherIndex);
        if (maxOtherIndex < otherIndex)
          maxOtherIndex = otherIndex;
      }
    }
    else
    {
      for (int i = 0; i < 5; i++)
      {
        // String
        if (strcmp(topic, tempTopics[i]) == 0)
        {
          int sentval = atoi(msg);
          if (zoneSetTemp[i] != sentval)
          {
            // Send MQTT message that Set Temp Changed
            // Log.infoln("%s Set Temp changed: %dF ---> %dF", zoneNames[i], zoneSetTemp[i], sentval);
            publishHeatingStatus();
            zoneSetTemp[i] = sentval;
            storePrefs();
          }
        }
        else if (strcmp(topic, enableTopics[i]) == 0)
        {
          int sentval = atoi(msg);
          if (zoneHeatEnable[i] != (bool)sentval)
          {
            // Send MQTT message that Enabled State Changed
            Log.infoln("%s Enable State Changed from %s", zoneHeatEnable[i] ? "Disabled ---> Enabled" : "Enabled ---> Disabled");
            zoneHeatEnable[i] = (bool)sentval;
            storePrefs();
          }
        }
      }
    }
  }

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void onMqttPublish(const uint16_t &packetId)
{
  String oldMethodName = methodName;
  methodName = "onMqttPublish()";
  // Log.infoln("Publish acknowledged.  packetId: %s", packetId);

  methodName = oldMethodName;
}

float ConvertValToTemp(int Vo)
{
  String oldMethodName = methodName;
  methodName = "ConvertValToTemp(int Vo)";
  Log.verboseln("Entering...");

  float Vout, Rt = 0;
  float T, Tc, Tf = 0;

  Vout = Vo * Vs / adcMax;
  Rt = Rref * Vout / (Vs - Vout);
  T = 1 / (1 / To + log(Rt / Ro) / Beta); // Temperature in Kelvin
  Tc = T - 273.15;                        // Celsius
  Tf = Tc * 9 / 5 + 32;                   // Fahrenheit

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
  return Tf;
}

void GetTemps()
{
  String oldMethodName = methodName;
  methodName = "GetTemps()";
  Log.verboseln("Entering...");

  Log.verboseln("Reading Temps");

  for (int i = 0; i < 5; i++)
  {
    int avg = 0;
    for (int j = 0; j < 10; j++)
    {
      int readVal = analogRead(inPins[i]);
      avg += readVal;
      delay(10);
    }
    zoneReadVal[i] = avg / 10.0;
    zoneActualTemp[i] = ConvertValToTemp(zoneReadVal[i]);
  }

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void SetHeatControl()
{
  String oldMethodName = methodName;
  methodName = "SetHeatControl()";
  Log.verboseln("Entering...");

  for (int i = 0; i < 5; i++)
  {
    if (zoneActualTemp[i] < 90)
    {

      // Are we allowed to heat?
      if (zoneHeatEnable[i])
      { // Yes - Decide if we should heat.
        // Cool enough to think about heating?
        if ((zoneActualTemp[i] <= (zoneSetTemp[i] + 0.5)))
        { // Yes - Decide whether to turn on heat.

          // Cool enough to turn on heat?
          if (zoneActualTemp[i] < (zoneSetTemp[i] - 0.5))
          { // Yes - Heat it up!
            Log.verboseln("%s HEATING", zoneNames[i]);
            zoneHeating[i] = true;
            zoneHeatingMode[i] = "HEATING";
          }
          else
          { // No - We're in the zone (+/- 0.5F). No change until upper or lower bound is reached.
            // IDLE - if it is ON, leave it on i.e. still Heating
            //        if it is OFF, leave it OFF i.e. cooling down from (zoneSetTemp[i] +1)
            // zoneHeatingMode[i] = "IDLE";
            // Log.verboseln("", zoneNames[i]);
          }
        }
        else
        { // No - Hot enough, stop heating.
          Log.verboseln("%s IDLE", zoneNames[i]);
          zoneHeating[i] = false;
          zoneHeatingMode[i] = "IDLE";
        }
      }
      else // NOT zoneHeatEnable[i]
      {
        const char *warningMessage = "Unrequested Heating!!!";
        if (zoneHeating[i])
        {
          Log.verboseln("!!! ERROR !!! %s", warningMessage);
          // Should also send MQTT message to alert someone
          publishZoneAlarmMessage(strdup(zoneNames[i]), strdup(warningMessage));
        }
        Log.verboseln("!!! ERROR !!! %s - Shutting OFF %s", warningMessage, zoneNames[i]);
        zoneHeating[i] = false;
        zoneHeatingMode[i] = "OFF";
      }

      //****************************************
      // The ONLY place that heating gets written
      //
      digitalWrite(outPins[i], zoneHeating[i]);
      //
      // ***************************************
    }
    else
    {
      const char *warningMessage = "OVERHEATING";
      Log.verboseln("!!! ERROR !!! %s", warningMessage);
      // Should also send MQTT message to alert someone
      publishZoneAlarmMessage(strdup(zoneNames[i]), strdup(warningMessage));

      Log.verboseln("!!! ERROR !!! %s - Shutting OFF %s", warningMessage, zoneNames[i]);
      zoneHeating[i] = false;
      zoneHeatingMode[i] = "OFF";
    }
  }

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void logHeatingStatus()
{
  String oldMethodName = methodName;
  methodName = "logHeatingStatus()";
  // Log.verboseln("Entering...");
  String statusMessage = "Status:\n";

  for (int j = 0; j < 5; j++)
  {
    String err = "";
    if ((!zoneHeatEnable[j] && zoneHeating[j]) || ((zoneActualTemp[j] > 90) && zoneHeating[j]))
      err = "!!! ERROR !!!";

    String heating = "IDLE";
    if (zoneHeating[j])
      heating = "HEATING";

    Log.infoln("%s: Enabled: %T     Current: %F     Target: %i     Heating: %s     %s", zoneNames[j], zoneActualTemp[j], zoneSetTemp[j], heating.c_str(), err.c_str());

  }

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void displayHeatingStatus()
{
  String oldMethodName = methodName;
  methodName = "displayHeatingStatus()";
  Log.verboseln("Entering...");

  int x = 35;
  int y = 1;

  display.clearDisplay();
  display.display();
  display.setCursor(x, y);
  display.print("FloorTherm");
  y = 15;
  for (int j = 0; j < 5; j++)
  {
    x = 1;
    display.setCursor(x, y);
    display.print(zoneNames[j]);

    // Log.infoln();

    x += 35;
    display.setCursor(x, y);
    display.print(zoneActualTemp[j], 0);
    display.print("F");

    x += 15;
    display.setCursor(x, y);
    if (zoneHeatEnable[j])
    {
      if (zoneHeating[j])
      {
        // display.print("HEATING");
        for (int k = 0; k < 9; k++)
        {
          x += 5;
          if (k == zoneHeatArrowCounter[j])
          {
            display.setCursor(x, y);
            display.print(">");
          }
        }
        zoneHeatArrowCounter[j]++;
        if (zoneHeatArrowCounter[j] > 8)
        {
          zoneHeatArrowCounter[j] = 0;
        }
      }
      else
      {
        x += 25;
        display.print("IDLE");
      }
      x += 10;
      display.setCursor(x, y);
      display.print(zoneSetTemp[j], 0);
      display.print("F");

      x += 10;
      display.setCursor(x, y);
    }
    else
    {
      x += 40;
      display.print("OFF");
    }
    y += 10;
  }
  display.display();
  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void setupDisplay()
{
  String oldMethodName = methodName;
  methodName = "setupDisplay()";
  Log.verboseln("Entering...");

  Wire.begin(I2C_SDA, I2C_SCL); /// #2 Identify SDA & SLC pins for your board
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    /// Mode + Screen Address
    Log.warningln("Display failed to initialize");
    return;
  }
  else
  {
    Log.infoln("Display initializing...");
    delay(200);                  /// time for board to initialize?
    display.clearDisplay();      ///
    display.setTextColor(WHITE); ///
    // display.setFont(&FreeSans9pt7b);
    display.setTextSize(2);      ///
    display.setCursor(5, 1);     ///
    display.print("FloorTherm"); ///
    display.setTextSize(1);      ///
    display.setCursor(1, 25);
    display.print("Starting");
    display.display(); /// needed to actually display the message
    delay(5000);       /// time for message to stay up
    display.clearDisplay();
    display.display();
    Log.infoln("Display setup complete!");
  }

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void setup()
{
  String oldMethodName = methodName;
  methodName = "setup()";

  preferences.begin("ACclimate", false);

  Serial.begin(115200);

  while (!Serial && millis() < 5000)
    ;

  delay(500);

  Log.begin(LOG_LEVEL, &Serial);
  Log.setPrefix(printTimestamp);
  Log.setShowLevel(false);

  Log.infoln("FloorTherm starting...");

  for (int i = 0; i < 5; i++)
    pinMode(outPins[i], OUTPUT);

  pinMode(LED_PIN, OUTPUT);

  buildCommandTopics();

  loadPrefs();

  setupDisplay();

  GetTemps();

  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0,
                                    reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0,
                                    reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
  mqttRegisterIDTimer = xTimerCreate("indexTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0,
                                     reinterpret_cast<TimerCallbackFunction_t>(setIndex));

  WiFi.onEvent(WiFiEvent);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  connectToWifi();

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}

void loop()
{
  String oldMethodName = methodName;
  methodName = "loop()";

  GetTemps();
  SetHeatControl();
  displayHeatingStatus();
  // delay(1000);

  bool anyEnabled = zoneHeatEnable[0] || zoneHeatEnable[1] || zoneHeatEnable[2] || zoneHeatEnable[3];
  unsigned long rightNow = millis();

  if (rightNow > lastStatusBroadcast + 60000)
  {
    publishHeatingStatus();
    logHeatingStatus();
    lastStatusBroadcast = millis();
  }

  ledOn = !ledOn;
  digitalWrite(LED_PIN, ledOn);

  Log.verboseln("Exiting...");
  methodName = oldMethodName;
}