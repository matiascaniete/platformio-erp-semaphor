#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino

// needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager

#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include "SSD1306Wire.h" // legacy: #include "SSD1306.h"

#include <NeoPixelBus.h>
#include <Ticker.h>
#include "OneButton.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

const long utcOffsetInSeconds = 3600;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

uint8_t SW1_PIN = 14;
uint8_t SW2_PIN = 0;
uint8_t OLED_SDA = 5;
uint8_t OLED_CLK = 4;

OneButton button1(SW1_PIN, true);
OneButton button2(SW2_PIN, true);

Ticker ticker;
Ticker tickerConnectAPI;
const char *server = "https://derp.ndorma.com";
int thresholdMin = 40;
int thresholdMax = 45;
long apiCallEverySeconds = 60;
const int jsonSize = 1024;
HTTPClient http;

String mod = "min";

#define colorSaturation 255
const uint16_t PixelCount = 16; // this example assumes 4 pixels, making it smaller will cause a failure
// pixel pin ignored in ESP8266, uses pin RX
NeoPixelBus<NeoRgbFeature, Neo800KbpsMethod> strip(PixelCount);

RgbColor red(colorSaturation, 0, 0);
RgbColor yellow(colorSaturation, colorSaturation, 0);
RgbColor green(0, colorSaturation, 0);
RgbColor blue(0, 0, colorSaturation);
RgbColor white(colorSaturation);
RgbColor black(0);

SSD1306Wire display(0x3c, OLED_SDA, OLED_CLK, GEOMETRY_128_32); // ADDRESS, SDA, SCL  -  SDA and SCL usually populate automatically based on your board's pins_arduino.h e.g. https://github.com/esp8266/Arduino/blob/master/variants/nodemcu/pins_arduino.h

RgbColor valToColor(int val)
{
  if (val > thresholdMax)
  {
    return green;
  }

  if (val < thresholdMin)
  {
    return red;
  }

  return yellow;
}

void tick()
{
  // toggle state
  int state = digitalRead(LED_BUILTIN); // get the current state of GPIO2 pin
  digitalWrite(LED_BUILTIN, !state);    // set pin to the opposite state
}

// gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager)
{
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  // if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  // entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

void message(String message)
{
  display.clear();

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawStringMaxWidth(0, 0, 128, message);

  display.display();
  Serial.println("OLED display message: " + message);
}

long parseJson(String jsonDoc)
{
  Serial.println("Parsing JSON...");
  Serial.println(jsonDoc);
  DynamicJsonDocument doc(jsonSize);
  DeserializationError error = deserializeJson(doc, jsonDoc);

  // Test if parsing succeeds.
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return -1;
  }

  long value = doc["count"];
  return value;
}

void setColor(RgbColor color, int del = 0)
{
  strip.SetPixelColor(0, color);
  strip.Show();
  delay(del);
}

void flashColor(RgbColor color, int times = 3, int del = 50)
{
  for (size_t i = 0; i < times; i++)
  {
    setColor(color, del);
    setColor(black, del);
  }
}

void connectToServer()
{
  flashColor(blue);
  timeClient.update();
  String timeString = timeClient.getFormattedTime();

  Serial.println(timeString);
  message("CONNECTING TO HTTP SERVER... " + timeString);

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  char apiURL[1024];

  sprintf(apiURL, "%s/iot/stats/servicios", server);

  Serial.printf("API URL=%s\r\n", apiURL);
  if (http.begin(*client, apiURL))
  {
    int code = http.GET();
    message("HTTP CLIENT CONECTED!. HTTP CODE:" + code);

    if (code > 0)
    {
      if (code == HTTP_CODE_OK || code == HTTP_CODE_MOVED_PERMANENTLY)
      {
        String payload = http.getString();
        long value = parseJson(payload);
        message("Nº SERVICIOS: " + String(value) + " (" + timeString + ")");
        setColor(valToColor(value));
      }
      else
      {
        String payload = http.getString();
        message("NOT OK. RESPONSE: " + payload);
      }
    }
    else
    {
      Serial.printf("[HTTP] GET... failed, error: %s", http.errorToString(code).c_str());
    }
  }
}

void callWifiConfigurator()
{
  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  wifiManager.setTimeout(120);
  WiFi.mode(WIFI_STA);

  ticker.attach(0.2, tick);
  message("STARTED CONFIG PORTAL");

  if (!wifiManager.startConfigPortal("ERPSemaphor"))
  {
    message("FAILED TO CONNECT AND HIT TIMEOUT...");
    delay(3000);
    message("RESETTING...");
    // reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  // keep LED off
  ticker.detach();
  digitalWrite(LED_BUILTIN, HIGH);
}

void showMin()
{
  mod = "min";
  message("MIN:" + String(thresholdMin));
}

void showMax()
{
  mod = "max";
  message("MAX:" + String(thresholdMax));
}

void callUp()
{
  if (mod == "min")
  {
    thresholdMin++;
    showMin();
  }
  if (mod == "max")
  {
    thresholdMax++;
    showMax();
  }
}

void callDown()
{
  if (mod == "min")
  {
    thresholdMin--;
    showMin();
  }
  if (mod == "max")
  {
    thresholdMax--;
    showMax();
  }
}

void setup()
{
  Serial.begin(115200);

  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(SW1_PIN, INPUT_PULLUP);
  pinMode(SW2_PIN, INPUT_PULLUP);

  // this resets all the neopixels to an off state
  strip.Begin();
  strip.Show();

  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);
  message("CONNECTING TO WIFI...");

  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  // reset settings - for testing
  // wifiManager.resetSettings();

  // set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  // fetches ssid and pass and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "AutoConnectAP"
  // and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("ERPSemaphor"))
  {
    message("FAILED TO CONNECT AND HIT TIMEOUT");
    // reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  // if you get here you have connected to the WiFi
  message("CONNECTED TO WIFI!");

  // keep LED off
  ticker.detach();
  digitalWrite(LED_BUILTIN, HIGH);

  timeClient.begin();

  connectToServer();
  tickerConnectAPI.attach_scheduled(apiCallEverySeconds, connectToServer);
  button1.attachLongPressStart(connectToServer);
  button1.attachClick(callDown);
  button1.attachDoubleClick(showMin);

  button2.attachClick(callUp);
  button2.attachDoubleClick(showMax);
  button2.attachLongPressStart(callWifiConfigurator);
}

void loop()
{
  button1.tick();
  button2.tick();

  delay(10);
}