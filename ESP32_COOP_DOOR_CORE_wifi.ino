#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Stepper.h>
#include <time.h>
#include <Preferences.h>

//Hardware Notes: DOIT ESP32 DEVKIT V1 Driver: esp32 by Expressif Systems v 2.0.17

// 1. Enter your WiFi credentials here:
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const char* tzInfo = "MST7MDT,M3.2.0,M11.1.0"; 
const char* ntpServer = "pool.ntp.org";

WebServer server(80);
Preferences preferences; 

#define IN1 25
#define IN2 26
#define IN3 27
#define IN4 14
#define LIGHT 34

const int stepsPerRev = 2048; 
Stepper myStepper(stepsPerRev, IN1, IN3, IN2, IN4); 

// Core Status
int doorStatus = 0;        
float doorNbTurn = 5.0;  
int envState = 0; // 0 = Night Cycle, 1 = Day Cycle

// Settings
int openMode = 0;  
int closeMode = 0; 
int openThreshold = 400;   
int closeThreshold = 1;   
int openTimeHour = 7, openTimeMin = 0;   
int closeTimeHour = 20, closeTimeMin = 0; 

// Timers
unsigned long lightOpenTimer = 0;
unsigned long lightCloseTimer = 0;

const unsigned long triggerDelay = 30000; // 30 Second Delay

