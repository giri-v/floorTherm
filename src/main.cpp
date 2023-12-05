#define WIFI_SSID "vtap"
#define WIFI_PASSWORD "things1250"

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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
#define SCREEN_ADDRESS 0x78 /// 0x3C for SSD1315 OLED

float Rref = 10000.0;
float Beta = 3950.0;
float To = 298.15;
float Ro = 10000.0;
float adcMax = 4096;
float Vs = 3.3;

const char delim[2] = "/";
const char *mainPubTopic = "floortherm/"; // Topic to publish
const char *aliveTopic = "floortherm/alive";
const char *SubTopic = "floortherm/#";

const char *setTempTopic = "floortherm/#/set";
const char *enableHeatTopic = "floortherm/#/enable";

const char *zoneNames[] = {"MBR", "UpHall", "Office", "SV", "MV"};
const char *nameSpacing[] = {"   ", "", "", "    ", "    "};
int inPins[] = {32, 33, 34, 35, 36};
int outPins[] = {16, 17, 18, 19, 23};
float zoneSetTemp[] = {72.0, 72.0, 72.0, 72.0, 72.0};
float zoneActualTemp[] = {72, 72, 72, 72, 72};
int zoneReadVal[] = {2048, 2048, 2048, 2048, 2048};
bool zoneHeatEnable[] = {false, false, false, false, false};
bool zoneHeating[] = {false, false, false, false, false};

const char *enableCommand = "enable";
const char *setCommand = "set";

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

  mqttClient.setWill(aliveTopic, 0, true, "0");
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

void onMqttMessage(char *topic, char *payload, const AsyncMqttClientMessageProperties &properties,
                   const size_t &len, const size_t &index, const size_t &total)
{
  (void)payload;

  String recTopic = String(topic);

  Serial.print("Received");
  Serial.print("  topic: ");
  Serial.println(topic);

  char *message;
  char *targetRoom;
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
      targetRoom = token;
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

  Serial.print("Target Room: ");
  if (targetRoom != NULL)
    Serial.println(targetRoom);
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
    while (i < 5)
    {
      if (strcmp(targetRoom, zoneNames[i]) == 0)
      {
        int sentval = atoi(payload);
        Serial.println(sentval);
        if (strcmp(command, enableCommand) == 0)
        {
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
          zoneSetTemp[i] = (float)sentval;
        }
      }
      i++;
    }
  }
  else
  {
    Serial.println("!!!EMPTY!!!");
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
  Serial.println("\nReading Temps...");
  for (int i = 0; i < 5; i++)
  {
    int avg = 0;
    for (int j = 0; j < 50; j++)
    {
      avg += analogRead(inPins[i]);
      delay(10);
    }
    zoneReadVal[i] = avg / 50.0;
    zoneActualTemp[i] = ConvertValToTemp(zoneReadVal[i]);

    /*
    Serial.print(zoneNames[i]);
    Serial.print(": ");
    //Serial.println(zoneActualTemp[i]);
    Serial.println(zoneReadVal[i]);
    */
  }
  Serial.println();
}

void SetHeatControl()
{

  for (int i = 0; i < 5; i++)
  {
    if ((zoneActualTemp[i] < zoneSetTemp[i]) && zoneHeatEnable[i])
    {
      zoneHeating[i] = true;
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

    if (!zoneHeatEnable[j] && zoneHeating[j])
    {
      Serial.println("!!! ERROR !!!");
    }
    else
    {
      Serial.println();
    }
  }
  Serial.println();
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
    delay(200);                                  /// time for board to initialize?
    display.clearDisplay();                      ///
    display.setTextColor(WHITE);                 ///
    display.setTextSize(1);                      ///
    display.setCursor(10, 30);                   ///
    display.print("FloorTherm Initializing..."); ///
    display.display();                           /// needed to actually display the message
    delay(1000);                                 /// time for message to stay up
    display.clearDisplay();
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
  GetTemps();
  SetHeatControl();
  logHeatingStatus();
  delay(1000);

  delay(500);
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
}