#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>

// ========== CONFIGURATION ==========
#define DEFAULT_AP_SSID "NodeMCU_Energy"
#define DEFAULT_AP_PASSWORD "12345678"
#define EEPROM_SIZE 512
#define SSID_ADDR 0
#define PASS_ADDR 32
#define WIFI_CONFIGURED 96

// Pin Configuration
#define RELAY_PIN D5
#define PZEM_RX_PIN D7
#define PZEM_TX_PIN D6
#define STATUS_LED D8

// Firestore Configuration (REST API)
#define FIREBASE_PROJECT_ID "sunshare-c24f4"
#define FIREBASE_API_KEY "AIzaSyBDC2gAGukmEpQGEAvaB__k7DTDbbzegrg"
#define FIRESTORE_HOST "firestore.googleapis.com"
#define DEVICE_ID "energy_monitor_001"

// Timing Configuration
#define DATA_UPDATE_INTERVAL 10000  // Send data every 10 seconds
#define PZEM_CHECK_INTERVAL 5000

// ========== GLOBAL OBJECTS ==========
ESP8266WebServer server(80);
SoftwareSerial pzemSerial(PZEM_RX_PIN, PZEM_TX_PIN);

// ========== GLOBAL VARIABLES ==========
String staSSID = "";
String staPassword = "";
bool wifiConfigured = false;
bool isConnectedToWiFi = false;

// PZEM Data Structure
struct PZEMData {
  float voltage = 0.0;
  float current = 0.0;
  float power = 0.0;
  float energy = 0.0;
  float frequency = 0.0;
  float pf = 0.0;
  bool connected = false;
};

PZEMData pzemData;
unsigned long lastDataSend = 0;
unsigned long lastPZEMCheck = 0;

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize hardware
  initializeHardware();
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Load saved credentials
  loadCredentials();
  
  // Start Access Point
  startAPMode();
  
  // Setup web server for configuration
  setupWebServer();
  
  Serial.println("\n=== NodeMCU Energy Monitor ===");
  Serial.println("Configuration Portal: http://192.168.4.1");
  Serial.println("Device ID: " + String(DEVICE_ID));
  Serial.println("==============================\n");
  
  // Try to connect to saved WiFi if available
  if (wifiConfigured && staSSID.length() > 0) {
    connectToWiFi();
  }
}

// ========== MAIN LOOP ==========
void loop() {
  server.handleClient();  // Handle web server requests
  
  handleWiFiReconnection();
  updatePZEMData();
  
  if (isConnectedToWiFi && millis() - lastDataSend > DATA_UPDATE_INTERVAL) {
    lastDataSend = millis();
    sendDataToFirestore();
  }
}

// ========== HARDWARE INITIALIZATION ==========
void initializeHardware() {
  pzemSerial.begin(9600);
  
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(STATUS_LED, LOW);
  
  Serial.println("Hardware initialized");
}

// ========== EEPROM MANAGEMENT ==========
void loadCredentials() {
  char ssid[32] = {0};
  char password[64] = {0};
  bool configured = false;
  
  EEPROM.get(SSID_ADDR, ssid);
  EEPROM.get(PASS_ADDR, password);
  EEPROM.get(WIFI_CONFIGURED, configured);
  
  if (configured && ssid[0] != 0) {
    staSSID = String(ssid);
    staPassword = String(password);
    wifiConfigured = true;
    Serial.println("✓ Loaded WiFi credentials for: " + staSSID);
  } else {
    Serial.println("No saved WiFi credentials found");
  }
}

void saveCredentials(const String& ssid, const String& password) {
  char ssid_arr[32] = {0};
  char password_arr[64] = {0};
  
  ssid.toCharArray(ssid_arr, 32);
  password.toCharArray(password_arr, 64);
  
  EEPROM.put(SSID_ADDR, ssid_arr);
  EEPROM.put(PASS_ADDR, password_arr);
  EEPROM.put(WIFI_CONFIGURED, true);
  EEPROM.commit();
  
  staSSID = ssid;
  staPassword = password;
  wifiConfigured = true;
  
  Serial.println("✓ Saved WiFi credentials for: " + ssid);
}

