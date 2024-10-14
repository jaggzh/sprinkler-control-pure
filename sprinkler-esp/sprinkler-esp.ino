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
  String name;
};

Zone zones[MAX_ZONES];

// Custom parameters for static IP configuration
char static_ip[16] = "192.168.1.50";
char static_gw[16] = "192.168.1.1";
char static_sn[16] = "255.255.255.0";

WiFiManagerParameter custom_ip("ip", "Static IP", static_ip, 16);
WiFiManagerParameter custom_gw("gw", "Gateway", static_gw, 16);
WiFiManagerParameter custom_sn("sn", "Subnet Mask", static_sn, 16);

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
  /* LittleFS.format(); */
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
  loadZoneConfiguration();

  // Check if network configuration is already saved
  String savedSSID = readFromFile("/net-ssid");
  String savedPassword = readFromFile("/net-ssidpw");
  String savedIP = readFromFile("/net-ip");
  String savedGW = readFromFile("/net-gw");
  String savedMask = readFromFile("/net-mask");

  if (savedSSID.length() > 0 && savedPassword.length() > 0) {
    // If credentials are saved, apply static IP if available and connect directly
    sp("Connecting to saved WiFi: ");
    sp(savedSSID);
    if (savedIP.length() > 0 && savedGW.length() > 0 && savedMask.length() > 0) {
      // Convert the saved strings to IPAddress objects
      IPAddress localIP, gateway, subnet;
      localIP.fromString(savedIP);
      gateway.fromString(savedGW);
      subnet.fromString(savedMask);

      // Configure WiFi to use static IP
      WiFi.config(localIP, gateway, subnet);

      sp(" with static IP: ");
      sp(savedIP);
      sp(", Gateway: ");
      sp(savedGW);
      sp(", Netmask: ");
      spl(savedMask);
    }

    // Start WiFi connection
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
      startWiFiManager();
    }
  } else {
    // If no credentials are saved, use WiFiManager
    spl("No saved WiFi credentials, starting WiFiManager");
    startWiFiManager();
  }

  // Initialize zones only after WiFi is configured
  if (wm_done) {
    for (int i = 0; i < MAX_ZONES; i++) {
      if (zones[i].pin != -1) {  // Only configure if pin is valid
        pinMode(zones[i].pin, OUTPUT);
        digitalWrite(zones[i].pin, LOW);
        zones[i].endTime = 0;
        zones[i].isOn = false;
        spl("Zone " + String(i) + " initialized on pin " + String(zones[i].pin));
      }
    }
    spl("Zones initialized");
  }

  // Setup web server routes
  server.on("/", HTTP_GET, handleRootPage);
  server.on("/s.css", HTTP_GET, handleCSS);
  server.on("/on", HTTP_GET, handleZoneOn);
  server.on("/off", HTTP_GET, handleZoneOff);
  server.on("/reboot", HTTP_GET, handleReboot);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/config_net", HTTP_GET, handleConfigNet);
  server.on("/config_zones", HTTP_GET, handleConfigZones);
  server.on("/store", HTTP_GET, handleStoreConfig);
  server.on("/wipe_zones", HTTP_GET, handleWipeZones);

  server.begin();
  spl("Web server started");

  // Time update from HTTP
  update_time_from_server();
}