// HTML Interface
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Coop Door Controller</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background: #f4f4f4; margin: 0; padding: 20px 0;}
    .card { background: white; padding: 20px; border-radius: 10px; box-shadow: 2px 2px 12px rgba(0,0,0,0.1); width: 90%; max-width: 400px; margin: 0 auto 20px auto; }
    .btn { padding: 15px; font-size: 18px; margin: 5px; cursor: pointer; border-radius: 8px; border: none; color: white; width: 45%; font-weight: bold;}
    .btn-open { background-color: #4CAF50; }
    .btn-close { background-color: #f44336; }
    .btn-save { background-color: #2196F3; width: 100%; margin-top: 15px;}
    .stat { font-size: 16px; margin: 8px 0; text-align: left; padding: 0 10px;}
    .settings-group { border: 1px solid #ddd; padding: 15px; border-radius: 5px; margin-bottom: 15px; text-align: left;}
    label { display: block; margin-top: 10px; font-size: 14px; color: #555;}
    input[type="number"], input[type="time"] { font-size: 14pt; width: 100%; padding: 8px; margin-top: 4px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; }
    hr { border: 0; border-top: 1px solid #eee; margin: 15px 0; }
    
    .toggle-container { margin-top: 8px; display: flex; align-items: center; justify-content: center; background: #f9f9f9; padding: 10px; border-radius: 8px;}
    .switch { position: relative; display: inline-block; width: 50px; height: 26px; margin: 0 15px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #4CAF50; transition: .4s; border-radius: 26px; }
    .slider:before { position: absolute; content: ""; height: 20px; width: 20px; left: 3px; bottom: 3px; background-color: white; transition: .4s; border-radius: 50%; }
    input:checked + .slider { background-color: #2196F3; }
    input:checked + .slider:before { transform: translateX(24px); }
    .toggle-label { font-weight: bold; color: #666; font-size: 14px; }
    .toggle-light { color: #4CAF50; }
    .toggle-time { color: #2196F3; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Coop Status</h2>
    <div class="stat">Door: <strong id="doorState">...</strong></div>
    <div class="stat">Light: <strong id="lightState">...</strong></div>
    <div class="stat">Time: <strong id="timeState">...</strong></div>
    <hr>
    <button class="btn btn-open" onclick="sendCommand('/open')">OPEN</button>
    <button class="btn btn-close" onclick="sendCommand('/close')">CLOSE</button>
  </div>

  <div class="card">
    <h2>Settings</h2>
    <div class="settings-group">
      <strong>Opening Trigger</strong>
      <div class="toggle-container">
        <span class="toggle-label toggle-light">Light</span>
        <label class="switch"><input type="checkbox" id="openMode"><span class="slider"></span></label>
        <span class="toggle-label toggle-time">Time</span>
      </div>
      <label>Light Threshold (Bright):</label>
      <input type="number" id="openThreshold">
      <label>Open Time:</label>
      <input type="time" id="openTime">
    </div>

    <div class="settings-group">
      <strong>Closing Trigger</strong>
      <div class="toggle-container">
        <span class="toggle-label toggle-light">Light</span>
        <label class="switch"><input type="checkbox" id="closeMode"><span class="slider"></span></label>
        <span class="toggle-label toggle-time">Time</span>
      </div>
      <label>Light Threshold (Dark):</label>
      <input type="number" id="closeThreshold">
      <label>Close Time:</label>
      <input type="time" id="closeTime">
    </div>

    <div class="settings-group">
      <strong>Motor Calibration</strong>
      <label>Door Revolutions:</label>
      <input type="number" id="doorNbTurn" step="0.1" min="0.1">
    </div>

    <button class="btn btn-save" onclick="saveSettings()">Save Settings</button>
  </div>

  <script>
    let isEditing = false;
    document.addEventListener('input', () => { isEditing = true; });
    document.addEventListener('change', () => { isEditing = true; });

    function sendCommand(url) {
      fetch(url).then(response => updateData());
    }
    
    function saveSettings() {
      let om = document.getElementById('openMode').checked ? 1 : 0;
      let cm = document.getElementById('closeMode').checked ? 1 : 0;
      let ot = document.getElementById('openTime').value;
      let ct = document.getElementById('closeTime').value;
      let ol = document.getElementById('openThreshold').value;
      let cl = document.getElementById('closeThreshold').value;
      let nt = document.getElementById('doorNbTurn').value;
      
      let url = `/settings?om=${om}&cm=${cm}&ot=${ot}&ct=${ct}&ol=${ol}&cl=${cl}&nt=${nt}`;
      fetch(url).then(res => { 
        alert("Settings Saved to Memory!"); 
        isEditing = false; 
        updateData(); 
      });
    }

    function updateData() {
      fetch('/status').then(res => res.json()).then(data => {
        document.getElementById('doorState').innerText = data.door == 1 ? "OPEN" : "CLOSED";
        document.getElementById('lightState').innerText = data.light;
        document.getElementById('timeState').innerText = data.time;
        
        if (!isEditing) {
          document.getElementById('openMode').checked = (data.openMode == 1);
          document.getElementById('closeMode').checked = (data.closeMode == 1);
          document.getElementById('openThreshold').value = data.openThreshold;
          document.getElementById('closeThreshold').value = data.closeThreshold;
          document.getElementById('openTime').value = data.openTime;
          document.getElementById('closeTime').value = data.closeTime;
          document.getElementById('doorNbTurn').value = data.doorNbTurn;
        }
      });
    }
    setInterval(updateData, 3000);
    updateData(); 
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  myStepper.setSpeed(12); 
  
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);

  preferences.begin("coop", false); 
  doorStatus     = preferences.getInt("doorStatus", 0);
  doorNbTurn     = preferences.getFloat("doorNbTurn", 5.0);
  envState       = preferences.getInt("envState", 0);
  openMode       = preferences.getInt("openMode", 0);
  closeMode      = preferences.getInt("closeMode", 0);
  openThreshold  = preferences.getInt("openThresh", 400);
  closeThreshold = preferences.getInt("closeThresh", 1);
  openTimeHour   = preferences.getInt("openTH", 7);
  openTimeMin    = preferences.getInt("openTM", 0);
  closeTimeHour  = preferences.getInt("closeTH", 20);
  closeTimeMin   = preferences.getInt("closeTM", 0);

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected!");

  configTzTime(tzInfo, ntpServer); 
  if (MDNS.begin("coop")) Serial.println("Visit http://coop.local");

  server.on("/", []() { server.send(200, "text/html", INDEX_HTML); });
  
  server.on("/open", []() { 
    if(doorStatus == 0) moveDoor(1); 
    server.send(200, "text/plain", "OK"); 
  });
  
  server.on("/close", []() { 
    if(doorStatus == 1) moveDoor(0); 
    server.send(200, "text/plain", "OK"); 
  });
  
  server.on("/settings", []() {
    if (server.hasArg("om")) { openMode = server.arg("om").toInt(); preferences.putInt("openMode", openMode); }
    if (server.hasArg("cm")) { closeMode = server.arg("cm").toInt(); preferences.putInt("closeMode", closeMode); }
    if (server.hasArg("ol")) { openThreshold = server.arg("ol").toInt(); preferences.putInt("openThresh", openThreshold); }
    if (server.hasArg("cl")) { closeThreshold = server.arg("cl").toInt(); preferences.putInt("closeThresh", closeThreshold); }
    if (server.hasArg("nt")) { doorNbTurn = server.arg("nt").toFloat(); preferences.putFloat("doorNbTurn", doorNbTurn); }
    if (server.hasArg("ot")) {
      String ot = server.arg("ot");
      openTimeHour = ot.substring(0, 2).toInt(); openTimeMin = ot.substring(3, 5).toInt();
      preferences.putInt("openTH", openTimeHour); preferences.putInt("openTM", openTimeMin);
    }
    if (server.hasArg("ct")) {
      String ct = server.arg("ct");
      closeTimeHour = ct.substring(0, 2).toInt(); closeTimeMin = ct.substring(3, 5).toInt();
      preferences.putInt("closeTH", closeTimeHour); preferences.putInt("closeTM", closeTimeMin);
    }
    server.send(200, "text/plain", "Saved");
  });
  
  server.on("/status", []() {
    int currentLight = analogRead(LIGHT);
    struct tm timeinfo;
    char timeStr[12] = "Syncing..";
    if (getLocalTime(&timeinfo)) strftime(timeStr, sizeof(timeStr), "%I:%M %p", &timeinfo); 
    
    char otStr[6], ctStr[6];
    sprintf(otStr, "%02d:%02d", openTimeHour, openTimeMin);
    sprintf(ctStr, "%02d:%02d", closeTimeHour, closeTimeMin);

    String json = "{";
    json += "\"door\":" + String(doorStatus) + ",";
    json += "\"light\":" + String(currentLight) + ",";
    json += "\"time\":\"" + String(timeStr) + "\",";
    json += "\"openMode\":" + String(openMode) + ",";
    json += "\"closeMode\":" + String(closeMode) + ",";
    json += "\"openThreshold\":" + String(openThreshold) + ",";
    json += "\"closeThreshold\":" + String(closeThreshold) + ",";
    json += "\"doorNbTurn\":" + String(doorNbTurn, 1) + ",";
    json += "\"openTime\":\"" + String(otStr) + "\",";
    json += "\"closeTime\":\"" + String(ctStr) + "\"";
    json += "}";
    
    server.send(200, "application/json", json);
  });

  server.begin();
}

void loop() {
  server.handleClient();
  checkTriggers();
}

void checkTriggers() {
  int currentLight = analogRead(LIGHT);
  struct tm timeinfo;
  bool timeValid = getLocalTime(&timeinfo, 10);
  
  bool openConditionMet = false;
  bool closeConditionMet = false;

  // 1. Check Open Conditions
  if (openMode == 0) { 
    if (currentLight > openThreshold) {
      if (lightOpenTimer == 0) lightOpenTimer = millis();
      if (millis() - lightOpenTimer > triggerDelay) openConditionMet = true;
    } else { lightOpenTimer = 0; }
  } else if (openMode == 1 && timeValid) { 
    if (timeinfo.tm_hour == openTimeHour && timeinfo.tm_min == openTimeMin) openConditionMet = true;
  }

  // 2. Check Close Conditions
  if (closeMode == 0) { 
    if (currentLight < closeThreshold) {
      if (lightCloseTimer == 0) lightCloseTimer = millis();
      if (millis() - lightCloseTimer > triggerDelay) closeConditionMet = true;
    } else { lightCloseTimer = 0; }
  } else if (closeMode == 1 && timeValid) { 
    if (timeinfo.tm_hour == closeTimeHour && timeinfo.tm_min == closeTimeMin) closeConditionMet = true;
  }

  // 3. Edge-Trigger State Machine 
  if (openConditionMet && envState == 0) {
    envState = 1; 
    preferences.putInt("envState", 1); 
    if (doorStatus == 0) moveDoor(1);
  } 
  else if (closeConditionMet && envState == 1) {
    envState = 0; 
    preferences.putInt("envState", 0); 
    if (doorStatus == 1) moveDoor(0);
  }
}

void moveDoor(int targetStatus) {
  long totalSteps = stepsPerRev * doorNbTurn;
  if (targetStatus == 1) myStepper.step(-totalSteps);
  else myStepper.step(totalSteps);
  
  doorStatus = targetStatus;
  preferences.putInt("doorStatus", doorStatus);
  stopCoils(); 
}

void stopCoils() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}