void clearCredentials() {
  char empty[1] = {0};
  EEPROM.put(SSID_ADDR, empty);
  EEPROM.put(PASS_ADDR, empty);
  EEPROM.put(WIFI_CONFIGURED, false);
  EEPROM.commit();
  
  staSSID = "";
  staPassword = "";
  wifiConfigured = false;
  isConnectedToWiFi = false;
  
  Serial.println("✓ Cleared WiFi credentials");
}

// ========== WIFI MANAGEMENT ==========
void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD);
  Serial.println("✓ Access Point: " + String(DEFAULT_AP_SSID));
  Serial.println("✓ AP IP: " + WiFi.softAPIP().toString());
}

bool connectToWiFi() {
  if (staSSID.length() == 0) return false;
  
  Serial.println("Connecting to: " + staSSID);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(staSSID.c_str(), staPassword.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    isConnectedToWiFi = true;
    digitalWrite(STATUS_LED, HIGH);
    Serial.println("\n✓ Connected! IP: " + WiFi.localIP().toString());
    return true;
  } else {
    isConnectedToWiFi = false;
    digitalWrite(STATUS_LED, LOW);
    Serial.println("\n✗ Connection failed");
    WiFi.mode(WIFI_AP);
    return false;
  }
}

void handleWiFiReconnection() {
  static unsigned long lastReconnectAttempt = 0;
  if (wifiConfigured && WiFi.status() != WL_CONNECTED) {
    if (millis() - lastReconnectAttempt > 30000) {
      lastReconnectAttempt = millis();
      Serial.println("Reconnecting to WiFi...");
      connectToWiFi();
    }
  }
}

// ========== PZEM DATA MANAGEMENT ==========
void updatePZEMData() {
  if (millis() - lastPZEMCheck > PZEM_CHECK_INTERVAL) {
    lastPZEMCheck = millis();
    
    bool previousStatus = pzemData.connected;
    pzemData.connected = true; // Simulated connection
    
    if (pzemData.connected != previousStatus) {
      Serial.println("PZEM: " + String(pzemData.connected ? "Connected" : "Disconnected"));
    }
    
    if (pzemData.connected) {
      pzemData.voltage = 230.0 + random(-100, 100) / 10.0;
      pzemData.current = 1.5 + random(-50, 100) / 10.0;
      pzemData.power = pzemData.voltage * pzemData.current;
      pzemData.energy += pzemData.power / 3600.0;
      pzemData.frequency = 50.0 + random(-5, 5) / 10.0;
      pzemData.pf = 0.95 + random(-20, 10) / 100.0;
      
      pzemData.voltage = constrain(pzemData.voltage, 210, 250);
      pzemData.current = constrain(pzemData.current, 0, 20);
      pzemData.pf = constrain(pzemData.pf, 0.5, 1.0);
      
      Serial.printf("PZEM - V: %.1fV, I: %.2fA, P: %.1fW, E: %.3fkWh\n", 
                    pzemData.voltage, pzemData.current, pzemData.power, pzemData.energy);
    } else {
      pzemData.voltage = 0.0;
      pzemData.current = 0.0;
      pzemData.power = 0.0;
      pzemData.frequency = 0.0;
      pzemData.pf = 0.0;
    }
  }
}