void startWiFiManager() {
  WiFiManager wm;

  // Add custom parameters for IP, Gateway, and Subnet Mask
  wm.addParameter(&custom_ip);
  wm.addParameter(&custom_gw);
  wm.addParameter(&custom_sn);

  if (!wm.autoConnect(AP_SSID)) {
    spl("Failed to connect to WiFi. Restarting...");
    delay(3000);
    ESP.restart();
  }

  // Retrieve the user-provided static IP settings
  String ip = custom_ip.getValue();
  String gw = custom_gw.getValue();
  String sn = custom_sn.getValue();

  if (ip.length() > 0 && gw.length() > 0 && sn.length() > 0) {
    IPAddress localIP, gateway, subnet;
    localIP.fromString(ip);
    gateway.fromString(gw);
    subnet.fromString(sn);

    // Configure WiFi with static IP if provided
    WiFi.config(localIP, gateway, subnet);
    spl("Static IP configured: " + localIP.toString());
  }

  saveNetworkConfiguration(); // Save the network credentials and optional IP settings
  wm_done = true;
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

void handleStoreConfig() {
  bool success = true;

  // Save network settings if they are passed
  if (server.hasArg("ssid") && server.hasArg("ssid_pw") && server.hasArg("ip") && server.hasArg("gw") && server.hasArg("mask")) {
    String ssid = server.arg("ssid");
    String ssidPw = server.arg("ssid_pw");
    String ip = server.arg("ip");
    String gw = server.arg("gw");
    String mask = server.arg("mask");

    // Validate settings (for example, reject password '***')
    if (ssidPw == "***") {
      server.send(400, "text/plain", "Invalid password. Settings not saved.");
      return;
    }

    // Save network configuration
    saveToFile("/net-ssid", ssid);
    saveToFile("/net-ssidpw", ssidPw);
    saveToFile("/net-ip", ip);
    saveToFile("/net-gw", gw);
    saveToFile("/net-mask", mask);
    spl("Network settings saved.");
  }

  // Save time servers if they are passed
  for (int i = 0; i < 5; i++) {
    String paramName = "time_server_" + String(i);
    if (server.hasArg(paramName)) {
      timeServers[i] = server.arg(paramName).c_str();
      saveToFile(("/time-server-" + String(i)).c_str(), timeServers[i]);
      spl("Time server " + String(i) + " saved as " + timeServers[i]);
    }
  }

  // Save zone configurations if they are passed
  for (int i = 0; i < MAX_ZONES; i++) {
    String pinParam = "zone" + String(i) + "_pin";
    String nameParam = "zone" + String(i) + "_name";
    if (server.hasArg(pinParam)) {
      int pin = server.arg(pinParam).toInt();
      String zoneName = server.hasArg(nameParam) ? server.arg(nameParam) : "Zone " + String(i);

      // Validate the pin value before saving
      if (pin >= 0) {
        zones[i].pin = pin;
        zones[i].name = zoneName;

        // Save the zone configuration to LittleFS
        saveToFile(("/zone" + String(i) + "-pin").c_str(), String(pin));
        saveToFile(("/zone" + String(i) + "-name").c_str(), zoneName);
        spl("Zone " + String(i) + " configured with pin " + String(pin) + " and name " + zoneName);
      }
    }
  }

  // Respond to client with success message and redirect to main page
  redir_plain();
}

void handleWipeZones() {
  for (int i = 0; i < MAX_ZONES; i++) {
    String pinFile = "/zone" + String(i) + "-pin";
    String nameFile = "/zone" + String(i) + "-name";
    if (LittleFS.exists(pinFile)) {
      LittleFS.remove(pinFile);
      spl("Removed " + pinFile);
    }
    if (LittleFS.exists(nameFile)) {
      LittleFS.remove(nameFile);
      spl("Removed " + nameFile);
    }
  }
  redir_plain();
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

void loadZoneConfiguration() {
  for (int i = 0; i < MAX_ZONES; i++) {
    String pinValue = readFromFile(("/zone" + String(i) + "-pin").c_str());
    String nameValue = readFromFile(("/zone" + String(i) + "-name").c_str());
    if (pinValue.length() > 0) {
      zones[i].pin = pinValue.toInt();
      zones[i].name = nameValue.length() > 0 ? nameValue : "Zone " + String(i);
      spl("Loaded zone " + String(i) + " with pin " + String(zones[i].pin));
    } else {
      zones[i].pin = -1;  // Invalid pin to indicate no configuration
    }
  }
}

void handleConfigZones() {
  String content = "<html><form action='/store' method='GET'>";
  content += "<h3>Configure Sprinkler Zones</h3>";
  content += "<h4>Pin ref:</h4>";
  content += "<b>&nbsp; <b>I2C:</b> D1:5, D2:4<br />";
  content += "<b>&nbsp; <b>Preferred:</b> D4:2 (led), D5:14, D6:12, D7:13<br />";
  content += "<h4>Zone config</h4>";
  for (int i = 0; i < MAX_ZONES; i++) {
    content += "Zone " + String(i) + " Pin: <input type='text' name='zone" + String(i) + "_pin' value='" + String(zones[i].pin != -1 ? zones[i].pin : 0) + "'><br>";
    content += "Zone " + String(i) + " Name: <input type='text' name='zone" + String(i) + "_name' value='" + zones[i].name + "'><br><br>";
  }
  content += "<input type='submit' value='Save Zones'>";
  content += "</form>
  content += "<a href='/wipe_zones'>Wipe Zones</a>";
  content += "</html>";
  server.send(200, "text/html", content);
}

void handleCSS() {
  server.send(200, "text/css", F(
    ".num{ width: 6em; }"
    ".noblock { display: inline-block; margin-right: .5em; }"
  ));
}

void handleRootPage() {
server.sendContent("HTTP/1.0 200 OK\r\n");
server.sendContent("Content-Type: text/html; charset=utf-8\r\n\r\n");
  server.sendContent(F(
    "<html><head><link rel=\"stylesheet\" href=\"/s.css\"></head><body>"
    "<h3>Sprinkler Main</h3>"));
  server.sendContent(F("Uptime: ")); 
  server.sendContent(String(millis() / 1000) + " seconds<br>");
  for (int i = 0; i < MAX_ZONES; i++) {
    if (zones[i].pin != -1) {
      server.sendContent(F("Zone ") + zones[i].name + F("<br>"));
      server.sendContent(F("<form class=noblock action='/on' method='GET'>"));
      server.sendContent(F("<input type='hidden' name='zone' value='") + String(i) + F("'>"));
      server.sendContent(F("<input class='num' type='text' name='duration' value='30'> <input type='submit' value='On'>"));
      server.sendContent(F("</form>"));
      server.sendContent(F("<form class=noblock action='/off' method='GET'>"));
      server.sendContent(F("<input type='hidden' name='zone' value='") + String(i) + F("'>"));
      server.sendContent(F("<input type='submit' value='Off'>"));
      server.sendContent(F("</form><br>"));
    }
  }
  server.sendContent(F("<a href='/config_zones'>Configure Zones</a><br>"));
  server.sendContent(F("<a href='/config'>Network Settings</a><br>"));
  server.sendContent(F("<a href='/reboot'>Reboot link</a><br>"));
  server.sendContent(F("<a href='/ota'>Flash OTA</a></html>"));
}

void redir_plain() {
  server.send(200, "text/html",
    F("Done. Redirecting...<meta http-equiv='refresh' content='3; url=/' />"));
}

void handleZoneOn() {
  if (server.hasArg("zone") && server.hasArg("duration")) {
    int zone = server.arg("zone").toInt();
    unsigned long duration = server.arg("duration").toInt() * 1000;
    if (zone >= 0 && zone < MAX_ZONES && duration > 0 && zones[zone].pin != -1) {
      zones[zone].isOn = true;
      zones[zone].endTime = millis() + duration;
      digitalWrite(zones[zone].pin, HIGH);
      redir_plain();
    } else {
      server.send(400, "text/plain", "Invalid zone, duration, or zone not configured");
    }
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleZoneOff() {
  if (server.hasArg("zone")) {
    int zone = server.arg("zone").toInt();
    if (zone >= 0 && zone < MAX_ZONES && zones[zone].pin != -1) {
      zones[zone].isOn = false;
      digitalWrite(zones[zone].pin, LOW);
      redir_plain();
    } else {
      server.send(400, "text/plain", "Invalid zone or zone not configured");
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

    redir_plain();
  } else {
    server.send(400, "text/plain", "All fields are required");
  }
}

void update_time_from_server() {
  return;
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
  // Handle WiFi and zone actions in loop
  server.handleClient();

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

// vim: sw=2 ts=2 et
