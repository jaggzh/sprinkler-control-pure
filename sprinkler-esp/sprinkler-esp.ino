#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include "httptime.h"
#include "settings.h"

#define AP_SSID "ESP_Sprinkler_Config"
AsyncWebServer server(80);
const char *timeServers[MAX_ZONES] = {"0.0.0.0", "0.0.0.0", "0.0.0.0", "0.0.0.0", "0.0.0.0"};
char adminPassword[32] = "";
struct timedata currentTimeData;

struct Zone {
  int pin;
  unsigned long endTime;
  bool isOn;
};

Zone zones[MAX_ZONES];

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed, formatting...");
    if (!LittleFS.format() || !LittleFS.begin()) {
      Serial.println("LittleFS format failed");
      return;
    }
  }
  Serial.println("LittleFS initialized");

  // Initialize zones
  for (int i = 0; i < MAX_ZONES; i++) {
    zones[i].pin = i + 2; // Example pin assignment
    pinMode(zones[i].pin, OUTPUT);
    digitalWrite(zones[i].pin, LOW);
    zones[i].endTime = 0;
    zones[i].isOn = false;
  }

  // Load configuration from LittleFS
  loadNetworkConfiguration();

  // WiFiManager for first-time WiFi Configuration
  WiFiManager wm;
  if (!wm.autoConnect(AP_SSID)) {
    Serial.println("Failed to connect to WiFi. Restarting...");
    delay(3000);
    ESP.restart();
  }
  Serial.println("Connected to WiFi");
  saveNetworkConfiguration();

  // Setup web server routes
  server.on("/", HTTP_GET, handleRootPage);
  server.on("/on", HTTP_GET, handleZoneOn);
  server.on("/off", HTTP_GET, handleZoneOff);
  server.on("/reboot", HTTP_GET, handleReboot);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/config_net", HTTP_GET, handleConfigNet);

  server.begin();
  Serial.println("Web server started");

  // Time update from HTTP
  update_time_from_server();
}

void saveToFile(const char* filename, const String& value) {
  File file = LittleFS.open(filename, "w");
  if (!file) {
    Serial.println("Failed to open file for writing: " + String(filename));
    return;
  }
  file.print(value);
  file.close();
}

String readFromFile(const char* filename) {
  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.println("Failed to open file for reading: " + String(filename));
    return "";
  }
  String value = file.readString();
  file.close();
  return value;
}

void saveNetworkConfiguration() {
  saveToFile("/net-ssid", WiFi.SSID());
  saveToFile("/net-ssidpw", WiFi.psk());
  saveToFile("/net-ip", WiFi.localIP().toString());
  saveToFile("/net-gw", WiFi.gatewayIP().toString());
  saveToFile("/net-mask", WiFi.subnetMask().toString());
  for (int i = 0; i < 5; i++) {
    saveToFile( ("/time-server-" + String(i)).c_str(), timeServers[i]);
  }
}

void loadNetworkConfiguration() {
  String ssid = readFromFile("/net-ssid");
  String password = readFromFile("/net-ssidpw");
  if (ssid.length() > 0 && password.length() > 0) {
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.println("Loaded WiFi credentials from storage.");
  }

  String ip = readFromFile("/net-ip");
  String gw = readFromFile("/net-gw");
  String mask = readFromFile("/net-mask");
  if (ip.length() > 0 && gw.length() > 0 && mask.length() > 0) {
    IPAddress localIP, gateway, subnet;
    localIP.fromString(ip);
    gateway.fromString(gw);
    subnet.fromString(mask);
    WiFi.config(localIP, gateway, subnet);
    Serial.println("Loaded network configuration from storage.");
  }

  for (int i = 0; i < 5; i++) {
    String serverIP = readFromFile( ("/time-server-" + String(i)).c_str());
    if (serverIP.length() > 0) {
      timeServers[i] = strdup(serverIP.c_str());
    }
  }
}

void handleRootPage(AsyncWebServerRequest *request) {
  request->send(200, "text/html", "");
  request->sendContent(F("<html>Sprinkler Main<br>"));
  request->sendContent(F("Uptime: ") + String(millis() / 1000) + F(" seconds<br>"));
  for (int i = 0; i < MAX_ZONES; i++) {
    request->sendContent(F("Zone ") + String(i) + F("<br>"));
    request->sendContent(F("<form action='/on' method='GET'>"));
    request->sendContent(F("<input type='hidden' name='zone' value='") + String(i) + F("'>"));
    request->sendContent(F("<input type='text' name='duration' value='30'> <input type='submit' value='On'>"));
    request->sendContent(F("</form>"));
    request->sendContent(F("<form action='/off' method='GET'>"));
    request->sendContent(F("<input type='hidden' name='zone' value='") + String(i) + F("'>"));
    request->sendContent(F("<input type='submit' value='Off'>"));
    request->sendContent(F("</form><br>"));
  }
  request->sendContent(F("<a href='/reboot'>Reboot link</a><br>"));
  request->sendContent(F("<a href='/ota'>Flash OTA</a></html>"));
}

