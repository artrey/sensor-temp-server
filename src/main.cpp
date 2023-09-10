#include <ArduinoOTA.h>
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
constexpr unsigned long temperatureDelay = 1000;

EEPROMConfig config(0);

// Dallas configuration
OneWire oneWire(2);
DallasTemperature dallasSensors(&oneWire);

// Wifi AP configuration
constexpr const char* const wifiAPSsid = "TempSensor01";
constexpr const char* const wifiAPPass = "sensorTemp";

// OTA configuration
constexpr uint16_t otaPort = 8232;
constexpr const char* const otaHostname = "esp-temp";
constexpr const char* const otaPassword = "temp-esp";

// Web server configuration
ESP8266WebServer server(80);
// AP address
IPAddress apIp(192, 168, 4, 10);
IPAddress apGateway(192, 168, 4, 1);
IPAddress apSubnet(255, 255, 255, 0);

// Runtime variables
enum class WifiConnectionStatus
{
  unknown = 0,
  connecting = 1,
  connected = 2,
};
WifiConnectionStatus wifiConnectionStatus = WifiConnectionStatus::unknown;
uint8_t wifiReconnectAttempts = 0;
unsigned long lastWifiTime;
double actualTemp;
unsigned long tempLastTime;
unsigned long sendLastTime;
int lastStatusCode = 0;
unsigned long lastStatusCodeTime;
unsigned long lastStatusCodeSuccessTime = -1;
char* htmlAnswer;

bool wifiConnect();
void configureOTA();
void handleRoot();
void sendToTarget();

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

  htmlAnswer = new char[4096];

  server.on("/", handleRoot);
  server.begin();
#ifdef NEED_SERIAL_PRINT
  Serial.println("HTTP server started");
#endif

  configureOTA();

  tempLastTime = millis();
}

void loop()
{
  if (millis() - tempLastTime > temperatureDelay)
  {
    actualTemp = dallasSensors.getTempCByIndex(0);
#ifdef NEED_SERIAL_PRINT
    Serial.print("Temp: ");
    Serial.println(actualTemp);
#endif

    dallasSensors.requestTemperatures();
    tempLastTime = millis();
  }

  if (config.data.sendEnabled && millis() - sendLastTime > (unsigned long)config.data.sendDelay * 1000)
  {
    sendToTarget();
    sendLastTime = millis();
  }

  wifiConnect();
  server.handleClient();
  ArduinoOTA.handle();
}

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

void configureOTA()
{
  ArduinoOTA.setPort(otaPort);
  ArduinoOTA.setHostname(otaHostname);
  ArduinoOTA.setPassword(otaPassword);

  ArduinoOTA.onStart([]() {
#ifdef NEED_SERIAL_PRINT
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
    {
      type = "sketch";
    }
    else
    {
      type = "filesystem";
    }
    Serial.println("OTA | Start updating " + type);
#endif
  });

  ArduinoOTA.onEnd([]() {
#ifdef NEED_SERIAL_PRINT
    Serial.println("\nOTA | End");
#endif
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
#ifdef NEED_SERIAL_PRINT
    Serial.printf("OTA | Progress: %u%%\r", (progress / (total / 100)));
#endif
  });

  ArduinoOTA.onError([](ota_error_t error) {
#ifdef NEED_SERIAL_PRINT
    Serial.printf("OTA | Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
    {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR)
    {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR)
    {
      Serial.println("End Failed");
    }
#endif
  });

  ArduinoOTA.begin();
}

void handleRoot()
{
  bool update = false;
  if (server.method() == HTTP_POST && !server.arg("ssid").isEmpty() && !server.arg("pass").isEmpty())
  {
#ifdef NEED_SERIAL_PRINT
    Serial.print("Setup WiFi params: ssid = ");
    Serial.print(server.arg("ssid"));
    Serial.print("; pass = ");
    Serial.println(server.arg("pass"));
#endif
    strcpy(config.data.ssid, server.arg("ssid").c_str());
    strcpy(config.data.pass, server.arg("pass").c_str());
    wifiConnectionStatus = WifiConnectionStatus::unknown;
    update = true;
  }

  if (server.method() == HTTP_POST && !server.arg("host").isEmpty() && !server.arg("port").isEmpty() && !server.arg("path").isEmpty())
  {
#ifdef NEED_SERIAL_PRINT
    Serial.print("Setup receiver params: host = ");
    Serial.print(server.arg("host"));
    Serial.print("; port = ");
    Serial.print(server.arg("port"));
    Serial.print("; path = ");
    Serial.println(server.arg("path"));
#endif
    auto ipAddress = IPAddress();
    ipAddress.fromString(server.arg("host"));
    config.data.ip[0] = ipAddress[0];
    config.data.ip[1] = ipAddress[1];
    config.data.ip[2] = ipAddress[2];
    config.data.ip[3] = ipAddress[3];
    config.data.port = (uint16_t)server.arg("port").toInt();
    strcpy(config.data.path, server.arg("path").c_str());
    update = true;
  }

  if (update)
  {
    config.write();

    server.sendHeader("Location", "/", true);
    server.send(302);
    return;
  }

  unsigned long secs = (millis() - lastStatusCodeTime) / 1000;
  unsigned long successSecs = (millis() - lastStatusCodeSuccessTime) / 1000;
  const char* status = lastStatusCode == 200 ? "успешно" : "ошибка";
  sprintf(
    htmlAnswer,
    INDEX_TEMPLATE,
    actualTemp,
    status,
    secs,
    successSecs,
    config.data.ssid,
    config.data.sendEnabled ? "checked" : "",
    config.data.sendDelay,
    IPAddress(config.data.ip).toString().c_str(),
    config.data.port,
    config.data.path
  );
  server.send(200, "text/html", htmlAnswer);
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
  http.begin(client, IPAddress(config.data.ip).toString(), config.data.port, config.data.path);
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
