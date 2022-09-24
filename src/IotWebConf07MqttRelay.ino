/**
 * IotWebConf07MqttRelay.ino -- IotWebConf is an ESP8266/ESP32
 *   non blocking WiFi/AP web configuration library for Arduino.
 *   https://github.com/prampec/IotWebConf
 *
 * Copyright (C) 2020 Balazs Kelemen <prampec+arduino@gmail.com>
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

/**
 * Example: MQTT Relay Demo
 * Description:
 *   All IotWebConf specific aspects of this example are described in
 *   previous examples, so please get familiar with IotWebConf before
 *   starting this example. So nothing new will be explained here,
 *   but a complete demo application will be built.
 *   It is also expected from the reader to have a basic knowledge over
 *   MQTT to understand this code.
 *
 *   This example starts an MQTT client with the configured
 *   connection settings.
 *   Will receives messages appears in channel "/devices/[thingName]/action"
 *   with payload ON/OFF, and reports current state in channel
 *   "/devices/[thingName]/status" (ON/OFF). Where the thingName can be
 *   configured in the portal. A relay will be switched on/off
 *   corresponding to the received action. The relay can be also controlled
 *   by the push button.
 *   The thing will delay actions arriving within 7 seconds.
 *
 *   This example also provides the firmware update option.
 *   (See previous examples for more details!)
 *
 * Software setup for this example:
 *   This example utilizes Joel Gaehwiler's MQTT library.
 *   https://github.com/256dpi/arduino-mqtt
 *
 * Hardware setup for this example:
 *   - A Relay is attached to the D5 pin (On=HIGH). Note on relay pin!
 *   - An LED is attached to LED_BUILTIN pin with setup On=LOW.
 *   - A push button is attached to pin D2, the other leg of the
 *     button should be attached to GND.
 *
 * Note on relay pin
 *   Some people might want to use Wemos Relay Shield to test this example.
 *   Now Wemos Relay Shield connects the relay to pin D1.
 *   However, when using D1 as output, Serial communication will be blocked.
 *   So you will either keep on using D1 and miss the Serial monitor
 *   feedback, or connect your relay to another digital pin (e.g. D5).
 *   (You can modify your Wemos Relay Shield for that, as I show it in this
 *   video: https://youtu.be/GykA_7QmoXE)
 */

#include <MQTT.h>
#include <IotWebConf.h>
#include <IotWebConfUsing.h> // This loads aliases for easier class names.
#ifdef ESP8266
# include <ESP8266HTTPUpdateServer.h>
#elif defined(ESP32)
# include <IotWebConfESP32HTTPUpdateServer.h>
#endif

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "testThing";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "smrtTHNG8266";

#define STRING_LEN 128
#define NUMBER_LEN 16
#define CHECKBOX_LEN 9

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "mqt5"

// -- When BUTTON_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
#if BUTTON_PIN == -1
# undef BUTTON_PIN
#else
# ifndef BUTTON_PIN
#  define BUTTON_PIN D2
# endif
#endif

// -- Status indicator pin.
//      First it will light up (kept LOW), on Wifi connection it will blink,
//      when connected to the Wifi it will turn off (kept HIGH).
#if STATUS_PIN == -1
# undef STATUS_PIN
#else
# ifndef STATUS_PIN
#  define STATUS_PIN LED_BUILTIN
# endif
#endif

// -- Connected output pin. See "Note on relay pin"!
#ifndef RELAY_PIN
# define RELAY_PIN D5
#endif

#define MQTT_TOPIC_PREFIX "devices/"
#define MQTT_CONNECT_FREQ_LIMIT 2000

// -- Ignore/limit status changes more frequent than the value below (milliseconds).
#define ACTION_FREQ_LIMIT 7000
#define NO_ACTION -1
#define FLASH_ACTION -2

// -- Method declarations.
void handleRoot();
void mqttMessageReceived(String &topic, String &payload);
bool connectMqtt();
void setState(int newState);
void report();