// ========== FIRESTORE COMMUNICATION ==========
void sendDataToFirestore() {
  if (!isConnectedToWiFi) {
    Serial.println("Not connected to WiFi, skipping Firestore upload");
    return;
  }

  Serial.println("Sending data to Firestore (REST API)...");

  // Build Firestore JSON document (Firestore requires "fields" structure)
  DynamicJsonDocument doc(512);
  JsonObject fields = doc.createNestedObject("fields");
  fields["device_id"]["stringValue"] = DEVICE_ID;
  fields["timestamp"]["integerValue"] = millis();
  fields["voltage"]["doubleValue"] = pzemData.voltage;
  fields["current"]["doubleValue"] = pzemData.current;
  fields["power"]["doubleValue"] = pzemData.power;
  fields["energy"]["doubleValue"] = pzemData.energy;
  fields["frequency"]["doubleValue"] = pzemData.frequency;
  fields["power_factor"]["doubleValue"] = pzemData.pf;
  fields["pzem_connected"]["booleanValue"] = pzemData.connected;

  String jsonData;
  serializeJson(doc, jsonData);

  WiFiClientSecure client;
  client.setInsecure(); // For testing only, skip cert validation

  if (client.connect(FIRESTORE_HOST, 443)) {
    String path = "/v1/projects/" + String(FIREBASE_PROJECT_ID) +
                  "/databases/(default)/documents/devices/" + String(DEVICE_ID) +
                  "?key=" + String(FIREBASE_API_KEY);

    client.println("PATCH " + path + " HTTP/1.1");
    client.println("Host: " + String(FIRESTORE_HOST));
    client.println("Connection: close");
    client.println("Content-Type: application/json");
    client.println("Content-Length: " + String(jsonData.length()));
    client.println();
    client.println(jsonData);

    unsigned long timeout = millis();
    while (client.connected() && millis() - timeout < 5000) {
      if (client.available()) {
        String line = client.readStringUntil('\n');
        if (line.startsWith("HTTP/1.1")) {
          if (line.indexOf("200") > 0 || line.indexOf("201") > 0) {
            Serial.println("✓ Data sent to Firestore successfully");
          } else {
            Serial.println("✗ Firestore error: " + line);
          }
        }
      }
    }

    client.stop();
  } else {
    Serial.println("✗ Failed to connect to Firestore");
  }
}

// ========== WEB SERVER FOR CONFIGURATION ==========
void setupWebServer() {
  // Serve configuration portal
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", createConfigPortalHTML());
  });
  
  // API endpoints
  server.on("/api/status", HTTP_GET, []() {
    String status = "{\"wifi_configured\":" + String(wifiConfigured ? "true" : "false") +
                   ",\"wifi_connected\":" + String(isConnectedToWiFi ? "true" : "false") +
                   ",\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\"" +
                   ",\"sta_ip\":\"" + (isConnectedToWiFi ? WiFi.localIP().toString() : "Not Connected") + "\"" +
                   ",\"sta_ssid\":\"" + (isConnectedToWiFi ? WiFi.SSID() : "Not Connected") + "\"" +
                   ",\"saved_ssid\":\"" + (wifiConfigured ? staSSID : "Not Configured") + "\"}";
    server.send(200, "application/json", status);
  });
  
  server.on("/api/scan", HTTP_GET, []() {
    WiFi.mode(WIFI_AP_STA);
    int n = WiFi.scanNetworks();
    
    String json = "[";
    for (int i = 0; i < n; i++) {
      if (i > 0) json += ",";
      json += "{";
      json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
      json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      json += "\"encrypted\":" + String(WiFi.encryptionType(i) != ENC_TYPE_NONE ? "true" : "false");
      json += "}";
    }
    json += "]";
    
    server.send(200, "application/json", json);
    WiFi.scanDelete();
    WiFi.mode(WIFI_AP);
  });
  
  server.on("/api/connect", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    
    if (ssid.length() == 0) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"SSID is required\"}");
      return;
    }
    
    saveCredentials(ssid, password);
    
    if (connectToWiFi()) {
      server.send(200, "application/json", "{\"success\":true,\"message\":\"Connected successfully!\"}");
    } else {
      server.send(200, "application/json", "{\"success\":true,\"message\":\"Credentials saved but connection failed. Device will retry.\"}");
    }
  });
  
  server.on("/api/disconnect", HTTP_POST, []() {
    clearCredentials();
    WiFi.disconnect();
    server.send(200, "application/json", "{\"success\":true,\"message\":\"WiFi disconnected and credentials cleared\"}");
  });
  
  server.onNotFound([]() {
    server.send(404, "text/plain", "404: Not Found");
  });
  
  server.begin();
  Serial.println("✓ HTTP server started on port 80");
}

