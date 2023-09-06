#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include "config.h"
#include "index.html.h"

constexpr uint8_t maxWifiReconnectAttempts = 20;
constexpr unsigned long minWifiCheckTimeout = 500;

EEPROMConfig config(0);

// Dallas configuration
OneWire oneWire(2);
DallasTemperature dallasSensors(&oneWire);

// Wifi AP configuration
constexpr const char *const wifiAPSsid = "TempSensor01";
constexpr const char *const wifiAPPass = "sensorTemp";

// Web server configuration
ESP8266WebServer server(80);
// AP address
IPAddress apIp(192, 168, 4, 10);
IPAddress apGateway(192, 168, 4, 1);
IPAddress apSubnet(255, 255, 255, 0);

// Runtime variables
enum class WifiConnectionStatus {
  unknown = 0,
  connecting = 1,
  connected = 2,
};
WifiConnectionStatus wifiConnectionStatus = WifiConnectionStatus::unknown;
uint8_t wifiReconnectAttempts = 0;
unsigned long lastWifiTime;
double actualTemp;
unsigned long boardTime;
int lastStatusCode = 0;
unsigned long lastStatusCodeTime;
unsigned long lastStatusCodeSuccessTime = -1;

bool wifiConnect()
{
  if (wifiConnectionStatus != WifiConnectionStatus::unknown && WiFi.isConnected())
  {
    if (wifiConnectionStatus != WifiConnectionStatus::connected)
    {
      #ifdef NEED_SERIAL_PRINT
        Serial.print("WiFi connected. IP address: ");
        Serial.println(WiFi.localIP());
      #endif
      wifiConnectionStatus = WifiConnectionStatus::connected;
    }
    return true;
  }

  if (wifiConnectionStatus == WifiConnectionStatus::connecting)
  {
    if (millis() - lastWifiTime > minWifiCheckTimeout)
    {
      #ifdef NEED_SERIAL_PRINT
        Serial.print("WiFi connection attempt #");
        Serial.println(wifiReconnectAttempts + 1);
      #endif

      if (wifiReconnectAttempts++ > maxWifiReconnectAttempts)
      {
        wifiConnectionStatus = WifiConnectionStatus::unknown;

        #ifdef NEED_SERIAL_PRINT
          Serial.println(" WiFi connection failed!");
        #endif
      }

      lastWifiTime = millis();
    }
    return false;
  }

  #ifdef NEED_SERIAL_PRINT
    Serial.print("Connecting to ");
    Serial.println(config.data.ssid);
  #endif
  
  wifiConnectionStatus = WifiConnectionStatus::connecting;
  wifiReconnectAttempts = 0;
  WiFi.disconnect();
  WiFi.begin(config.data.ssid, config.data.pass);
  return WiFi.isConnected();
}

void handleRoot()
{
  if (server.method() == HTTP_POST && server.arg("ssid") && server.arg("pass") && server.arg("ip")) 
  {
    #ifdef NEED_SERIAL_PRINT
      Serial.print("Setup params: ssid = ");
      Serial.print(server.arg("ssid"));
      Serial.print("; pass = ");
      Serial.print(server.arg("pass"));
      Serial.print("; ip = ");
      Serial.println(server.arg("ip"));
    #endif
    strcpy(config.data.ssid, server.arg("ssid").c_str());
    strcpy(config.data.pass, server.arg("pass").c_str());
    auto ipAddress = IPAddress();
    ipAddress.fromString(server.arg("ip"));
    config.data.ip[0] = ipAddress[0];
    config.data.ip[1] = ipAddress[1];
    config.data.ip[2] = ipAddress[2];
    config.data.ip[3] = ipAddress[3];
    config.write();
    wifiConnectionStatus = WifiConnectionStatus::unknown;
  }
  
  char html[1024];
  unsigned long secs = (millis() - lastStatusCodeTime) / 1000;
  unsigned long successSecs = (millis() - lastStatusCodeSuccessTime) / 1000;
  const char* status = lastStatusCode == 200 ? "успешно" : "ошибка";
  sprintf(html, INDEX_TEMPLATE, actualTemp, status, secs, successSecs, config.data.ssid, IPAddress(config.data.ip).toString().c_str());
  server.send(200, "text/html", html);
}

void sendToTarget()
{
  if (actualTemp < -100)
  {
    #ifdef NEED_SERIAL_PRINT
      Serial.println("Incorrect temperature value: no need to send");
    #endif
    return;
  }

  if (!WiFi.isConnected())
  {
    #ifdef NEED_SERIAL_PRINT
      Serial.println("Can't send the temperature due to connection problem");
    #endif
    return;
  }

  WiFiClient client;
  HTTPClient http;
  String postData = "temp=" + String(actualTemp, 3);
  http.begin(client, IPAddress(config.data.ip).toString(), 80);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  lastStatusCode = http.POST(postData);
  lastStatusCodeTime = millis();
  if (lastStatusCode == 200)
  {
    lastStatusCodeSuccessTime = lastStatusCodeTime;
  }
  #ifdef NEED_SERIAL_PRINT
    Serial.print("Request was sent, return code = ");
    Serial.println(lastStatusCode);
  #endif
  http.end();
}

void setup()
{
  #ifdef NEED_SERIAL_PRINT
    Serial.begin(115200);
  #endif

  #ifdef NEED_SERIAL_PRINT
    Serial.print("Init EEPROM with size = ");
    Serial.println(sizeof(ConfigData));
  #endif
  EEPROM.begin(sizeof(ConfigData));
  
  config.read();

  dallasSensors.begin();

  dallasSensors.requestTemperatures();
  delay(750); // to dallas collect data
  actualTemp = dallasSensors.getTempCByIndex(0);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIp, apGateway, apSubnet);
  WiFi.softAP(wifiAPSsid, wifiAPPass);
  lastWifiTime = millis();
  wifiConnect();

  server.on("/", handleRoot);
  server.begin();
  #ifdef NEED_SERIAL_PRINT
    Serial.println("HTTP server started");
  #endif

  boardTime = millis();
}

void loop()
{
  auto passedTime = millis() - boardTime;
  
  if (passedTime > 1000)
  {
    actualTemp = dallasSensors.getTempCByIndex(0);

    #ifdef NEED_SERIAL_PRINT
      Serial.print("Temp: ");
      Serial.println(actualTemp);
    #endif

    sendToTarget();
    boardTime = millis();
  }
  else if (passedTime > 100)
  {
    dallasSensors.requestTemperatures();
  }

  wifiConnect();
  server.handleClient();
}