// -- Callback methods.
void wifiConnected();
void configSaved();
bool formValidator(iotwebconf::WebRequestWrapper* webRequestWrapper);

DNSServer dnsServer;
WebServer server(80);
#ifdef ESP8266
ESP8266HTTPUpdateServer httpUpdater;
#elif defined(ESP32)
HTTPUpdateServer httpUpdater;
#endif
WiFiClient *net;
MQTTClient mqttClient;

char rqFlashDurationValue[NUMBER_LEN];

char mqttServerValue[STRING_LEN];
char mqttPortValue[NUMBER_LEN];
char mqttTlsValue[CHECKBOX_LEN];
char mqttUserValue[STRING_LEN];
char mqttPasswordValue[STRING_LEN];
char mqttRetainValue[CHECKBOX_LEN];
char mqttQoSValue[NUMBER_LEN];

static char qosValues[][NUMBER_LEN] = {"0", "1", "2"};
static char qosNames[][STRING_LEN] = {"0 - At most once", "1 - At least once", "2 - Exactly once"};

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);

IotWebConfParameterGroup rqParamGroup = IotWebConfParameterGroup("iwcRQ", "Relay control");
IotWebConfNumberParameter rqFlashDurationParam = IotWebConfNumberParameter("Flash duration", "rqFlashDuration", rqFlashDurationValue, NUMBER_LEN, "1000");

IotWebConfParameterGroup mqttParamGroup = IotWebConfParameterGroup("iwcMqtt", "MQTT");

IotWebConfTextParameter mqttServerParam = IotWebConfTextParameter("Server", "mqttServer", mqttServerValue, STRING_LEN);
IotWebConfNumberParameter mqttPortParam = IotWebConfNumberParameter("Port", "mqttPort", mqttPortValue, NUMBER_LEN, "1883");
IotWebConfCheckboxParameter mqttTlsParam = IotWebConfCheckboxParameter("Use TLS", "mqttTls", mqttTlsValue, CHECKBOX_LEN, false);
IotWebConfTextParameter mqttUserParam = IotWebConfTextParameter("User", "mqttUser", mqttUserValue, STRING_LEN);
IotWebConfPasswordParameter mqttPasswordParam = IotWebConfPasswordParameter("Password", "mqttPassword", mqttPasswordValue, STRING_LEN);
IotWebConfCheckboxParameter mqttRetainParam = IotWebConfCheckboxParameter("Retain", "mqttRetain", mqttRetainValue, CHECKBOX_LEN, true);
IotWebConfSelectParameter mqttQoSParam = IotWebConfSelectParameter("QoS", "mqttQoS", mqttQoSValue, NUMBER_LEN, (char*)qosValues, (char*)qosNames, sizeof(qosValues)/NUMBER_LEN, STRING_LEN, "1");

bool needMqttConnect = false;
bool needReset = false;
unsigned long lastMqttConnectionAttempt = 0;
int needAction = NO_ACTION;
int state = LOW;
int flashDuration;
unsigned long lastAction = 0;
char mqttActionTopic[STRING_LEN];
char mqttStatusTopic[STRING_LEN];
bool mqttTls;
int mqttPort;
bool mqttRetain;
int mqttQoS;