void handleZoneOn(AsyncWebServerRequest *request) {
  if (request->hasParam("zone") && request->hasParam("duration")) {
    int zone = request->getParam("zone")->value().toInt();
    unsigned long duration = request->getParam("duration")->value().toInt() * 1000;
    if (zone >= 0 && zone < MAX_ZONES && duration > 0) {
      zones[zone].isOn = true;
      zones[zone].endTime = millis() + duration;
      digitalWrite(zones[zone].pin, HIGH);
      request->send(200, "text/html", "Updated. Redirecting...");
      request->sendContent(F("<meta http-equiv='refresh' content='3; url=/' />"));
    } else {
      request->send(400, "text/plain", "Invalid zone or duration");
    }
  } else {
    request->send(400, "text/plain", "Missing parameters");
  }
}

void handleZoneOff(AsyncWebServerRequest *request) {
  if (request->hasParam("zone")) {
    int zone = request->getParam("zone")->value().toInt();
    if (zone >= 0 && zone < MAX_ZONES) {
      zones[zone].isOn = false;
      digitalWrite(zones[zone].pin, LOW);
      request->send(200, "text/html", "Updated. Redirecting...");
      request->sendContent(F("<meta http-equiv='refresh' content='3; url=/' />"));
    } else {
      request->send(400, "text/plain", "Invalid zone");
    }
  } else {
    request->send(400, "text/plain", "Missing parameters");
  }
}

void handleReboot(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Rebooting...");
  request->send(200, "text/html", "<html>Configuration saved. Please reboot manually from the homepage.<br><a href='/'>Return to Home</a></html>");
}

void handleConfigPage(AsyncWebServerRequest *request) {
  if (request->hasParam("pw")) {
    String providedPassword = request->getParam("pw")->value();
    if (strcmp(adminPassword, providedPassword.c_str()) == 0) {
      request->send(200, "text/html", "");
      request->sendContent(F("<html><form action='/config_net' method='GET'>"));
      request->sendContent(F("SSID: <input type='text' name='ssid' value='") + WiFi.SSID() + F("'><br>"));
      request->sendContent(F("SSID PW: <input type='password' name='ssid_pw' value='***'><br>"));
      request->sendContent(F("IP: <input type='text' name='ip' value='") + WiFi.localIP().toString() + F("'><br>"));
      request->sendContent(F("GW: <input type='text' name='gw' value='") + WiFi.gatewayIP().toString() + F("'><br>"));
      request->sendContent(F("Mask: <input type='text' name='mask' value='") + WiFi.subnetMask().toString() + F("'><br>"));
      for (int i = 0; i < 5; i++) {
        request->sendContent(F("Time server IP ") + String(i) + F(": <input type='text' name='time_server_") + String(i) + F("' value='") + String(timeServers[i]) + F("'><br>"));
      }
      request->sendContent(F("<input type='submit' value='Submit'>"));
      request->sendContent(F("</form></html>"));
    } else {
      request->send(401, "text/plain", "Unauthorized");
    }
  } else {
    request->send(400, "text/plain", "Password required");
  }
}

void handleConfigNet(AsyncWebServerRequest *request) {
  if (request->hasParam("ssid") && request->hasParam("ssid_pw") && request->hasParam("ip") && request->hasParam("gw") && request->hasParam("mask")) {
    String ssid = request->getParam("ssid")->value();
    String ssidPw = request->getParam("ssid_pw")->value();
    if (ssidPw == "***") {
      request->send(400, "text/plain", "Invalid password");
      return;
    }
    String ip = request->getParam("ip")->value();
    String gw = request->getParam("gw")->value();
    String mask = request->getParam("mask")->value();

    for (int i = 0; i < 5; i++) {
      String paramName = "time_server_" + String(i);
      if (request->hasParam(paramName)) {
        timeServers[i] = request->getParam(paramName)->value().c_str();
      }
    }

    // Save the new network configuration to LittleFS
    saveNetworkConfiguration();

    request->send(200, "text/html", "Updated. Redirecting...");
    request->sendContent(F("<meta http-equiv='refresh' content='3; url=/' />"));
    request->send(200, "text/html", "<html>Configuration saved. Please reboot manually from the homepage.<br><a href='/'>Return to Home</a></html>");
  } else {
    request->send(400, "text/plain", "All fields are required");
  }
}

void update_time_from_server() {
  int result = get_http_time(&currentTimeData, timeServers, MAX_ZONES, 80);
  if (result == 0) {
    Serial.println("Time updated successfully");
  } else {
    Serial.println("Failed to update time");
  }
}

void loop() {
  static unsigned long lastNetworkCheck = 0;
  static unsigned long lastTimeUpdate = 0;
  static unsigned long lastZoneCheck = 0;

  // Check WiFi every NET_TEST_S seconds
  if (millis() - lastNetworkCheck >= NET_TEST_S * 1000) {
    lastNetworkCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Attempting to reconnect to WiFi...");
      WiFi.reconnect();
    }
  }

  // Update time every TIME_RETR_S seconds
  if (millis() - lastTimeUpdate >= TIME_RETR_S * 1000) {
    lastTimeUpdate = millis();
    update_time_from_server();
  }

  // Check zone timers every 1 second
  if (millis() - lastZoneCheck >= 1000) {
    lastZoneCheck = millis();
    for (int i = 0; i < MAX_ZONES; i++) {
      if (zones[i].isOn && millis() >= zones[i].endTime) {
        zones[i].isOn = false;
        digitalWrite(zones[i].pin, LOW);
        Serial.println("Zone " + String(i) + " turned off");
      }
    }
  }
} 
