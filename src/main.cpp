#define WIFI_SSID "vtap"
#define WIFI_PASSWORD "things1250"

#include <Arduino.h>
#include <WiFi.h>
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

#define MQTT_HOST IPAddress(192, 168, 0, 13)
// #define MQTT_HOST "broker.emqx.io" // Broker address
#define MQTT_PORT 1883

#define LED_PIN 2

#define SCREEN_WIDTH 128 /// OLED display width, in pixels
#define SCREEN_HEIGHT 64 /// OLED display height, in pixels
#define OLED_RESET -1    ///-1 => no reset on 4 pin SSD1315 OLED

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
/// define screen params

#define I2C_SDA 21          /// ESP8266 NodeMCU SDA pin GPIO4 = D2
#define I2C_SCL 22          /// ESP8266 NodeMCU SCL pin GPIO5 = D1
#define SCREEN_ADDRESS 0x3C /// 0x3C for SSD1315 OLED

float Rref = 10000.0;
float Beta = 3894; // 3950.0;
float To = 298.15;
float Ro = 10000.0;
float adcMax = 4096;
float Vs = 3.3;

bool ledOn = true;

const char delim[2] = "/";

// Published Topics
const char *mainPubTopic = "floortherm/"; // Topic to publish
const char *aliveTopic = "floortherm/online";

const char *willTopic = "floortherm/offline";
const char *statusTopic = "floortherm/status";

// Subscribed Topics
const char *SubTopic = "floortherm/#";
const char *setTempTopic = "floortherm/#/set";
const char *enableHeatTopic = "floortherm/#/enable";

// Zone Data
const char *zoneFriendlyNames[] = {"Master Bedroom", "Narayan's Room", "Office", "Shanti's Room", "Maya's Room"};
const char *zoneNames[] = {"MBR", "NAV", "OFC", "SMV", "MAV"};
const char *nameSpacing[] = {"   ", "", "", "    ", "    "};
int inPins[] = {32, 33, 34, 35, 36};
int outPins[] = {16, 17, 18, 19, 23};
int zoneSetTemp[] = {72, 72, 72, 72, 72};
float zoneActualTemp[] = {72.0, 72.0, 72.0, 72.0, 72.0};
int zoneReadVal[] = {2048, 2048, 2048, 2048, 2048};
bool zoneHeatEnable[] = {false, false, false, false, false};
bool zoneHeating[] = {false, false, false, false, false};
int zoneHeatArrowCounter[] = {0, 0, 0, 0, 0};

unsigned long lastStatusBroadcast = 0;

// Topic Commands
const char *enableCommand = "enable";
const char *enCommand = "en";
const char *setCommand = "set";

const char *getCommand = "get";
const char *statusReport = "status";

const int docCapacity = JSON_OBJECT_SIZE(5) + 5 * JSON_OBJECT_SIZE(4);
const int roomDocCapacity = JSON_OBJECT_SIZE(4);
int logDisplayCounter = 0;
AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

