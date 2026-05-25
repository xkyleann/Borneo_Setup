#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <MQ135.h>

// WiFi Configuration
const char* AP_SSID     = "SoilSensor_Borneo";
const char* AP_PASSWORD = "";

// Pin Definitions
#define ONE_WIRE_BUS 4        // DS18B20 Temperature Sensor
#define MQ135_PIN 34          // MQ135 Gas Sensor (Analog)
#define SD_CS_PIN 5           // SD Card CS Pin
#define LED_PIN 2             // Built-in LED

// Sensor Objects
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
Adafruit_BMP280 bmp;
Adafruit_AHTX0 aht;
MQ135 mq135(MQ135_PIN);

WebServer server(80);

// Sensor Data
struct SensorData {
  float co2_ppm;
  float soil_temp;
  float air_temp;
  float humidity;
  float pressure;
  unsigned long timestamp;
  bool valid;
};

SensorData currentData;
SensorData dataHistory[100];  // Store last 100 readings
int historyIndex = 0;
int historyCount = 0;

// SD Card Status
bool sdCardAvailable = false;
String currentLogFile = "";

// Timing
unsigned long lastReadTime = 0;
unsigned long lastLogTime = 0;
const unsigned long READ_INTERVAL = 5000;   // Read sensors every 5 seconds
const unsigned long LOG_INTERVAL = 60000;   // Log to SD every 60 seconds

// Function Prototypes
void initSensors();
void readSensors();
void logToSD();
void createNewLogFile();
String getCSVHeader();
String dataToCSV(SensorData data);
void handleRoot();
void handleDownload();
void handleListFiles();
void handleDeleteFile();

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  Serial.println("\n\n=================================");
  Serial.println("Borneo Soil Respiration Sensor");
  Serial.println("=================================\n");
  
  // Initialize I2C
  Wire.begin();
  
  // Initialize Sensors
  initSensors();
  
  // Initialize SD Card
  Serial.print("Initializing SD card...");
  if (SD.begin(SD_CS_PIN)) {
    sdCardAvailable = true;
    Serial.println(" SUCCESS");
    createNewLogFile();
  } else {
    Serial.println(" FAILED - Will continue without SD card");
  }
  
  // Initialize WiFi AP
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("\nAccess Point started!");
  Serial.print("Network name : "); Serial.println(AP_SSID);
  Serial.print("Password     : "); Serial.println(AP_PASSWORD);
  Serial.print("IP Address   : "); Serial.println(WiFi.softAPIP());
  
  // Setup Web Server Routes
  server.on("/", handleRoot);
  server.on("/download", handleDownload);
  server.on("/list", handleListFiles);
  server.on("/delete", handleDeleteFile);
  server.begin();
  Serial.println("Web server started.\n");
  
  // Initial sensor reading
  readSensors();
}

void loop() {
  server.handleClient();
  
  unsigned long currentTime = millis();
  
  // Read sensors at regular intervals
  if (currentTime - lastReadTime >= READ_INTERVAL) {
    readSensors();
    lastReadTime = currentTime;
    
    // Blink LED to show activity
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
  }
  
  // Log to SD card at regular intervals
  if (sdCardAvailable && currentTime - lastLogTime >= LOG_INTERVAL) {
    logToSD();
    lastLogTime = currentTime;
  }
}

void initSensors() {
  Serial.println("Initializing sensors...");
  
  // DS18B20 Temperature Sensor
  Serial.print("  DS18B20 (Soil Temp)... ");
  ds18b20.begin();
  if (ds18b20.getDeviceCount() > 0) {
    Serial.println("OK");
  } else {
    Serial.println("NOT FOUND");
  }
  
  // BMP280 Pressure/Temperature Sensor
  Serial.print("  BMP280 (Pressure)... ");
  if (bmp.begin(0x76)) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
    Serial.println("OK");
  } else {
    Serial.println("NOT FOUND");
  }
  
  // AHT20 Temperature/Humidity Sensor
  Serial.print("  AHT20 (Humidity)... ");
  if (aht.begin()) {
    Serial.println("OK");
  } else {
    Serial.println("NOT FOUND");
  }
  
  // MQ135 Gas Sensor
  Serial.print("  MQ135 (CO2)... ");
  Serial.println("OK (warming up...)");
  
  Serial.println("Sensor initialization complete.\n");
}

