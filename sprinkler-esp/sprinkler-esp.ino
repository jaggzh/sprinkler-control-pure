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
#define RELAY_ON_STATE  HIGH
#define RELAY_OFF_STATE LOW

struct Zone {
  int pin;
  unsigned long endTime;
  bool isOn;
  String name;
  int preset[MAX_ZONE_PRESETS];  // Added preset array to store preset durations
};

Zone zones[MAX_ZONES];

// Custom parameters for static IP configuration
char static_ip[16] = "192.168.1.50";
char static_gw[16] = "192.168.1.1";
char static_sn[16] = "255.255.255.0";

WiFiManagerParameter custom_ip("ip", "Static IP", static_ip, 16);
WiFiManagerParameter custom_gw("gw", "Gateway", static_gw, 16);
WiFiManagerParameter custom_sn("sn", "Subnet Mask", static_sn, 16);


/* void loadNetworkConfiguration(); */
/* void loadZoneConfiguration(); */
/* String readFromFile(const char* filename); */
/* void startWiFiManager(); */
/* void handleRootPage(); */
/* void handleCSS(); */
/* void handleZoneOn(); */
/* void handleZoneOff(); */
/* void handleReboot(); */
/* void handleConfigPage(); */
/* void handleConfigNet(); */
/* void handleConfigZones(); */
/* void handleStoreConfig(); */
/* void handleWipeZones(); */
/* void update_time_from_server(); */
/* void saveNetworkConfiguration(); */
/* void redir_plain(); */
/* void http200(); */
/* void mimehtml(); */
/* void sp(const String &s); */
/* void sp(int v); */
/* void spl(const String &s); */
/* void spl(int v); */

#define svc(s) server.sendContent(s)
void http200() { svc("HTTP/1.0 200 OK\r\n"); }
void mimehtml() { http200(); svc("Content-Type: text/html; charset=utf-8\r\n\r\n"); }

void sp(const String &s) { Serial.print(s); Serial.flush(); }
void sp(int v) { Serial.print(v); Serial.flush(); }
void spl(const String &s) { Serial.println(s); Serial.flush(); }
void spl(int v) { Serial.println(v); Serial.flush(); }

void initZonesInMem() {
  for (int i = 0; i < MAX_ZONES; i++) {
    zones[i].pin = -1;  // Invalid pin to indicate no configuration
    zones[i].endTime = 0;
    zones[i].isOn = false;
    zones[i].name = "";
    for (int j = 0; j < MAX_ZONE_PRESETS; j++) {
      zones[i].preset[j] = -1;  // Default preset to -1 indicating unset
    }
  }
}


void setup() {
  Serial.begin(115200);
  delay(100);
  initZonesInMem();

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
    spl(savedSSID);
    if (savedIP.length() > 0 && savedGW.length() > 0 && savedMask.length() > 0) {
      // Convert the saved strings to IPAddress objects
      IPAddress localIP, gateway, subnet;
      localIP.fromString(savedIP);
      gateway.fromString(savedGW);
      subnet.fromString(savedMask);

      // Configure WiFi to use static IP
      WiFi.config(localIP, gateway, subnet);

      sp(" Static IP: ");
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
        digitalWrite(zones[i].pin, RELAY_OFF_STATE);
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

        // Save the presets
        for (int j = 0; j < MAX_ZONE_PRESETS; j++) {
          String presetParam = "zone" + String(i) + "_pset" + String(j);
          if (server.hasArg(presetParam)) {
            int presetValue = server.arg(presetParam).toInt();
            zones[i].preset[j] = presetValue;

            // Save each preset to LittleFS
            saveToFile(("/zone" + String(i) + "_pset" + String(j)).c_str(), String(presetValue));
          }
        }
      }
    }
  }

  redir_plain();
}

