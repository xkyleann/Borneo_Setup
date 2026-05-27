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
#include <RTClib.h>
#include <Adafruit_NeoPixel.h>

// WiFi Configuration
const char* AP_SSID     = "SoilSensor_Borneo";
const char* AP_PASSWORD = "";

// Pin Definitions
#define ONE_WIRE_BUS 4        // DS18B20 Temperature Sensor
#define MQ135_PIN 34          // MQ135 Gas Sensor (Analog)
#define SD_CS_PIN 5           // SD Card CS Pin
#define LED_PIN 2             // Built-in LED
#define RTC_SDA_PIN 25        // DS3231 RTC (second I2C bus)
#define RTC_SCL_PIN 26        // DS3231 RTC (second I2C bus)
#define NEOPIXEL_PIN 27       // WS2812B DIN
#define NEOPIXEL_COUNT 8

// Sensor Objects
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
Adafruit_BMP280 bmp;
Adafruit_AHTX0 aht;
MQ135 mq135(MQ135_PIN);
RTC_DS3231 rtc;

WebServer server(80);
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Sensor Data
struct SensorData {
  float co2_ppm;
  float soil_temp;
  float air_temp;
  float humidity;
  float pressure;
  unsigned long timestamp;
  char datetime[20];  // "YYYY-MM-DD HH:MM:SS"
  bool valid;
};

SensorData currentData;
SensorData dataHistory[100];  // Store last 100 readings
int historyIndex = 0;
int historyCount = 0;

// SD Card Status
bool sdCardAvailable = false;
String currentLogFile = "";

// Sensor availability flags
bool bmpAvailable = false;
bool ahtAvailable = false;

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
void setStatus(uint32_t color);
void statusBoot();
void statusFromReadings();
void updateNeopixel();
void handleRoot();
void handleData();
void handleDownload();
void handleListFiles();
void handleDeleteFile();

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pixels.begin();
  pixels.setBrightness(80);
  statusBoot();
  
  Serial.println("\n\n=================================");
  Serial.println("Borneo Soil Respiration Sensor");
  Serial.println("=================================\n");
  
  // Initialize I2C buses
  Wire.begin();                          // BMP280 + AHT20 on GPIO 21/22
  Wire1.begin(RTC_SDA_PIN, RTC_SCL_PIN); // DS3231 on GPIO 25/26
  
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
  server.on("/data", handleData);
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
  updateNeopixel();

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
  ds18b20.setResolution(10);  // 10-bit: ~188ms conversion vs 750ms at 12-bit
  if (ds18b20.getDeviceCount() > 0) {
    Serial.println("OK");
  } else {
    Serial.println("NOT FOUND");
  }
  
  // BMP280 Pressure/Temperature Sensor (try 0x76 then 0x77)
  Serial.print("  BMP280 (Pressure)... ");
  if (bmp.begin(0x76)) {
    bmpAvailable = true;
  } else if (bmp.begin(0x77)) {
    bmpAvailable = true;
    Serial.print("(addr 0x77) ");
  }
  if (bmpAvailable) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
    Serial.println("OK");
  } else {
    Serial.println("NOT FOUND - check wiring and I2C address");
  }

  // AHT20 Temperature/Humidity Sensor
  Serial.print("  AHT20 (Humidity)... ");
  if (aht.begin()) {
    ahtAvailable = true;
    Serial.println("OK");
  } else {
    Serial.println("NOT FOUND");
  }
  
  // MQ135 Gas Sensor
  Serial.print("  MQ135 (CO2)... ");
  Serial.println("OK (warming up...)");

  // DS3231 RTC
  Serial.print("  DS3231 (RTC)... ");
  if (rtc.begin(&Wire1)) {
    if (rtc.lostPower()) {
      Serial.println("LOST POWER - set time to compile time");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    } else {
      Serial.println("OK");
    }
  } else {
    Serial.println("NOT FOUND");
  }

  Serial.println("Sensor initialization complete.\n");
}