void setup()
{
  Serial.begin(115200); // See "Note on relay pin"!
  Serial.println();
  Serial.println("Starting up...");

  pinMode(RELAY_PIN, OUTPUT);

#ifdef STATUS_PIN
  iotWebConf.setStatusPin(STATUS_PIN);
#endif
#ifdef BUTTON_PIN
  iotWebConf.setConfigPin(BUTTON_PIN);
#endif
  iotWebConf.addParameterGroup(&rqParamGroup);
  rqParamGroup.addItem(&rqFlashDurationParam);

  iotWebConf.addParameterGroup(&mqttParamGroup);
  mqttParamGroup.addItem(&mqttServerParam);
  mqttParamGroup.addItem(&mqttPortParam);
  mqttParamGroup.addItem(&mqttTlsParam);
  mqttParamGroup.addItem(&mqttUserParam);
  mqttParamGroup.addItem(&mqttPasswordParam);
  mqttParamGroup.addItem(&mqttRetainParam);
  mqttParamGroup.addItem(&mqttQoSParam);

#ifdef SKIP_AP_STARTUP
  iotWebConf.skipApStartup();
#endif
  iotWebConf.setConfigSavedCallback(&configSaved);
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.setWifiConnectionCallback(&wifiConnected);
  iotWebConf.setupUpdateServer(
    [](const char* updatePath) { httpUpdater.setup(&server, updatePath); },
    [](const char* userName, char* password) { httpUpdater.updateCredentials(userName, password); });

  // -- Initializing the configuration.
  bool validConfig = iotWebConf.init();
  if (!validConfig)
  {
    rqFlashDurationValue[0] = '\0';

    mqttServerValue[0] = '\0';
    mqttPortValue[0] = '\0';
    mqttTlsValue[0] = '\0';
    mqttUserValue[0] = '\0';
    mqttPasswordValue[0] = '\0';
    mqttRetainValue[0] = '\0';
    mqttQoSValue[0] = '\0';
  }

  // Relay control setup

  flashDuration = atoi(rqFlashDurationValue);

  // MQTT setup

  String temp = String(MQTT_TOPIC_PREFIX);
  temp += iotWebConf.getThingName();
  temp += "/action";
  temp.toCharArray(mqttActionTopic, STRING_LEN);
  temp = String(MQTT_TOPIC_PREFIX);
  temp += iotWebConf.getThingName();
  temp += "/status";
  temp.toCharArray(mqttStatusTopic, STRING_LEN);

  mqttPort = atoi(mqttPortValue);
  mqttTls = !strcmp(mqttTlsValue, "selected");
  mqttRetain = !strcmp(mqttRetainValue, "selected");
  mqttQoS = atoi(mqttQoSValue);

  if (mqttTls) {
    net = new WiFiClientSecure;

    // No server cert check
    ((WiFiClientSecure *)net)->setInsecure();

  } else {
    net = new WiFiClient;
  }
  mqttClient.begin(mqttServerValue, mqttPort, *net);
  mqttClient.onMessage(mqttMessageReceived);

  // Web server setup

  server.on("/", handleRoot);
  server.on("/config", []{ iotWebConf.handleConfig(); });
  server.onNotFound([](){ iotWebConf.handleNotFound(); });

  Serial.println("Ready.");
}

void report() {
  mqttClient.publish(mqttStatusTopic, state == HIGH ? "ON" : "OFF", mqttRetain, mqttQoS);
#ifdef PUBLISH_ON_ACTION_TOPIC
  mqttClient.publish(mqttActionTopic, state == HIGH ? "ON" : "OFF", retain, qos);
#endif
}

void loop()
{
  unsigned long now;

  // -- doLoop should be called as frequently as possible.
  iotWebConf.doLoop();
  mqttClient.loop();

  now = millis();
  if ((needMqttConnect
       || (iotWebConf.getState() == iotwebconf::OnLine
           && !mqttClient.connected()))
      && now - lastMqttConnectionAttempt > MQTT_CONNECT_FREQ_LIMIT)
  {
    lastMqttConnectionAttempt = now;
    Serial.println("MQTT reconnect");
    if (connectMqtt())
      needMqttConnect = false;
  }

  if (needReset)
  {
    Serial.println("Rebooting after 1 second.");
    iotWebConf.delay(1000);
    ESP.restart();
  }

  now = millis();

#ifdef BUTTON_PIN
  // -- Check for button push
  if ((digitalRead(BUTTON_PIN) == LOW)
    && ( ACTION_FREQ_LIMIT < now - lastAction))
  {
    needAction = 1 - state; // -- Invert the state
  }
#endif

  if ((needAction != NO_ACTION)
    && ( ACTION_FREQ_LIMIT < now - lastAction))
  {
    if (needAction >= 0)
      setState(needAction);
    else if (needAction == FLASH_ACTION) {
      setState(HIGH);
      delay(flashDuration);
      setState(LOW);
    }
    lastAction = now;
  }
}