void connectToWifi()
{
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt()
{
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event)
{
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
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    connectToMqtt();
    break;

  case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    Serial.println("WiFi lost IP");
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    Serial.println("WiFi lost connection");
    xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
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
}

void printSeparationLine()
{
  Serial.println("************************************************");
}

void onMqttConnect(bool sessionPresent)
{
  Serial.print("Connected to MQTT broker: ");
  Serial.print(MQTT_HOST);
  Serial.print(", port: ");
  Serial.println(MQTT_PORT);
  Serial.print("PubTopic: ");
  Serial.println(aliveTopic);
  Serial.print("SubTopic: ");
  Serial.println(SubTopic);

  printSeparationLine();
  Serial.print("Session present: ");
  Serial.println(sessionPresent);

  uint16_t packetIdSub = mqttClient.subscribe(SubTopic, 2);
  Serial.print("Subscribing at QoS 2, packetId: ");
  Serial.println(packetIdSub);

  mqttClient.publish(aliveTopic, 0, true, "1");
  Serial.println("Publishing at QoS 0");

  mqttClient.setWill(willTopic, 0, true, "1");
  Serial.println("Set Last Will and Testament message.");

  printSeparationLine();
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  (void)reason;

  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected())
  {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttSubscribe(const uint16_t &packetId, const uint8_t &qos)
{
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(const uint16_t &packetId)
{
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

String getRoomStatusJson(int i)
{

  StaticJsonDocument<roomDocCapacity> doc;
  String payload;

  doc["CurrentTemp"] = zoneActualTemp[i];
  doc["Enabled"] = zoneHeatEnable[i];
  doc["SetTemp"] = zoneSetTemp[i];
  doc["Heating"] = zoneHeating[i];

  serializeJson(doc, payload);
  return payload;
}

String getStatusJson()
{

  StaticJsonDocument<docCapacity> doc;
  String payload;

  for (int i = 0; i < 5; i++)
  {
    doc[zoneNames[i]]["CurrentTemp"] = zoneActualTemp[i];
    doc[zoneNames[i]]["Enabled"] = zoneHeatEnable[i];
    doc[zoneNames[i]]["SetTemp"] = zoneSetTemp[i];
    doc[zoneNames[i]]["Heating"] = zoneHeating[i];
  }

  serializeJson(doc, payload);
  return payload;
}

void publishHeatingStatus()
{
  // Publish Status
  String oDoc = getStatusJson();
  const char *doc = oDoc.c_str();
  Serial.print("Topic: ");
  Serial.println(statusTopic);
  Serial.print("Payload: ");
  Serial.println(doc);
  mqttClient.publish(statusTopic, 0, false, doc);
  Serial.println("Publishing Status at QoS 0");
}

void onMqttMessage(char *topic, char *payload, const AsyncMqttClientMessageProperties &properties,
                   const size_t &len, const size_t &index, const size_t &total)
{
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
  if (len > 0)
  {
    if (strcmp(target, statusReport) == 0)
    {
      Serial.println(msg);
    }
    else
    {
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
          }
        }
        i++;
      }
    }
  }
  else
  {
    Serial.println("!!!EMPTY!!!");
    i = 0;

    if (strcmp(target, getCommand) == 0)
    {
      Serial.println("Processing GET command!");
      if (command != NULL)
      {
        while (i < 5)
        {
          if (strcmp(command, zoneNames[i]) == 0)
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
            break;
          }
          i++;
        }
      }
      else
      {
        // Publish Heating Status
        publishHeatingStatus();
      }
    }
  }

  Serial.println();
  /*
  Serial.print("  qos: ");
  Serial.println(properties.qos);
  Serial.print("  dup: ");
  Serial.println(properties.dup);
  Serial.print("  retain: ");
  Serial.println(properties.retain);
  Serial.print("  len: ");
  Serial.println(len);
  Serial.print("  index: ");
  Serial.println(index);
  Serial.print("  total: ");
  Serial.println(total);
  */
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
  Serial.println("Reading Temps...");
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
}

void SetHeatControl()
{

  for (int i = 0; i < 5; i++)
  {
    if ((zoneActualTemp[i] <= zoneSetTemp[i]) && zoneHeatEnable[i] && (zoneActualTemp[i] < 90))
    {
      if (zoneActualTemp[i] < zoneSetTemp[i])
      {
        zoneHeating[i] = true;
      }
    }
    else
    {
      zoneHeating[i] = false;
    }

    digitalWrite(outPins[i], zoneHeating[i]);
  }
}

void logHeatingStatus()
{
  Serial.println("Status:");
  for (int j = 0; j < 5; j++)
  {
    Serial.print(zoneNames[j]);
    Serial.print(nameSpacing[j]);
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

    if ((!zoneHeatEnable[j] && zoneHeating[j]) || ((zoneActualTemp[j] > 90) && zoneHeating[j]))
    {
      Serial.println("!!! ERROR !!!");
      // Should also send MQTT message to alert someone
      continue;
    }
    else
    {
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

          x += 10;
          display.setCursor(x, y);
          display.print(zoneSetTemp[j], 0);
          display.print("F");
        }
        else
        {
          display.print("NOT HEATING");
        }

        x += 10;
        display.setCursor(x, y);
      }
      else
      {
        // display.print("Disabled");
      }
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

  for (int i = 0; i < 5; i++)
    pinMode(outPins[i], OUTPUT);

  pinMode(LED_PIN, OUTPUT);

  Serial.begin(115200);

  while (!Serial && millis() < 5000)
    ;

  delay(500);

  Serial.print("\nStarting FullyFeature_ESP32 on ");
  Serial.println(ARDUINO_BOARD);
  Serial.println(ASYNC_MQTT_ESP32_VERSION);

  Serial.println("FloorTherm starting...");

  setupDisplay();

  GetTemps();

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
}

void loop()
{
  logDisplayCounter++;
  GetTemps();
  SetHeatControl();
  displayHeatingStatus();
  if (logDisplayCounter > 9)
  {
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