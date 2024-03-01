

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

// Topic Commands
const char *enableCommand = "enable";
const char *enCommand = "en";
const char *setCommand = "set";

const char *getCommand = "get";
const char *statusReport = "status";




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
const char *nameSpacing[] = {"   ", "", "", "    ", "    "};
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


void loadPrefs()
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

void storePrefs()
{
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
}

void connectToWifi()
{
  Serial.println("Connecting to Wi-Fi...");
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname.c_str());
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt()
{
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event)
{
  methodName = "WiFiEvent(WiFiEvent_t event)";
  Log.verboseln("Entering...");

  switch (event)
  {
#if USING_CORE_ESP32_CORE_V200_PLUS

  case ARDUINO_EVENT_WIFI_READY:
    Serial.println("WiFi ready");
    break;

  case ARDUINO_EVENT_WIFI_STA_START:
    Serial.println("WiFi STA starting");
    break;

  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    Serial.println("WiFi STA connected");
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
    Serial.println("WiFi lost IP");
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
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    connectToMqtt();
    break;

  case SYSTEM_EVENT_STA_DISCONNECTED:
    Serial.println("WiFi lost connection");
    xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
    xTimerStart(wifiReconnectTimer, 0);
    break;
#endif

  default:
    break;
  }

  Log.verboseln("Exiting...");
}

void printSeparationLine()
{
  Serial.println("************************************************");
}