void setState(int newState) {
    state = newState;
    digitalWrite(RELAY_PIN, state);
    if (state == HIGH)
    {
      iotWebConf.blink(5000, 95);
    }
    else
    {
      iotWebConf.stopCustomBlink();
    }
    Serial.print("Switched ");
    Serial.println(state == HIGH ? "ON" : "OFF");
    needAction = NO_ACTION;
    report();
}

/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  String s = F("<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>");
  s += iotWebConf.getHtmlFormatProvider()->getStyle();
  s += "<title>IotWebConf 07 MQTT Relay</title></head><body>";
  s += iotWebConf.getThingName();
  s += "<div>State: ";
  s += (state == HIGH ? "ON" : "OFF");
  s += "</div>";
  s += "<button type='button' onclick=\"location.href='';\" >Refresh</button>";
  s += "<div>Go to <a href='config'>configure page</a> to change values.</div>";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}

void wifiConnected()
{
  needMqttConnect = true;
}

void configSaved()
{
  Serial.println("Configuration was updated.");
  needReset = true;
}

bool validate_int_range(iotwebconf::WebRequestWrapper* webRequestWrapper, iotwebconf::Parameter &param, int min, int max) {
  String arg = webRequestWrapper->arg(param.getId());
  const char * const arg_str = arg.c_str();
  char *end;
  const int val = strtol(arg_str, &end, 10);
  return val >= min && val <= max;
}

bool formValidator(iotwebconf::WebRequestWrapper* webRequestWrapper)
{
  Serial.println("Validating form.");
  bool valid = true;

  int l = webRequestWrapper->arg(mqttServerParam.getId()).length();
  if (l < 3)
  {
    mqttServerParam.errorMessage = "Please provide at least 3 characters!";
    valid = false;
  }

  if (!validate_int_range(webRequestWrapper, mqttQoSParam, 0, 2))
  {
    mqttQoSParam.errorMessage = "Please provide a valid QoS";
    valid = false;
  }

  if (!validate_int_range(webRequestWrapper, rqFlashDurationParam, 1, INT_MAX))
  {
    rqFlashDurationParam.errorMessage = "Flash duration must be a positive integer (ms)";
    valid = false;
  }
  return valid;
}

bool connectMqtt() {
  char buf[STRING_LEN];

  if (!mqttClient.connect(iotWebConf.getThingName(),
                          mqttUserValue[0] ? mqttUserValue : nullptr,
                          mqttPasswordValue[0] ? mqttPasswordValue : nullptr))
  {
    Serial.println("Connection failed");
    Serial.print("Client status: ");
    Serial.println(net->status());
    if (mqttTls) {
      ((WiFiClientSecure *)net)->getLastSSLError(buf, sizeof buf);
      Serial.print("SSL error: ");
      Serial.println(buf);
    }
    return false;
  }
  Serial.println("Connected!");
  mqttClient.subscribe(mqttActionTopic);
  snprintf(
    buf, sizeof buf,
    "HELLO ip=%s flash=%d QoS=%d retain=%s",
    WiFi.localIP().toString().c_str(),
    flashDuration,
    mqttQoS,
    mqttRetain ? "true": "false"
  );
  mqttClient.publish(mqttStatusTopic, buf);
  report();
  return true;
}

void mqttMessageReceived(String &topic, String &payload)
{
  Serial.println("Incoming: " + topic + " - " + payload);

  if (topic.endsWith("action"))
  {
    if (payload.equals("ON"))
      needAction = HIGH;
    else if (payload.equals("OFF"))
      needAction = LOW;
    else if (payload.equals("TOGGLE"))
      needAction = 1 - state;
    else if (payload.equals("FLASH"))
      needAction = FLASH_ACTION;
    else if (payload.equals("REPORT"))
      report();

    if (needAction == state)
    {
      needAction = NO_ACTION;
    }
  }
}