void handleWipeZones() {
  for (int i = 0; i < MAX_ZONES; i++) {
    String pinFile = "/zone" + String(i) + "_pin";
    String nameFile = "/zone" + String(i) + "_name";
    if (LittleFS.exists(pinFile)) {
      LittleFS.remove(pinFile);
      spl("Removed " + pinFile);
    }
    if (LittleFS.exists(nameFile)) {
      LittleFS.remove(nameFile);
      spl("Removed " + nameFile);
    }
  }
  initZonesInMem();
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
    sp("Reading zone ");
    sp(i);
    sp("... ");
    sp("pinValue:");
    sp(pinValue);
    sp(" nameValue:");
    spl(nameValue);
    if (pinValue.length() > 0) {
      zones[i].pin = pinValue.toInt();
      zones[i].name = nameValue.length() > 0 ? nameValue : "Zone " + String(i);

      // Load presets
      for (int j = 0; j < MAX_ZONE_PRESETS; j++) {
        String presetValue = readFromFile(("/zone" + String(i) + "_pset" + String(j)).c_str());
        if (presetValue.length() > 0) {
          zones[i].preset[j] = presetValue.toInt();
        } else {
          zones[i].preset[j] = -1;  // Default to -1 if not set
        }
      }
      spl("Loaded zone " + String(i) + " with pin " + String(zones[i].pin));
    } else {
      zones[i].pin = -1;
      zones[i].name = "";
      zones[i].isOn = false;
      zones[i].endTime = 0;
      for (int j = 0; j < MAX_ZONE_PRESETS; j++) {
        zones[i].preset[j] = -1;  // Default presets to -1
      }
      spl("Set zone " + String(i) + " to -1");
    }
  }
}

void handleConfigZones() {
  mimehtml();
  svc(F(
    "<html><head><link rel=\"stylesheet\" href=\"/s.css\"></head><body>"
    "<p>[ <a href='/'>Home</a> ]</p>"
    "<form action='/store' method='GET'>"
    "<h3>Configure Sprinkler Zones</h3>"
    "* Pin = -1 means zone unassigned"
    "<h4>Pin ref:</h4>"
    "<b>&nbsp; <b>I2C:</b> D1:5, D2:4<br />"
    "<b>&nbsp; <b>Preferred:</b> D4:2 (led), D5:14, D6:12, D7:13<br />"
    "<h4>Zone config</h4>"
  ));
  for (int i = 0; i < MAX_ZONES; i++) {
    svc(
      "<div class=zconf><h5>Zone " + String(i) + "</h5>\n"
       "<div>Pin: <input type='text' name='zone" + String(i) + "_pin' value='" + String(zones[i].pin) + "'></div>"
       "<div>Name: <input type='text' name='zone" + String(i) + "_name' value='" + zones[i].name + "'></div>"
       "<div>Presets:"
        "<div class=presets>\n"
       );
    // This zone Presets row contents:
    // 'zone#_pset#'
    for (int j = 0; j < MAX_ZONE_PRESETS; j++) {
      svc("<input type='text' name='zone" + String(i) + "_pset" + String(j) + "' value='" + String(zones[i].preset[j]) + "'>\n");
    }
    svc(
        "</div>" // presets row
       "</div>" // presets section
      "</div>" // zone
    );
  }
  svc(
    "<input type='submit' value='Save Zones'>"
    "</form>"
    "<p><br /><a href='/wipe_zones'>Wipe Zones</a></p>"
    "</html>"
  );
}

void handleCSS() {
  server.send(200, "text/css", F(
    "@media (max-width: 1081px) {"
      "body { font-size: 245%; }"
      "input, button { font-size: 1em; padding: .05em .4em .05em .4em; }"
    "}"
    "body { background: #bbb}"
    ".num{ width: 6em; }"
    "form { margin: .1em 0 .1em 0; }"
    ".zone { margin-top: .7em; padding: .1em .5em .1em .5em; border-top: 1px solid grey; border-bottom: 1px solid grey; border-collapse:collapse; }"
    ".noblock { display: inline-block; margin-right: .5em; }"
    "h3,h4,h5 { margin: .5em 0 .4em 0; }"
    ".zconf { padding-left: 2em; }"
    ".zconf div { margin: .1em 0 .1em 0; }"
    ".zconf div { margin: .1em 0 .1em 0; }"
    ".zconf input { width: 5em; }"
    ".stat { padding: .05em .5em .05em .5em; }"
    ".on { background: red; color: white; }"
    ".off { background: #99b; color: black; }"
  ));
}