void onMqttConnect(bool sessionPresent)
{
  methodName = "onMqttConnect(bool sessionPresent)";
  Log.verboseln("Entering...");

  Log.infoln("Connected to MQTT broker: %p , port: %d", MQTT_HOST, MQTT_PORT);
  Log.infoln("PubTopic:  %s", mainPubTopic);

  // printSeparationLine();
  Log.infoln("Session present: %T", sessionPresent);

  uint16_t packetIdSub = mqttClient.subscribe(SubTopic, 2);
  Log.infoln("Subscribing at QoS 2, packetId: %u", packetIdSub);

  mqttClient.publish(aliveTopic, 1, false, "1");
  Log.infoln("Publishing at QoS 0");

  mqttClient.setWill(willTopic, 1, true, "1");
  Log.infoln("Set Last Will and Testament message.");

  // printSeparationLine();

  Log.verboseln("Exiting...");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  methodName = "onMqttConnect(bool sessionPresent)";
  Log.verboseln("Entering...");

  (void)reason;

  Log.warningln("Disconnected from MQTT.");

  if (WiFi.isConnected())
  {
    Log.infoln("Reconnecting to MQTT broker.");
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttSubscribe(const uint16_t &packetId, const uint8_t &qos)
{
  methodName = "onMqttSubscribe(const uint16_t &packetId, const uint8_t &qos)";

  Log.infoln("Subscribe acknowledged.");
  Log.infoln("  packetId: %u    qos:  %u", packetId, qos);
}

void onMqttUnsubscribe(const uint16_t &packetId)
{
  methodName = "onMqttUnsubscribe(const uint16_t &packetId)";

  Log.infoln("Unsubscribe acknowledged.  packetId: %u", packetId);
}

void publishZoneAlarmMessage(char *zoneName, char *message)
{
  const char *slash = "/";

  char *topic = strdup(alarmTopic);
  strcat(topic, slash);
  strcat(topic, zoneName);

  char *alarmMessage = message;
  const char *msg = alarmMessage;
  mqttClient.publish(topic, 0, false, alarmMessage);
}

void logMQTTMessage(char *topic, int len, char *payload)
{
  methodName = "logMQTTMessage(char *topic, int len, char *payload)";
  Log.infoln("Topic: %s", topic);

  Log.infoln("Payload Length: %d", len);

  if (!isNullorEmpty(payload))
  {
    Log.infoln("Payload: ");
    Log.infoln(payload);
  }
}

String getRoomStatusJson(int i)
{
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
  return payload;
}

String getStatusJson()
{
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
  return payload;
}

void publishHeatingStatus()
{
  methodName = "publishHeatingStatus()";
  Log.verboseln("Entering...");

  // Publish Status
  String oDoc = getStatusJson();
  const char *doc = oDoc.c_str();
  logMQTTMessage((char *)statusTopic, strlen(doc), (char *)doc);
  Log.infoln("Publishing Status at QoS 0");
  mqttClient.publish(statusTopic, 0, false, doc);
  Log.verboseln("Exiting...");
}

void onMqttMessage(char *topic, char *payload, const AsyncMqttClientMessageProperties &properties,
                   const size_t &len, const size_t &index, const size_t &total)
{
  // This safely extracts the proper payload message
  //(void)payload;
  char msg[len + 1];
  memcpy(msg, payload, len);
  msg[len] = 0;

  String recTopic = String(topic);
  bool foundZone = false;

  Serial.print("Received");
  Serial.print("  topic: ");
  Serial.println(topic);

  char *message;
  char *target;
  char *command;
  char *token;
  int i = 0;

  token = strtok(topic, delim);
  message = token;

  while (token != NULL)
  {
    token = strtok(NULL, delim);
    i++;
    if (i == 1)
    {
      target = token;
    }
    else if (i == 2)
    {
      command = token;
    }
  }

  i = 0;

  Serial.print("Message: ");
  if (message != NULL)
    Serial.println(message);
  else
    Serial.println("!!!EMPTY!!!");

  Serial.print("Target: ");
  if (target != NULL)
    Serial.println(target);
  else
    Serial.println("!!!EMPTY!!!");

  Serial.print("Command: ");
  if (command != NULL)
    Serial.println(command);
  else
    Serial.println("!!!EMPTY!!!");

  Serial.print("Payload Length: ");
  Serial.println(len);

  Serial.print("Payload: ");

  if (strcmp(target, statusReport) == 0)
  {
    Serial.println(msg);
  }
  else if (strcmp(target, getCommand) == 0)
  {
    Serial.println("Processing GET command!");
    // Publish Heating Status
    publishHeatingStatus();
  }
  else
  {
    Serial.println(msg);
    while (i < 5)
    {
      if (strcmp(target, zoneNames[i]) == 0)
      {
        int sentval = atoi(msg);
        Serial.println(sentval);
        if (strcmp(command, enableCommand) == 0)
        {
          foundZone = true;
          if (zoneHeatEnable[i] != (bool)sentval)
          {
            // Send MQTT message that Enabled State Changed
            Serial.print("Enable State Changed from ");
            if (!zoneHeatEnable[i])
            {
              Serial.println("Disabled ---> Enabled");
            }
            else
            {
              Serial.println("Enabled ---> Disabled");
            }
          }
          zoneHeatEnable[i] = (bool)sentval;
          storePrefs();
        }
        else if (strcmp(command, setCommand) == 0)
        {
          if (zoneSetTemp[i] != sentval)
          {
            // Send MQTT message that Set Temp Changed
            Serial.print(zoneSetTemp[i]);
            Serial.print("F --> ");
            Serial.print(sentval);
            Serial.println("F");
          }
          zoneSetTemp[i] = sentval;
          storePrefs();
        }
        else if (strcmp(command, getCommand) == 0)
        {
          char *topicString = strdup(statusTopic);
          strcat(topicString, "/");
          strcat(topicString, zoneNames[i]);
          Serial.print("Topic: ");
          Serial.println(topicString);
          String rDoc = getRoomStatusJson(i);
          const char *doc = rDoc.c_str();
          Serial.print("Payload: ");
          Serial.println(doc);
          mqttClient.publish(topicString, 0, false, doc);
          Serial.println("Publishing Status at QoS 0");
        }
      }
      i++;
    }
  }

  Serial.println();
}

void onMqttPublish(const uint16_t &packetId)
{
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

float ConvertValToTemp(int Vo)
{
  float Vout, Rt = 0;
  float T, Tc, Tf = 0;

  Vout = Vo * Vs / adcMax;
  Rt = Rref * Vout / (Vs - Vout);
  T = 1 / (1 / To + log(Rt / Ro) / Beta); // Temperature in Kelvin
  Tc = T - 273.15;                        // Celsius
  Tf = Tc * 9 / 5 + 32;                   // Fahrenheit

  return Tf;
}

void GetTemps()
{
  Serial.print(".");
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

    if ((zoneActualTemp[i] > 92) && (logDisplayCounter > 9))
    {
      const char *warningMessage = "Overheating above 92F!!!";

      publishZoneAlarmMessage(strdup(zoneNames[i]), strdup(warningMessage));
    }
  }
}

void SetHeatControl()
{

  for (int i = 0; i < 5; i++)
  {
    if (zoneActualTemp[i] < 90)
    {

      // Are we allowed to heat?
      if (zoneHeatEnable[i])
      {// Yes - Decide if we should heat.
        // Cool enough to think about heating?
        if ((zoneActualTemp[i] <= (zoneSetTemp[i] + 0.5)))
        {// Yes - Decide whether to turn on heat.

          // Cool enough to turn on heat?
          if (zoneActualTemp[i] < (zoneSetTemp[i] - 0.5))
          {// Yes - Heat it up!
            zoneHeating[i] = true;
            zoneHeatingMode[i] = "HEATING";
          }
          else
          {// No - We're in the zone (+/- 0.5F). No change until upper or lower bound is reached.
            // IDLE - if it is ON, leave it on i.e. still Heating
            //        if it is OFF, leave it OFF i.e. cooling down from (zoneSetTemp[i] +1)
            //zoneHeatingMode[i] = "IDLE";
          }
        }
        else
        {// No - Hot enough, stop heating.
          zoneHeating[i] = false;
          zoneHeatingMode[i] = "IDLE";
        }
      }
      else // NOT zoneHeatEnable[i]
      {
        if (zoneHeating[i])
        {
          const char *warningMessage = "Unrequested Heating!!!";
          Serial.println("!!! ERROR !!!");
          // Should also send MQTT message to alert someone
          publishZoneAlarmMessage(strdup(zoneNames[i]), strdup(warningMessage));
        }
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
      Serial.println("!!! ERROR !!!");
      // Should also send MQTT message to alert someone
      publishZoneAlarmMessage(strdup(zoneNames[i]), strdup(warningMessage));
      zoneHeating[i] = false;
      zoneHeatingMode[i] = "OFF";
    }
  }
}

void logHeatingStatus()
{
  Serial.println("Status:");
  for (int j = 0; j < 5; j++)
  {
    Serial.print(zoneNames[j]);
    // Serial.print(nameSpacing[j]);
    Serial.print("   ");
    Serial.print(": ");
    Serial.print("    ");
    Serial.print("Current: ");
    Serial.print(zoneActualTemp[j]);
    Serial.print("F");
    Serial.print("    ");
    if (zoneHeatEnable[j])
    {
      Serial.print("Enabled ");
      Serial.print("    ");
      Serial.print("Target: ");
      Serial.print(zoneSetTemp[j]);
      Serial.print("F");
      Serial.print("    ");

      if (zoneHeating[j])
      {
        Serial.print("HEATING");
      }
      else
      {
        Serial.print("NOT HEATING");
      }
    }
    else
    {
      Serial.print("Disabled");
    }

    if ((!zoneHeatEnable[j] && zoneHeating[j]) || ((zoneActualTemp[j] > 90) && zoneHeating[j]))
    {
      Serial.println("!!! ERROR !!!");
    }
    else
    {
      Serial.println();
    }
  }
  // Serial.println();
}

void displayHeatingStatus()
{
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

    // Serial.println();

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
}

void setupDisplay()
{
  Serial.println("Setting up display!");
  Wire.begin(I2C_SDA, I2C_SCL); /// #2 Identify SDA & SLC pins for your board
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    /// Mode + Screen Address
    Serial.println("Display failed to initialize");
    return;
  }
  else
  {
    Serial.println("Display initializing...");
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
    Serial.println("Display setup complete!");
  }
}

void setup()
{
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

  loadPrefs();

  setupDisplay();

  Serial.print("Reading Temps.");
  GetTemps();
  Serial.println(".");
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0,
                                    reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0,
                                    reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));

  WiFi.onEvent(WiFiEvent);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  connectToWifi();

  Serial.print("Reading Temps.");
}

void loop()
{
  if (logDisplayCounter == 0)
  {
    Serial.println("");
    Serial.print("Reading Temps");
  }
  logDisplayCounter++;
  GetTemps();
  SetHeatControl();
  displayHeatingStatus();
  if (logDisplayCounter > 9)
  {
    Serial.println("");
    Serial.println("");
    logHeatingStatus();
    logDisplayCounter = 0;
  }
  // delay(1000);

  bool anyEnabled = zoneHeatEnable[0] || zoneHeatEnable[1] || zoneHeatEnable[2] || zoneHeatEnable[3];
  unsigned long rightNow = millis();
  if (anyEnabled)
  {
    if (rightNow > lastStatusBroadcast + 60000)
    {
      publishHeatingStatus();
      lastStatusBroadcast = millis();
    }
  }
  else
  {
    lastStatusBroadcast = 0;
  }

  ledOn = !ledOn;
  digitalWrite(LED_PIN, ledOn);
}