void readSensors() {
  currentData.timestamp = millis();
  currentData.valid = true;
  
  // Read DS18B20 Soil Temperature
  ds18b20.requestTemperatures();
  currentData.soil_temp = ds18b20.getTempCByIndex(0);
  if (currentData.soil_temp == DEVICE_DISCONNECTED_C) {
    currentData.soil_temp = 0.0;
  }
  
  // Read BMP280 Pressure and Temperature
  currentData.pressure = bmp.readPressure() / 100.0; // Convert to hPa
  float bmp_temp = bmp.readTemperature();
  
  // Read AHT20 Temperature and Humidity
  sensors_event_t humidity_event, temp_event;
  aht.getEvent(&humidity_event, &temp_event);
  currentData.humidity = humidity_event.relative_humidity;
  currentData.air_temp = temp_event.temperature;
  
  // If AHT20 failed, use BMP280 temperature
  if (isnan(currentData.air_temp)) {
    currentData.air_temp = bmp_temp;
  }
  
  // Read MQ135 CO2 (with temperature and humidity compensation)
  float rzero = mq135.getRZero();
  currentData.co2_ppm = mq135.getCorrectedPPM(currentData.air_temp, currentData.humidity);
  
  // Validate readings
  if (isnan(currentData.co2_ppm) || currentData.co2_ppm < 0) {
    currentData.co2_ppm = 400.0; // Default atmospheric CO2
  }
  
  // Store in history
  dataHistory[historyIndex] = currentData;
  historyIndex = (historyIndex + 1) % 100;
  if (historyCount < 100) historyCount++;
  
  // Print to Serial
  Serial.printf("CO2: %.1f ppm | Soil: %.1f°C | Air: %.1f°C | Humidity: %.1f%% | Pressure: %.1f hPa\n",
                currentData.co2_ppm, currentData.soil_temp, currentData.air_temp, 
                currentData.humidity, currentData.pressure);
}

void createNewLogFile() {
  if (!sdCardAvailable) return;
  
  // Create filename with date/time (using millis as timestamp)
  unsigned long timestamp = millis() / 1000;
  currentLogFile = "/data_" + String(timestamp) + ".csv";
  
  File file = SD.open(currentLogFile, FILE_WRITE);
  if (file) {
    file.println(getCSVHeader());
    file.close();
    Serial.println("Created new log file: " + currentLogFile);
  } else {
    Serial.println("Failed to create log file");
  }
}

String getCSVHeader() {
  return "Timestamp,Uptime_sec,CO2_ppm,Soil_Temp_C,Air_Temp_C,Humidity_%,Pressure_hPa";
}

String dataToCSV(SensorData data) {
  char buffer[200];
  unsigned long uptime = data.timestamp / 1000;
  snprintf(buffer, sizeof(buffer), "%lu,%lu,%.2f,%.2f,%.2f,%.2f,%.2f",
           data.timestamp, uptime, data.co2_ppm, data.soil_temp, 
           data.air_temp, data.humidity, data.pressure);
  return String(buffer);
}