String createConfigPortalHTML() {
  String html = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NodeMCU WiFi Setup</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 40px rgba(0,0,0,0.1);
            overflow: hidden;
            width: 100%;
            max-width: 500px;
        }
        .header {
            background: linear-gradient(135deg, #2c3e50, #34495e);
            color: white;
            padding: 40px 30px;
            text-align: center;
        }
        .header h1 {
            font-size: 2.2em;
            margin-bottom: 10px;
        }
        .header p {
            opacity: 0.9;
            font-size: 1.1em;
        }
        .content {
            padding: 40px 30px;
        }
        .status-card, .wifi-form {
            background: #f8f9fa;
            border-radius: 15px;
            padding: 25px;
            margin-bottom: 25px;
        }
        .status-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 12px;
            padding-bottom: 12px;
            border-bottom: 1px solid #e9ecef;
        }
        .status-item:last-child {
            margin-bottom: 0;
            padding-bottom: 0;
            border-bottom: none;
        }
        .status-label {
            font-weight: 600;
            color: #2c3e50;
        }
        .status-value {
            font-weight: bold;
        }
        .connected { color: #27ae60; }
        .disconnected { color: #e74c3c; }
        .form-group {
            margin-bottom: 20px;
        }
        .form-group label {
            display: block;
            margin-bottom: 8px;
            font-weight: 600;
            color: #2c3e50;
        }
        .form-control {
            width: 100%;
            padding: 15px;
            border: 2px solid #e9ecef;
            border-radius: 10px;
            font-size: 16px;
        }
        .btn {
            padding: 15px 25px;
            border: none;
            border-radius: 10px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
            width: 100%;
            margin-bottom: 10px;
        }
        .btn-primary { background: #3498db; color: white; }
        .btn-secondary { background: #95a5a6; color: white; }
        .btn-success { background: #27ae60; color: white; }
        .btn-danger { background: #e74c3c; color: white; }
        .btn:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(0,0,0,0.2); }
        .network-list {
            max-height: 200px;
            overflow-y: auto;
            border: 2px solid #e9ecef;
            border-radius: 10px;
            margin-top: 10px;
            background: white;
        }
        .network-item {
            padding: 15px;
            border-bottom: 1px solid #e9ecef;
            cursor: pointer;
        }
        .network-item:hover { background-color: #f8f9fa; }
        .network-ssid { font-weight: 600; margin-bottom: 5px; }
        .network-info { display: flex; justify-content: space-between; font-size: 0.9em; color: #7f8c8d; }
        .message {
            padding: 15px;
            border-radius: 10px;
            margin-bottom: 20px;
            font-weight: 600;
            text-align: center;
        }
        .message.success { background: #d4edda; color: #155724; }
        .message.error { background: #f8d7da; color: #721c24; }
        .hidden { display: none; }
        .loading { opacity: 0.7; pointer-events: none; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🔌 Energy Monitor</h1>
            <p>WiFi Setup & Configuration</p>
        </div>
        
        <div class="content">
            <div class="status-card">
                <h3 style="margin-bottom: 20px; color: #2c3e50;">📊 System Status</h3>
                <div class="status-item">
                    <span class="status-label">AP Status:</span>
                    <span class="status-value connected">● Running (192.168.4.1)</span>
                </div>
                <div class="status-item">
                    <span class="status-label">WiFi Config:</span>
                    <span class="status-value" id="configStatus">Checking...</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Connection:</span>
                    <span class="status-value" id="connectionStatus">Checking...</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Station IP:</span>
                    <span class="status-value" id="stationIp">Checking...</span>
                </div>
            </div>
            
            <div class="wifi-form">
                <h3 style="margin-bottom: 20px; color: #2c3e50;">📶 WiFi Setup</h3>
                
                <button type="button" class="btn btn-secondary" onclick="scanNetworks()" id="scanBtn">
                    🔍 Scan Networks
                </button>
                <div id="networkList" class="network-list hidden"></div>
                
                <form id="wifiForm" onsubmit="connectToWiFi(event)">
                    <div class="form-group">
                        <label for="ssid">Network Name:</label>
                        <input type="text" id="ssid" class="form-control" placeholder="Enter WiFi name" required>
                    </div>
                    <div class="form-group">
                        <label for="password">Password:</label>
                        <input type="password" id="password" class="form-control" placeholder="Enter WiFi password">
                    </div>
                    <button type="submit" class="btn btn-primary" id="connectBtn">📡 Connect</button>
                </form>
                
                <button type="button" class="btn btn-danger" onclick="disconnectWiFi()">🗑️ Clear Settings</button>
            </div>
            
            <div id="message" class="message hidden"></div>
        </div>
    </div>

    <script>
        function updateStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('configStatus').innerHTML = data.wifi_configured ? 
                        '<span style="color: #f39c12">●</span> Configured (' + data.saved_ssid + ')' : 
                        '<span style="color: #e74c3c">●</span> Not Configured';
                        
                    document.getElementById('connectionStatus').innerHTML = data.wifi_connected ? 
                        '<span style="color: #27ae60">●</span> Connected' : 
                        '<span style="color: #e74c3c">●</span> Not Connected';
                        
                    document.getElementById('stationIp').textContent = data.sta_ip;
                })
                .catch(error => {
                    console.error('Error:', error);
                });
        }

        function scanNetworks() {
            const btn = document.getElementById('scanBtn');
            btn.classList.add('loading');
            btn.innerHTML = '🔄 Scanning...';
            
            fetch('/api/scan')
                .then(response => response.json())
                .then(networks => {
                    const list = document.getElementById('networkList');
                    list.innerHTML = '';
                    
                    if (networks.length === 0) {
                        list.innerHTML = '<div class="network-item">No networks found</div>';
                    } else {
                        networks.forEach(network => {
                            const item = document.createElement('div');
                            item.className = 'network-item';
                            item.innerHTML = `
                                <div class="network-ssid">${network.ssid}</div>
                                <div class="network-info">
                                    <span>Signal: ${network.rssi} dBm</span>
                                    <span>${network.encrypted ? '🔒 Secured' : '🔓 Open'}</span>
                                </div>
                            `;
                            item.onclick = () => {
                                document.getElementById('ssid').value = network.ssid;
                                if (network.encrypted) {
                                    document.getElementById('password').focus();
                                }
                                list.classList.add('hidden');
                            };
                            list.appendChild(item);
                        });
                    }
                    
                    list.classList.remove('hidden');
                })
                .catch(error => {
                    console.error('Error:', error);
                    alert('Error scanning networks');
                })
                .finally(() => {
                    btn.classList.remove('loading');
                    btn.innerHTML = '🔍 Scan Networks';
                });
        }

        function connectToWiFi(event) {
            event.preventDefault();
            
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            const btn = document.getElementById('connectBtn');
            
            if (!ssid) {
                showMessage('Please enter WiFi name', 'error');
                return;
            }
            
            btn.classList.add('loading');
            btn.innerHTML = '🔄 Connecting...';
            
            fetch('/api/connect', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: 'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password)
            })
            .then(response => response.json())
            .then(data => {
                showMessage(data.message, data.success ? 'success' : 'error');
                if (data.success) {
                    setTimeout(() => {
                        updateStatus();
                    }, 2000);
                }
            })
            .catch(error => {
                console.error('Error:', error);
                showMessage('Connection failed. Please try again.', 'error');
            })
            .finally(() => {
                btn.classList.remove('loading');
                btn.innerHTML = '📡 Connect';
            });
        }

        function disconnectWiFi() {
            if (!confirm('Are you sure you want to clear WiFi settings?')) {
                return;
            }
            
            fetch('/api/disconnect', {
                method: 'POST'
            })
            .then(response => response.json())
            .then(data => {
                showMessage(data.message, 'info');
                setTimeout(() => {
                    updateStatus();
                }, 1000);
            })
            .catch(error => {
                console.error('Error:', error);
                showMessage('Error clearing settings', 'error');
            });
        }

        function showMessage(message, type) {
            const messageEl = document.getElementById('message');
            messageEl.textContent = message;
            messageEl.className = 'message ' + type;
            messageEl.classList.remove('hidden');
            
            setTimeout(() => {
                messageEl.classList.add('hidden');
            }, 5000);
        }

        // Update status every 5 seconds
        setInterval(updateStatus, 5000);
        
        // Initial status update
        updateStatus();
    </script>
</body>
</html>
)=====";
  return html;
}
