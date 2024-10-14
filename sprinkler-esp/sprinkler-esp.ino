#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include "httptime.h"
#include "settings.h"

#define AP_SSID "ESP_Sprinkler_Config"
ESP8266WebServer server(80);
const char *timeServers[MAX_ZONES] = {"0.0.0.0", "0.0.0.0", "0.0.0.0", "0.0.0.0", "0.0.0.0"};
char adminPassword[32] = "";
struct timedata currentTimeData;
bool wm_done=false;

struct Zone {
  int pin;
  unsigned long endTime;
  bool isOn;
};

Zone zones[MAX_ZONES];

void sp(const String &message) {
  Serial.print(message);
  Serial.flush();
}

void spl(const String &message) {
  Serial.println(message);
  Serial.flush();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize LittleFS
  if (!LittleFS.begin()) {
    spl("LittleFS mount failed, formatting...");
    if (!LittleFS.format() || !LittleFS.begin()) {
      spl("LittleFS format failed");
      return;
    }
  }
  spl("LittleFS initialized");

  // Load configuration from LittleFS
  loadNetworkConfiguration();

  // Check if network configuration is already saved
  String savedSSID = readFromFile("/net-ssid");
  String savedPassword = readFromFile("/net-ssidpw");
  if (savedSSID.length() > 0 && savedPassword.length() > 0) {
    // If credentials are saved, connect directly
    spl("Connecting to saved WiFi...");
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    // Wait for connection
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(100);
      yield();
      sp(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      spl("\nConnected to WiFi");
      wm_done = true;
    } else {
      spl("\nFailed to connect to saved WiFi, starting WiFiManager");
  WiFiManager wm;
  if (!wm.autoConnect(AP_SSID)) {
        spl("Failed to connect to WiFi. Restarting...");
    delay(3000);
    ESP.restart();
  }
  saveNetworkConfiguration();
      wm_done = true;
    }
  } else {
    // If no credentials are saved, use WiFiManager
    spl("No saved WiFi credentials, starting WiFiManager");
    WiFiManager wm;
    if (!wm.autoConnect(AP_SSID)) {
      spl("Failed to connect to WiFi. Restarting...");
      delay(3000);
      ESP.restart();
    }
    saveNetworkConfiguration();
    wm_done = true;
  }

  // Initialize zones only after WiFi is configured
  if (wm_done) {
    for (int i = 0; i < MAX_ZONES; i++) {
      zones[i].pin = i + 2; // Example pin assignment
      pinMode(zones[i].pin, OUTPUT);
      digitalWrite(zones[i].pin, LOW);
      zones[i].endTime = 0;
      zones[i].isOn = false;
    }
    spl("Zones initialized");
  }

  // Setup web server routes
  server.on("/", HTTP_GET, handleRootPage);
  server.on("/on", HTTP_GET, handleZoneOn);
  server.on("/off", HTTP_GET, handleZoneOff);
  server.on("/reboot", HTTP_GET, handleReboot);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/config_net", HTTP_GET, handleConfigNet);

  server.begin();
  spl("Web server started");

  // Time update from HTTP
  update_time_from_server();
}

void saveToFile(const char* filename, const String& value) {
  File file = LittleFS.open(filename, "w");
  if (!file) {
    spl("Failed to open file for writing: " + String(filename));
    return;
  }
  file.print(value);
  file.close();
}

String readFromFile(const char* filename) {
  File file = LittleFS.open(filename, "r");
  if (!file) {
    spl("Failed to open file for reading: " + String(filename));
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
    spl("Loaded WiFi credentials from storage.");
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
    spl("Loaded network configuration from storage.");
  }

  for (int i = 0; i < 5; i++) {
    String serverIP = readFromFile( ("/time-server-" + String(i)).c_str());
    if (serverIP.length() > 0) {
      timeServers[i] = strdup(serverIP.c_str());
    }
  }
}

void handleRootPage() {
  String content = "<html>Sprinkler Main<br>";
  content += "Uptime: " + String(millis() / 1000) + " seconds<br>";
  for (int i = 0; i < MAX_ZONES; i++) {
    content += "Zone " + String(i) + "<br>";
    content += "<form action='/on' method='GET'>";
    content += "<input type='hidden' name='zone' value='" + String(i) + "'>";
    content += "<input type='text' name='duration' value='30'> <input type='submit' value='On'>";
    content += "</form>";
    content += "<form action='/off' method='GET'>";
    content += "<input type='hidden' name='zone' value='" + String(i) + "'>";
    content += "<input type='submit' value='Off'>";
    content += "</form><br>";
  }
  content += "<a href='/reboot'>Reboot link</a><br>";
  content += "<a href='/ota'>Flash OTA</a></html>";
  server.send(200, "text/html", content);
}