void handleRootPage() {
  mimehtml();
  svc(F(
    "<html><head><link rel=\"stylesheet\" href=\"/s.css\"></head><body>"
    "<h3>Sprinkler Main</h3>"
    "Uptime: "
  )); 
  svc(String(millis() / 1000) + " seconds<br>");
  for (int i = 0; i < MAX_ZONES; i++) {
    if (zones[i].pin != -1) {
      svc(F("<div class='zone'>Zone ") + zones[i].name + " " + (zones[i].isOn ? F("<span class='stat on'>ON</span>") : F("<span class='stat off'>Off</span>")) + " ");
      svc(F("<form class=noblock action='/on' method='GET'>"));
      svc(F("<input type='hidden' name='zone' value='") + String(i) + F("'>"));
      svc(F(
        "<input class='num' type='text' name='duration' value='30'> <input type='submit' value='On'>"
        "</form>"
        "<form class=noblock action='/off' method='GET'>"));
      svc(F("<input type='hidden' name='zone' value='") + String(i) + F("'>"));
      svc(F(
        "<input type='submit' value='Off'>"
        "</form><br>"
      ));
      
      // Add preset buttons for each zone
      for (int j = 0; j < MAX_ZONE_PRESETS; j++) {
        if (zones[i].preset[j] != -1) {
          svc("<a href='/on?zone=" + String(i) + "&duration=" + String(zones[i].preset[j]) + "'>" + String(zones[i].preset[j]) + "s</a> ");
          if (j < MAX_ZONE_PRESETS - 1) {
            svc("| ");
          }
        }
      }
      svc("</div>"); // /zone

    }
  }
  svc(F(
    "<a href='/config_zones'>Configure Zones</a><br>"
    "<a href='/config'>Network Settings</a><br>"
    "<a href='/reboot'>Reboot link</a><br>"
    "<a href='/ota'>Flash OTA</a></html>"
  ));
}

void redir_zero() {
  server.send(200, "text/html",
    F("Done. Redirecting...<meta http-equiv='refresh' content='0; url=/' />"));
}
void redir_plain() {
  server.send(200, "text/html",
    F("Done. Redirecting...<meta http-equiv='refresh' content='1; url=/' />"));
}

void handleZoneOn() {
  if (server.hasArg("zone") && server.hasArg("duration")) {
    int zone = server.arg("zone").toInt();
    unsigned long duration = server.arg("duration").toInt() * 1000;
    if (zone >= 0 && zone < MAX_ZONES && duration > 0 && zones[zone].pin != -1) {
      zones[zone].isOn = true;
      zones[zone].endTime = millis() + duration;
      digitalWrite(zones[zone].pin, RELAY_ON_STATE);
      redir_zero();
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
      digitalWrite(zones[zone].pin, RELAY_OFF_STATE);
      redir_zero();
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
      String content =
        String("<html><form action='/config_net' method='GET'>\n") +
        "SSID: <input type='text' name='ssid' value='" + WiFi.SSID() + "'><br>" +
        "SSID PW: <input type='password' name='ssid_pw' value='***'><br>" +
        "IP: <input type='text' name='ip' value='" + WiFi.localIP().toString() + "'><br>" +
        "GW: <input type='text' name='gw' value='" + WiFi.gatewayIP().toString() + "'><br>" +
        "Mask: <input type='text' name='mask' value='" + WiFi.subnetMask().toString() + "'><br>\n";
      for (int i = 0; i < 5; i++) {
        content += "Time server IP " + String(i) + ": <input type='text' name='time_server_" + String(i) + "' value='" + String(timeServers[i]) + "'><br>";
      }
      content +=
        "<input type='submit' value='Submit'>"
        "</form></html>";
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
        digitalWrite(zones[i].pin, RELAY_OFF_STATE);
        spl("Zone " + String(i) + " turned off");
      }
    }
  }
} 

// vim: sw=2 ts=2 et
