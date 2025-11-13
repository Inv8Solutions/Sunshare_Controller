#include <SoftwareSerial.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "PZEM004TV1.h"

// ----------------------- ACCESS POINT CONFIG -----------------------
#define AP_SSID     "EnergyMonitor_AP"
#define AP_PASSWORD "12345678"

// ----------------------- PZEM CONFIG -----------------------
// Try different pin combinations if D5/D6 doesn't work
#define PZEM_RX D1
#define PZEM_TX D2

SoftwareSerial pzemSerial(PZEM_RX, PZEM_TX);
PZEM004TV1 pzem(&pzemSerial, PZEM_RX, PZEM_TX);

struct PZEMData {
  float voltage;
  float current;
  float power;
  float energy;
  float frequency;
  float powerFactor;
  bool connected;
  String error;
} pzemData;

// ----------------------- WEB SERVER -----------------------
ESP8266WebServer server(80);

// ----------------------- HTML PAGE -----------------------
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Energy Monitor</title>
    <style>
      body {
        font-family: Arial, sans-serif;
        background: #111;
        color: #eee;
        text-align: center;
        margin: 0;
        padding: 20px;
      }
      h1 { color: #4CAF50; }
      table {
        margin: 20px auto;
        border-collapse: collapse;
        width: 90%%;
        max-width: 500px;
      }
      td {
        border-bottom: 1px solid #333;
        padding: 10px;
        text-align: left;
      }
      tr:last-child td { border: none; }
      #status { color: #4CAF50; font-weight: bold; margin: 10px; }
      .debug { 
        color: #ff9800; 
        margin: 10px; 
        font-family: monospace;
        font-size: 12px;
        text-align: left;
        background: #222;
        padding: 10px;
        border-radius: 5px;
        max-width: 500px;
        margin: 20px auto;
      }
      .error { color: #f44336; }
      .success { color: #4CAF50; }
    </style>
  </head>
  <body>
    <h1>⚡ Energy Monitor</h1>
    <div id="status">Loading...</div>
    <table>
      <tr><td>Voltage</td><td id="voltage">--</td></tr>
      <tr><td>Current</td><td id="current">--</td></tr>
      <tr><td>Power</td><td id="power">--</td></tr>
      <tr><td>Energy</td><td id="energy">--</td></tr>
      <tr><td>Frequency</td><td id="frequency">--</td></tr>
      <tr><td>Power Factor</td><td id="pf">--</td></tr>
    </table>
    <div class="debug" id="debug">Debug info will appear here</div>

    <script>
      async function getData() {
        try {
          const res = await fetch("/data");
          const d = await res.json();
          
          document.getElementById("voltage").innerText   = d.voltage.toFixed(2) + " V";
          document.getElementById("current").innerText   = d.current.toFixed(2) + " A";
          document.getElementById("power").innerText     = d.power.toFixed(2) + " W";
          document.getElementById("energy").innerText    = d.energy.toFixed(3) + " kWh";
          document.getElementById("frequency").innerText = d.frequency.toFixed(2) + " Hz";
          document.getElementById("pf").innerText        = d.powerFactor.toFixed(2);
          
          document.getElementById("status").innerText    = d.connected ? "✅ PZEM Connected" : "❌ PZEM Not Found";
          document.getElementById("status").className    = d.connected ? "success" : "error";
          
          let debugInfo = "Last update: " + new Date().toLocaleTimeString() + "<br>";
          debugInfo += "Connection: " + (d.connected ? "✅ Connected" : "❌ Disconnected") + "<br>";
          debugInfo += "Error: " + (d.error || "None") + "<br>";
          debugInfo += "Uptime: " + Math.round(d.uptime/1000) + "s";
          
          document.getElementById("debug").innerHTML = debugInfo;
        } catch (error) {
          document.getElementById("debug").innerHTML = "Error fetching data: " + error;
        }
      }
      setInterval(getData, 3000);
      getData();
    </script>
  </body>
</html>
)rawliteral";

// ----------------------- FUNCTIONS -----------------------
void readPZEMData() {
  Serial.println("\n" + String(millis()) + " === READING PZEM DATA ===");
  
  // Reset data
  pzemData.connected = false;
  pzemData.error = "";
  
  // Test if SoftwareSerial is working
  Serial.print("SoftwareSerial status - Available: ");
  Serial.print(pzemSerial.available());
  Serial.print(", IsListening: ");
  Serial.println(pzemSerial.isListening());
  
  if (!pzemSerial.isListening()) {
    pzemData.error = "SoftwareSerial not listening";
    Serial.println("❌ SoftwareSerial not listening!");
    return;
  }

  // Read all parameters with individual error checking
  Serial.println("Reading voltage...");
  float v = pzem.readVoltage();
  Serial.print("Voltage result: "); Serial.println(v);
  
  Serial.println("Reading current...");
  float i = pzem.readCurrent();
  Serial.print("Current result: "); Serial.println(i);
  
  Serial.println("Reading power...");
  float p = pzem.readPower();
  Serial.print("Power result: "); Serial.println(p);
  
  Serial.println("Reading energy...");
  float e = pzem.readEnergy();
  Serial.print("Energy result: "); Serial.println(e);
  
  Serial.println("Reading frequency...");
  float f = pzem.readFrequency();
  Serial.print("Frequency result: "); Serial.println(f);
  
  Serial.println("Reading power factor...");
  float pf = pzem.readPowerFactor();
  Serial.print("Power Factor result: "); Serial.println(pf);

  // Comprehensive validity check
  bool voltageValid = (v > 80 && v < 300 && !isnan(v));
  bool currentValid = (i >= 0 && i < 100 && !isnan(i));
  
  Serial.print("Voltage valid: "); Serial.println(voltageValid);
  Serial.print("Current valid: "); Serial.println(currentValid);

  if (voltageValid && currentValid) {
    pzemData.connected = true;
    pzemData.voltage = v;
    pzemData.current = i;
    pzemData.power = p;
    pzemData.energy = e;
    pzemData.frequency = f;
    pzemData.powerFactor = pf;
    pzemData.error = "None";
    Serial.println("✅ PZEM DETECTED - Valid readings");
  } else {
    pzemData.connected = false;
    pzemData.voltage = 0;
    pzemData.current = 0;
    pzemData.power = 0;
    pzemData.energy = 0;
    pzemData.frequency = 0;
    pzemData.powerFactor = 0;
    
    if (!voltageValid) pzemData.error = "Invalid voltage: " + String(v);
    else if (!currentValid) pzemData.error = "Invalid current: " + String(i);
    else pzemData.error = "Unknown communication error";
    
    Serial.println("❌ PZEM NOT DETECTED - " + pzemData.error);
  }
  
  Serial.println("=== PZEM READ COMPLETE ===");
}

void testHardwarePins() {
  Serial.println("\n🔧 TESTING HARDWARE PINS...");
  Serial.print("PZEM_RX pin (D5): GPIO"); Serial.println(PZEM_RX);
  Serial.print("PZEM_TX pin (D6): GPIO"); Serial.println(PZEM_TX);
  
  // Test pin modes
  pinMode(PZEM_RX, INPUT);
  pinMode(PZEM_TX, OUTPUT);
  Serial.println("Pins configured for SoftwareSerial");
}

void testPZEMConnection() {
  Serial.println("\n🔍 TESTING PZEM CONNECTION...");
  
  for(int attempt = 1; attempt <= 5; attempt++) {
    Serial.print("Attempt "); Serial.print(attempt); Serial.println(":");
    
    float v = pzem.readVoltage();
    Serial.print("  Voltage: "); 
    if (v == NAN) Serial.println("NAN");
    else if (v < 0) Serial.println("Negative");
    else Serial.println(v);
    
    delay(1000);
  }
  
  Serial.println("PZEM Connection Test Complete\n");
}

void testAlternativePins() {
  Serial.println("\n🔄 TESTING ALTERNATIVE PIN CONFIGURATIONS...");
  
  // Common alternative pin configurations
  int rxPins[] = {D1, D2, D5, D6, D7};
  int txPins[] = {D1, D2, D5, D6, D7};
  
  for (int i = 0; i < 5; i++) {
    Serial.print("Testing RX:D"); Serial.print(rxPins[i]);
    Serial.print(" TX:D"); Serial.println(txPins[i]);
    
    // Note: In practice, you'd need to reinitialize SoftwareSerial here
    delay(100);
  }
}

void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleData() {
  readPZEMData();
  
  String json = "{";
  json += "\"voltage\":"     + String(pzemData.voltage, 2)     + ",";
  json += "\"current\":"     + String(pzemData.current, 2)     + ",";
  json += "\"power\":"       + String(pzemData.power, 2)       + ",";
  json += "\"energy\":"      + String(pzemData.energy, 3)      + ",";
  json += "\"frequency\":"   + String(pzemData.frequency, 2)   + ",";
  json += "\"powerFactor\":" + String(pzemData.powerFactor, 2) + ",";
  json += "\"connected\":"   + String(pzemData.connected ? "true" : "false") + ",";
  json += "\"error\":\""     + pzemData.error + "\",";
  json += "\"uptime\":"      + String(millis());
  json += "}";
  
  server.send(200, "application/json", json);
}

// ----------------------- SETUP -----------------------
void setup() {
  Serial.begin(115200);
  delay(2000); // Give more time for Serial to initialize
  
  Serial.println("\n\n🔧 INITIALIZING ENERGY MONITOR - ENHANCED DEBUGGING");
  Serial.println("===================================================");

  // Test hardware pins first
  testHardwarePins();

  // Initialize SoftwareSerial for PZEM
  Serial.println("\nInitializing SoftwareSerial for PZEM...");
  pzemSerial.begin(9600);
  delay(2000); // Give more time for SoftwareSerial to initialize
  
  Serial.print("SoftwareSerial started: ");
  Serial.println(pzemSerial.isListening() ? "✅ Listening" : "❌ Not Listening");

  // Test basic connection
  testPZEMConnection();

  // Show alternative pin options
  testAlternativePins();

  // Start Access Point
  Serial.println("\nStarting Access Point...");
  WiFi.mode(WIFI_AP);
  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  Serial.print("Access Point: ");
  Serial.println(apStarted ? "✅ Started" : "❌ Failed");
  Serial.print("📶 SSID: "); Serial.println(AP_SSID);
  Serial.print("🌐 IP: "); Serial.println(WiFi.softAPIP());

  // Start Web Server
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  
  Serial.println("✅ Web server running — visit http://192.168.4.1/");
  Serial.println("===================================================\n");

  // Initial PZEM read
  readPZEMData();
}

// ----------------------- LOOP -----------------------
void loop() {
  server.handleClient();
  
  // Periodic status every 10 seconds
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 10000) {
    Serial.print("\n🔄 System Status - Uptime: ");
    Serial.print(millis() / 1000);
    Serial.println("s");
    Serial.print("PZEM Connected: ");
    Serial.println(pzemData.connected ? "Yes" : "No");
    lastStatus = millis();
  }
  
  delay(100);
}