void logToSD() {
  if (!sdCardAvailable || currentLogFile == "") return;
  
  File file = SD.open(currentLogFile, FILE_APPEND);
  if (file) {
    file.println(dataToCSV(currentData));
    file.close();
    Serial.println("Data logged to SD card");
  } else {
    Serial.println("Failed to open log file for writing");
  }
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Borneo Soil Respiration Sensor</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #0a0e27 0%, #1a1f3a 100%);
            color: #e0e0e0;
            min-height: 100vh;
            padding: 20px;
        }
        .container { max-width: 1400px; margin: 0 auto; }
        header {
            text-align: center;
            padding: 30px 0;
            border-bottom: 2px solid rgba(79, 195, 247, 0.3);
            margin-bottom: 40px;
        }
        h1 {
            font-size: 2.5em;
            color: #4fc3f7;
            text-transform: uppercase;
            letter-spacing: 3px;
            margin-bottom: 10px;
            text-shadow: 0 0 20px rgba(79, 195, 247, 0.5);
        }
        .subtitle { color: #81c784; font-size: 1.1em; font-weight: 300; }
        .status-badge {
            display: inline-block;
            padding: 8px 20px;
            background: linear-gradient(135deg, #66bb6a 0%, #43a047 100%);
            border-radius: 25px;
            font-weight: bold;
            text-transform: uppercase;
            letter-spacing: 1px;
            box-shadow: 0 4px 15px rgba(102, 187, 106, 0.4);
            animation: pulse 2s infinite;
        }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.7; } }
        .led-indicator {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background: #66bb6a;
            display: inline-block;
            margin-right: 8px;
            box-shadow: 0 0 10px #66bb6a;
            animation: blink 1.5s infinite;
        }
        @keyframes blink { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
        .sensor-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
            margin: 30px 0;
        }
        .sensor-card {
            background: rgba(255, 255, 255, 0.05);
            border: 2px solid rgba(79, 195, 247, 0.3);
            border-radius: 20px;
            padding: 25px;
            position: relative;
            overflow: hidden;
            transition: all 0.3s ease;
            backdrop-filter: blur(10px);
        }
        .sensor-card:hover {
            transform: translateY(-5px);
            border-color: rgba(79, 195, 247, 0.6);
            box-shadow: 0 15px 40px rgba(79, 195, 247, 0.3);
        }
        .sensor-card::before {
            content: '';
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            height: 4px;
            background: linear-gradient(90deg, #4fc3f7, #81c784);
        }
        .sensor-icon { font-size: 2.5em; margin-bottom: 10px; }
        .sensor-label {
            font-size: 0.85em;
            color: #90caf9;
            text-transform: uppercase;
            letter-spacing: 1.5px;
            margin-bottom: 10px;
        }
        .sensor-reading {
            font-size: 2.8em;
            font-weight: bold;
            color: #fff;
            line-height: 1;
            margin: 10px 0;
            text-shadow: 0 2px 10px rgba(255, 255, 255, 0.2);
        }
        .sensor-unit { font-size: 0.4em; color: #b0bec5; margin-left: 5px; }
        .sensor-status { font-size: 0.8em; color: #81c784; margin-top: 10px; }
        .chart-container {
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid rgba(79, 195, 247, 0.2);
            border-radius: 15px;
            padding: 25px;
            margin: 30px 0;
            height: 350px;
        }
        .chart-title {
            font-size: 1.2em;
            color: #4fc3f7;
            margin-bottom: 20px;
            text-align: center;
        }
        .download-section {
            background: rgba(255, 255, 255, 0.05);
            border: 2px solid rgba(79, 195, 247, 0.3);
            border-radius: 15px;
            padding: 25px;
            margin: 30px 0;
        }
        .download-btn {
            display: inline-block;
            padding: 12px 30px;
            background: linear-gradient(135deg, #4fc3f7, #2196f3);
            color: white;
            text-decoration: none;
            border-radius: 25px;
            font-weight: bold;
            margin: 10px;
            transition: all 0.3s ease;
            border: none;
            cursor: pointer;
        }
        .download-btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 20px rgba(79, 195, 247, 0.5);
        }
        .info-section {
            margin-top: 40px;
            padding: 25px;
            background: rgba(255, 255, 255, 0.03);
            border-radius: 15px;
            border: 1px solid rgba(79, 195, 247, 0.1);
        }
        .info-row {
            display: flex;
            justify-content: space-between;
            padding: 15px 0;
            border-bottom: 1px solid rgba(255, 255, 255, 0.1);
        }
        .info-row:last-child { border-bottom: none; }
        .info-label { color: #90caf9; font-weight: 600; }
        .info-value { color: #fff; font-family: 'Courier New', monospace; }
        footer {
            text-align: center;
            margin-top: 50px;
            padding: 20px;
            color: #607d8b;
            font-size: 0.9em;
        }
        @media (max-width: 768px) {
            h1 { font-size: 1.8em; }
            .sensor-grid { grid-template-columns: 1fr; }
            .sensor-reading { font-size: 2em; }
            .chart-container { height: 250px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>🌿 Borneo Soil Respiration Sensor</h1>
            <p class="subtitle">Real-Time Environmental Monitoring</p>
            <div style="margin-top: 20px;">
                <span class="status-badge"><span class="led-indicator"></span>ACTIVE</span>
            </div>
        </header>
        
        <!-- Main Sensor Readings -->
        <div class="sensor-grid">
            <div class="sensor-card">
                <div class="sensor-icon">💨</div>
                <div class="sensor-label">CO₂ Concentration</div>
                <div class="sensor-reading">)rawliteral" + String(currentData.co2_ppm, 1) + R"rawliteral(<span class="sensor-unit">ppm</span></div>
                <div class="sensor-status">● )rawliteral" + String(currentData.co2_ppm < 1000 ? "Normal" : "Elevated") + R"rawliteral(</div>
            </div>
            
            <div class="sensor-card">
                <div class="sensor-icon">🌡️</div>
                <div class="sensor-label">Soil Temperature</div>
                <div class="sensor-reading">)rawliteral" + String(currentData.soil_temp, 1) + R"rawliteral(<span class="sensor-unit">°C</span></div>
                <div class="sensor-status">● Optimal</div>
            </div>
            
            <div class="sensor-card">
                <div class="sensor-icon">🌤️</div>
                <div class="sensor-label">Air Temperature</div>
                <div class="sensor-reading">)rawliteral" + String(currentData.air_temp, 1) + R"rawliteral(<span class="sensor-unit">°C</span></div>
                <div class="sensor-status">● Stable</div>
            </div>
            
            <div class="sensor-card">
                <div class="sensor-icon">💧</div>
                <div class="sensor-label">Humidity</div>
                <div class="sensor-reading">)rawliteral" + String(currentData.humidity, 1) + R"rawliteral(<span class="sensor-unit">%</span></div>
                <div class="sensor-status">● Good</div>
            </div>
            
            <div class="sensor-card">
                <div class="sensor-icon">🌍</div>
                <div class="sensor-label">Pressure</div>
                <div class="sensor-reading">)rawliteral" + String(currentData.pressure, 1) + R"rawliteral(<span class="sensor-unit">hPa</span></div>
                <div class="sensor-status">● Normal</div>
            </div>
            
            <div class="sensor-card">
                <div class="sensor-icon">⏱️</div>
                <div class="sensor-label">System Uptime</div>
                <div class="sensor-reading">)rawliteral" + String(millis() / 1000) + R"rawliteral(<span class="sensor-unit">sec</span></div>
                <div class="sensor-status">● Running</div>
            </div>
        </div>
        
        <!-- Data Download Section -->
        <div class="download-section">
            <h2 style="color: #4fc3f7; margin-bottom: 20px;">💾 Data Management</h2>
            <p style="margin-bottom: 20px;">SD Card Status: <strong style="color: )rawliteral" + String(sdCardAvailable ? "#66bb6a" : "#f44336") + R"rawliteral(;">)rawliteral" + String(sdCardAvailable ? "Available" : "Not Available") + R"rawliteral(</strong></p>
            <p style="margin-bottom: 20px;">Current Log File: <strong>)rawliteral" + currentLogFile + R"rawliteral(</strong></p>
            <p style="margin-bottom: 20px;">Data Points Logged: <strong>)rawliteral" + String(historyCount) + R"rawliteral(</strong></p>
            <div>
                <a href="/download" class="download-btn">📥 Download Current CSV</a>
                <a href="/list" class="download-btn">📋 List All Files</a>
            </div>
        </div>
        
        <!-- CO2 Trend Graph -->
        <div class="chart-container">
            <div class="chart-title">📊 CO₂ Concentration Trend</div>
            <canvas id="co2Chart"></canvas>
        </div>
        
        <!-- Temperature Trend Graph -->
        <div class="chart-container">
            <div class="chart-title">🌡️ Temperature Trends</div>
            <canvas id="tempChart"></canvas>
        </div>
        
        <!-- Device Information -->
        <div class="info-section">
            <h2 style="color: #4fc3f7; margin-bottom: 20px;">📋 Device Information</h2>
            <div class="info-row">
                <span class="info-label">Access Point Name</span>
                <span class="info-value">SoilSensor_Borneo</span>
            </div>
            <div class="info-row">
                <span class="info-label">Device IP Address</span>
                <span class="info-value">)rawliteral" + WiFi.softAPIP().toString() + R"rawliteral(</span>
            </div>
            <div class="info-row">
                <span class="info-label">MAC Address</span>
                <span class="info-value">)rawliteral" + WiFi.softAPmacAddress() + R"rawliteral(</span>
            </div>
            <div class="info-row">
                <span class="info-label">Firmware Version</span>
                <span class="info-value">v2.0.0</span>
            </div>
            <div class="info-row">
                <span class="info-label">Connected Clients</span>
                <span class="info-value">)rawliteral" + String(WiFi.softAPgetStationNum()) + R"rawliteral( devices</span>
            </div>
        </div>
        
        <footer>
            <p>🔬 Weatherproof • Autonomous • Field-Deployable</p>
            <p style="margin-top: 10px; font-size: 0.85em;">Borneo Soil Respiration Sensor © 2026</p>
        </footer>
    </div>
    
    <script>
        // Prepare data for charts
        const historyData = )rawliteral";
  
  // Add history data as JSON
  html += "[";
  for (int i = 0; i < historyCount; i++) {
    if (i > 0) html += ",";
    html += "{";
    html += "\"co2\":" + String(dataHistory[i].co2_ppm, 1) + ",";
    html += "\"soil\":" + String(dataHistory[i].soil_temp, 1) + ",";
    html += "\"air\":" + String(dataHistory[i].air_temp, 1);
    html += "}";
  }
  html += "]";
  
  html += R"rawliteral(;
        
        // CO2 Chart
        const co2Ctx = document.getElementById('co2Chart').getContext('2d');
        new Chart(co2Ctx, {
            type: 'line',
            data: {
                labels: historyData.map((_, i) => i + 1),
                datasets: [{
                    label: 'CO₂ (ppm)',
                    data: historyData.map(d => d.co2),
                    borderColor: '#4fc3f7',
                    backgroundColor: 'rgba(79, 195, 247, 0.1)',
                    borderWidth: 3,
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: { legend: { labels: { color: '#e0e0e0' } } },
                scales: {
                    y: { beginAtZero: false, grid: { color: 'rgba(255, 255, 255, 0.1)' }, ticks: { color: '#e0e0e0' } },
                    x: { grid: { color: 'rgba(255, 255, 255, 0.1)' }, ticks: { color: '#e0e0e0' } }
                }
            }
        });
        
        // Temperature Chart
        const tempCtx = document.getElementById('tempChart').getContext('2d');
        new Chart(tempCtx, {
            type: 'line',
            data: {
                labels: historyData.map((_, i) => i + 1),
                datasets: [{
                    label: 'Soil Temp (°C)',
                    data: historyData.map(d => d.soil),
                    borderColor: '#ff9800',
                    backgroundColor: 'rgba(255, 152, 0, 0.1)',
                    borderWidth: 2,
                    fill: true,
                    tension: 0.4
                }, {
                    label: 'Air Temp (°C)',
                    data: historyData.map(d => d.air),
                    borderColor: '#4caf50',
                    backgroundColor: 'rgba(76, 175, 80, 0.1)',
                    borderWidth: 2,
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: { legend: { labels: { color: '#e0e0e0' } } },
                scales: {
                    y: { beginAtZero: false, grid: { color: 'rgba(255, 255, 255, 0.1)' }, ticks: { color: '#e0e0e0' } },
                    x: { grid: { color: 'rgba(255, 255, 255, 0.1)' }, ticks: { color: '#e0e0e0' } }
                }
            }
        });
        
        // Auto-refresh every 10 seconds
        setTimeout(() => location.reload(), 10000);
    </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleDownload() {
  if (!sdCardAvailable || currentLogFile == "") {
    server.send(404, "text/plain", "SD card not available or no log file");
    return;
  }
  
  File file = SD.open(currentLogFile, FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  
  server.sendHeader("Content-Type", "text/csv");
  server.sendHeader("Content-Disposition", "attachment; filename=" + currentLogFile);
  server.streamFile(file, "text/csv");
  file.close();
}

void handleListFiles() {
  if (!sdCardAvailable) {
    server.send(404, "text/plain", "SD card not available");
    return;
  }
  
  String html = "<html><body><h1>Log Files</h1><ul>";
  File root = SD.open("/");
  File file = root.openNextFile();
  
  while (file) {
    if (!file.isDirectory()) {
      String filename = String(file.name());
      html += "<li><a href='/download?file=" + filename + "'>" + filename + "</a> (" + String(file.size()) + " bytes)</li>";
    }
    file = root.openNextFile();
  }
  
  html += "</ul></body></html>";
  server.send(200, "text/html", html);
}

void handleDeleteFile() {
  if (!sdCardAvailable) {
    server.send(404, "text/plain", "SD card not available");
    return;
  }
  
  if (server.hasArg("file")) {
    String filename = server.arg("file");
    if (SD.remove(filename)) {
      server.send(200, "text/plain", "File deleted: " + filename);
    } else {
      server.send(500, "text/plain", "Failed to delete file");
    }
  } else {
    server.send(400, "text/plain", "No file specified");
  }
}

// Made with Bob