void handleZoneOn() {
  if (server.hasArg("zone") && server.hasArg("duration")) {
    int zone = server.arg("zone").toInt();
    unsigned long duration = server.arg("duration").toInt() * 1000;
    if (zone >= 0 && zone < MAX_ZONES && duration > 0) {
      zones[zone].isOn = true;
      zones[zone].endTime = millis() + duration;
      digitalWrite(zones[zone].pin, HIGH);
      server.send(200, "text/html", "Updated. Redirecting...<meta http-equiv='refresh' content='3; url=/' />");
    } else {
      server.send(400, "text/plain", "Invalid zone or duration");
    }
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleZoneOff() {
  if (server.hasArg("zone")) {
    int zone = server.arg("zone").toInt();
    if (zone >= 0 && zone < MAX_ZONES) {
      zones[zone].isOn = false;
      digitalWrite(zones[zone].pin, LOW);
      server.send(200, "text/html", "Updated. Redirecting...<meta http-equiv='refresh' content='3; url=/' />");
    } else {
      server.send(400, "text/plain", "Invalid zone");
    }
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleReboot() {
  server.send(200, "text/plain", "Rebooting...");
  delay(1000);
  ESP.restart();
}

void handleConfigPage() {
  if (server.hasArg("pw")) {
    String providedPassword = server.arg("pw");
    if (strcmp(adminPassword, providedPassword.c_str()) == 0) {
      String content = "<html><form action='/config_net' method='GET'>";
      content += "SSID: <input type='text' name='ssid' value='" + WiFi.SSID() + "'><br>";
      content += "SSID PW: <input type='password' name='ssid_pw' value='***'><br>";
      content += "IP: <input type='text' name='ip' value='" + WiFi.localIP().toString() + "'><br>";
      content += "GW: <input type='text' name='gw' value='" + WiFi.gatewayIP().toString() + "'><br>";
      content += "Mask: <input type='text' name='mask' value='" + WiFi.subnetMask().toString() + "'><br>";
      for (int i = 0; i < 5; i++) {
        content += "Time server IP " + String(i) + ": <input type='text' name='time_server_" + String(i) + "' value='" + String(timeServers[i]) + "'><br>";
      }
      content += "<input type='submit' value='Submit'>";
      content += "</form></html>";
      server.send(200, "text/html", content);
    } else {
      server.send(401, "text/plain", "Unauthorized");
    }
  } else {
    server.send(400, "text/plain", "Password required");
  }
}

void handleConfigNet() {
  if (server.hasArg("ssid") && server.hasArg("ssid_pw") && server.hasArg("ip") && server.hasArg("gw") && server.hasArg("mask")) {
    String ssid = server.arg("ssid");
    String ssidPw = server.arg("ssid_pw");
    if (ssidPw == "***") {
      server.send(400, "text/plain", "Invalid password");
      return;
    }
    String ip = server.arg("ip");
    String gw = server.arg("gw");
    String mask = server.arg("mask");

    for (int i = 0; i < 5; i++) {
      String paramName = "time_server_" + String(i);
      if (server.hasArg(paramName)) {
        timeServers[i] = server.arg(paramName).c_str();
      }
    }

    // Save the new network configuration to LittleFS
    saveNetworkConfiguration();

    server.send(200, "text/html", "Updated. Redirecting...<meta http-equiv='refresh' content='3; url=/' />");
  } else {
    server.send(400, "text/plain", "All fields are required");
  }
}

void update_time_from_server() {
  int result = get_http_time(&currentTimeData, timeServers, MAX_ZONES, 80);
  if (result == 0) {
    spl("Time updated successfully");
  } else {
    spl("Failed to update time");
  }
}

void loop() {
  static unsigned long lastNetworkCheck = 0;
  static unsigned long lastTimeUpdate = 0;
  static unsigned long lastZoneCheck = 0;

  if (!wm_done) {
    // WiFiManager setup not done, skip the loop
    return;
  }

  // Check WiFi every NET_TEST_S seconds
  if (millis() - lastNetworkCheck >= NET_TEST_S * 1000) {
    lastNetworkCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      spl("Attempting to reconnect to WiFi...");
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
        spl("Zone " + String(i) + " turned off");
      }
    }
  }
} 