void readSensors() {
  currentData.timestamp = millis();
  currentData.valid = true;
  DateTime now = rtc.now();
  snprintf(currentData.datetime, sizeof(currentData.datetime), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  
  // Read DS18B20 Soil Temperature
  ds18b20.requestTemperatures();
  currentData.soil_temp = ds18b20.getTempCByIndex(0);
  if (currentData.soil_temp == DEVICE_DISCONNECTED_C) {
    currentData.soil_temp = 0.0;
  }
  
  // Read BMP280 Pressure and Temperature
  float bmp_temp = NAN;
  if (bmpAvailable) {
    currentData.pressure = bmp.readPressure() / 100.0; // Convert to hPa
    bmp_temp = bmp.readTemperature();
  }

  // Read AHT20 Temperature and Humidity
  if (ahtAvailable) {
    sensors_event_t humidity_event, temp_event;
    aht.getEvent(&humidity_event, &temp_event);
    currentData.humidity = humidity_event.relative_humidity;
    currentData.air_temp = temp_event.temperature;
  }

  // If AHT20 failed or unavailable, fall back to BMP280 temperature
  if (isnan(currentData.air_temp) && !isnan(bmp_temp)) {
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
  
  statusFromReadings();

  // Print to Serial
  Serial.printf("CO2: %.1f ppm | Soil: %.1f°C | Air: %.1f°C | Humidity: %.1f%% | Pressure: %.1f hPa\n",
                currentData.co2_ppm, currentData.soil_temp, currentData.air_temp,
                currentData.humidity, currentData.pressure);
}

void createNewLogFile() {
  if (!sdCardAvailable) return;

  DateTime now = rtc.now();
  char filename[30];
  snprintf(filename, sizeof(filename), "/data_%04d%02d%02d_%02d%02d%02d.csv",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  currentLogFile = String(filename);
  
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
  return "Datetime,Uptime_sec,CO2_ppm,Soil_Temp_C,Air_Temp_C,Humidity_%,Pressure_hPa";
}

String dataToCSV(SensorData data) {
  char buffer[200];
  unsigned long uptime = data.timestamp / 1000;
  snprintf(buffer, sizeof(buffer), "%s,%lu,%.2f,%.2f,%.2f,%.2f,%.2f",
           data.datetime, uptime, data.co2_ppm, data.soil_temp,
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

enum NeoState { NEO_BOOT, NEO_NORMAL, NEO_ELEVATED, NEO_HIGH };
NeoState neoState = NEO_BOOT;

void setStatus(uint32_t color) {
  for (int i = 0; i < NEOPIXEL_COUNT; i++) pixels.setPixelColor(i, color);
  pixels.show();
}

void statusBoot() {
  for (int i = 0; i < NEOPIXEL_COUNT * 2; i++) {
    pixels.clear();
    pixels.setPixelColor(i % NEOPIXEL_COUNT, pixels.Color(255, 180, 0));
    pixels.setPixelColor((i + 1) % NEOPIXEL_COUNT, pixels.Color(120, 80, 0));
    pixels.show();
    delay(50);
  }
  pixels.clear();
  pixels.show();
  neoState = NEO_NORMAL;
}

void updateNeopixel() {
  static unsigned long lastUpdate = 0;
  unsigned long t = millis();
  if (t - lastUpdate < 20) return;
  lastUpdate = t;
  uint8_t b;
  uint32_t color;
  switch (neoState) {
    case NEO_NORMAL:
      b = (uint8_t)(60 + 60 * sin(TWO_PI * t / 3000.0));
      color = pixels.Color(0, b, b / 5);
      break;
    case NEO_ELEVATED:
      b = (uint8_t)(80 + 80 * sin(TWO_PI * t / 1200.0));
      color = pixels.Color(b, b / 3, 0);
      break;
    case NEO_HIGH:
      b = (t % 300 < 150) ? 255 : 15;
      color = pixels.Color(b, 0, 0);
      break;
    default:
      return;
  }
  setStatus(color);
}

void statusFromReadings() {
  if      (currentData.co2_ppm > 2000) neoState = NEO_HIGH;
  else if (currentData.co2_ppm > 600)  neoState = NEO_ELEVATED;
  else                                  neoState = NEO_NORMAL;
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Borneo Soil Respiration Sensor</title>
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
        }
        .chart-container canvas { display: block; width: 100%; }
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
                <div class="sensor-reading"><span id="val-co2">)rawliteral" + String(currentData.co2_ppm, 1) + R"rawliteral(</span><span class="sensor-unit">ppm</span></div>
                <div class="sensor-status">● )rawliteral" + String(currentData.co2_ppm < 1000 ? "Normal" : "Elevated") + R"rawliteral(</div>
            </div>
            
            <div class="sensor-card">
                <div class="sensor-icon">🌡️</div>
                <div class="sensor-label">Soil Temperature</div>
                <div class="sensor-reading"><span id="val-soil">)rawliteral" + String(currentData.soil_temp, 1) + R"rawliteral(</span><span class="sensor-unit">°C</span></div>
                <div class="sensor-status">● Optimal</div>
            </div>
            
            <div class="sensor-card">
                <div class="sensor-icon">🌤️</div>
                <div class="sensor-label">Air Temperature</div>
                <div class="sensor-reading"><span id="val-air">)rawliteral" + String(currentData.air_temp, 1) + R"rawliteral(</span><span class="sensor-unit">°C</span></div>
                <div class="sensor-status">● Stable</div>
            </div>
            
            <div class="sensor-card">
                <div class="sensor-icon">💧</div>
                <div class="sensor-label">Humidity</div>
                <div class="sensor-reading"><span id="val-humidity">)rawliteral" + String(currentData.humidity, 1) + R"rawliteral(</span><span class="sensor-unit">%</span></div>
                <div class="sensor-status">● Good</div>
            </div>
            
            <div class="sensor-card">
                <div class="sensor-icon">🌍</div>
                <div class="sensor-label">Pressure</div>
                <div class="sensor-reading"><span id="val-pressure">)rawliteral" + String(currentData.pressure, 1) + R"rawliteral(</span><span class="sensor-unit">hPa</span></div>
                <div class="sensor-status">● Normal</div>
            </div>
            
            <div class="sensor-card">
                <div class="sensor-icon">⏱️</div>
                <div class="sensor-label">System Uptime</div>
                <div class="sensor-reading"><span id="val-uptime">)rawliteral" + String(millis() / 1000) + R"rawliteral(</span><span class="sensor-unit">sec</span></div>
                <div class="sensor-status">● <span id="val-datetime">)rawliteral" + String(currentData.datetime) + R"rawliteral(</span></div>
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
        const co2Data  = )rawliteral";

  html += "[";
  for (int i = 0; i < historyCount; i++) { if (i > 0) html += ","; html += String(dataHistory[i].co2_ppm, 1); }
  html += "]";
  html += R"rawliteral(;
        const soilData = )rawliteral";
  html += "[";
  for (int i = 0; i < historyCount; i++) { if (i > 0) html += ","; html += String(dataHistory[i].soil_temp, 1); }
  html += "]";
  html += R"rawliteral(;
        const airData  = )rawliteral";
  html += "[";
  for (int i = 0; i < historyCount; i++) { if (i > 0) html += ","; html += String(dataHistory[i].air_temp, 1); }
  html += "]";

  html += R"rawliteral(;

        function drawChart(id, datasets, colors, labels) {
            const canvas = document.getElementById(id);
            if (!canvas) return;
            canvas.width  = canvas.parentElement.clientWidth - 50;
            canvas.height = 260;
            const ctx = canvas.getContext('2d');
            const W = canvas.width, H = canvas.height;
            const pad = {l:55, r:15, t:15, b:35};
            ctx.clearRect(0, 0, W, H);
            let allVals = datasets.flat();
            if (allVals.length === 0) { ctx.fillStyle='#607d8b'; ctx.font='14px sans-serif'; ctx.fillText('Waiting for data...', W/2-60, H/2); return; }
            let mn = Math.min(...allVals), mx = Math.max(...allVals);
            const span = mx - mn || 1;
            mn -= span * 0.05; mx += span * 0.05;
            const toX = (i, len) => pad.l + (i / Math.max(len-1,1)) * (W - pad.l - pad.r);
            const toY = v => pad.t + (1 - (v - mn) / (mx - mn)) * (H - pad.t - pad.b);
            // grid
            ctx.strokeStyle = 'rgba(255,255,255,0.08)'; ctx.lineWidth = 1;
            for (let r = 0; r <= 4; r++) { const y = pad.t + r*(H-pad.t-pad.b)/4; ctx.beginPath(); ctx.moveTo(pad.l,y); ctx.lineTo(W-pad.r,y); ctx.stroke(); }
            // y-axis labels
            ctx.fillStyle='#90caf9'; ctx.font='11px sans-serif'; ctx.textAlign='right';
            for (let r = 0; r <= 4; r++) { const v = mx - r*(mx-mn)/4; const y = pad.t + r*(H-pad.t-pad.b)/4; ctx.fillText(v.toFixed(1), pad.l-5, y+4); }
            ctx.textAlign='left';
            // datasets
            datasets.forEach((data, di) => {
                if (data.length < 1) return;
                const hex = colors[di];
                ctx.beginPath(); ctx.moveTo(toX(0,data.length), H-pad.b);
                data.forEach((v,i) => ctx.lineTo(toX(i,data.length), toY(v)));
                ctx.lineTo(toX(data.length-1,data.length), H-pad.b); ctx.closePath();
                ctx.fillStyle = hex + '28'; ctx.fill();
                ctx.beginPath();
                data.forEach((v,i) => i===0 ? ctx.moveTo(toX(i,data.length),toY(v)) : ctx.lineTo(toX(i,data.length),toY(v)));
                ctx.strokeStyle = hex; ctx.lineWidth = 2; ctx.stroke();
                // legend dot + label
                const lx = pad.l + di * 160;
                ctx.fillStyle = hex; ctx.beginPath(); ctx.arc(lx+6, H-10, 5, 0, Math.PI*2); ctx.fill();
                ctx.fillStyle = '#e0e0e0'; ctx.font = '12px sans-serif';
                ctx.fillText(labels[di] + ': ' + (data[data.length-1]||0).toFixed(1), lx+15, H-6);
            });
        }

        function redrawAll() {
            drawChart('co2Chart',  [co2Data],          ['#4fc3f7'],           ['CO₂ (ppm)']);
            drawChart('tempChart', [soilData, airData], ['#ff9800', '#4caf50'], ['Soil °C', 'Air °C']);
        }
        redrawAll();
        window.addEventListener('resize', redrawAll);

        let lastDatetime = '';
        function updateReadings() {
            fetch('/data')
                .then(r => r.json())
                .then(d => {
                    document.getElementById('val-co2').textContent      = d.co2.toFixed(1);
                    document.getElementById('val-soil').textContent     = d.soil_temp.toFixed(1);
                    document.getElementById('val-air').textContent      = d.air_temp.toFixed(1);
                    document.getElementById('val-humidity').textContent = d.humidity.toFixed(1);
                    document.getElementById('val-pressure').textContent = d.pressure.toFixed(1);
                    document.getElementById('val-uptime').textContent   = d.uptime;
                    document.getElementById('val-datetime').textContent = d.datetime;
                    if (d.datetime !== lastDatetime) {
                        lastDatetime = d.datetime;
                        co2Data.push(d.co2);       if (co2Data.length  > 100) co2Data.shift();
                        soilData.push(d.soil_temp); if (soilData.length > 100) soilData.shift();
                        airData.push(d.air_temp);   if (airData.length  > 100) airData.shift();
                        redrawAll();
                    }
                })
                .catch(() => {});
        }
        setInterval(updateReadings, 2000);
    </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleData() {
  // Guard every float against NaN — snprintf writes "nan" for NaN which is invalid JSON
  float co2      = isnan(currentData.co2_ppm)   ? 0.0f : currentData.co2_ppm;
  float soil     = isnan(currentData.soil_temp)  ? 0.0f : currentData.soil_temp;
  float air      = isnan(currentData.air_temp)   ? 0.0f : currentData.air_temp;
  float humidity = isnan(currentData.humidity)   ? 0.0f : currentData.humidity;
  float pressure = isnan(currentData.pressure)   ? 0.0f : currentData.pressure;
  char json[200];
  snprintf(json, sizeof(json),
    "{\"co2\":%.1f,\"soil_temp\":%.1f,\"air_temp\":%.1f,\"humidity\":%.1f,\"pressure\":%.1f,\"uptime\":%lu,\"datetime\":\"%s\"}",
    co2, soil, air, humidity, pressure, millis() / 1000, currentData.datetime);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "application/json", json);
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
