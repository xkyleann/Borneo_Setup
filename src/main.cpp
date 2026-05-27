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
void handleHistory();
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
  server.on("/history", handleHistory);
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
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", "");
  server.sendContent(R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Borneo Sensor</title>
<style>
:root{--bg:#0d1117;--sf:#161b22;--sf2:#1c2230;--bd:rgba(240,246,252,.1);--tx:#e6edf3;--mu:#7d8590;
  --co2:#38bdf8;--soil:#fb923c;--air:#4ade80;--hum:#818cf8;--prs:#c084fc;
  --ok:#22c55e;--warn:#facc15;--hi:#f97316;--err:#ef4444;}
*{margin:0;padding:0;box-sizing:border-box}
body{background:var(--bg);color:var(--tx);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;min-height:100vh}
.wrap{max-width:1100px;margin:0 auto;padding:18px 14px}
header{display:flex;align-items:center;justify-content:space-between;padding:16px 0 20px;border-bottom:1px solid var(--bd);margin-bottom:22px;flex-wrap:wrap;gap:10px}
.logo{display:flex;align-items:center;gap:10px}
.logo-ic{width:34px;height:34px;background:linear-gradient(135deg,#38bdf8,#818cf8);border-radius:8px;display:grid;place-items:center;font-size:17px;flex-shrink:0}
.logo-nm{font-size:.95em;font-weight:700;letter-spacing:1.5px;text-transform:uppercase}
.logo-sb{font-size:.7em;color:var(--mu);margin-top:2px}
.hdr-r{display:flex;align-items:center;gap:12px;flex-wrap:wrap}
.live{display:flex;align-items:center;gap:6px;background:rgba(34,197,94,.1);border:1px solid rgba(34,197,94,.25);border-radius:20px;padding:5px 12px;font-size:.72em;font-weight:600;color:var(--ok);text-transform:uppercase;letter-spacing:.8px}
.live-dot{width:6px;height:6px;background:var(--ok);border-radius:50%;animation:glow 2s infinite}
@keyframes glow{0%,100%{box-shadow:0 0 0 0 rgba(34,197,94,.5)}60%{box-shadow:0 0 0 5px transparent}}
.last-up{font-size:.72em;color:var(--mu)}
nav{display:flex;gap:0;margin-bottom:22px;border-bottom:1px solid var(--bd)}
.tab{background:none;border:none;border-bottom:2px solid transparent;color:var(--mu);padding:9px 16px;font-size:.86em;font-weight:500;cursor:pointer;margin-bottom:-1px;transition:color .15s,border-color .15s}
.tab:hover{color:var(--tx)}
.tab.on{color:var(--co2);border-bottom-color:var(--co2)}
.pane{display:none}.pane.on{display:block}
.grid{display:grid;gap:13px}
.g1{grid-template-columns:1fr}
.g2{grid-template-columns:1fr 1fr}
.g4{grid-template-columns:repeat(4,1fr)}
.gchart{grid-template-columns:repeat(2,1fr)}
.card{background:var(--sf);border:1px solid var(--bd);border-radius:11px;padding:18px;position:relative;overflow:hidden;transition:border-color .2s}
.card:hover{border-color:rgba(240,246,252,.2)}
.cap{position:absolute;top:0;left:0;right:0;height:3px;border-radius:11px 11px 0 0}
.clabel{font-size:.67em;font-weight:600;letter-spacing:1.5px;text-transform:uppercase;color:var(--mu);margin-bottom:11px}
.cval{font-size:2.4em;font-weight:700;line-height:1;letter-spacing:-1px;display:flex;align-items:baseline;gap:4px}
.cunit{font-size:.32em;font-weight:400;color:var(--mu);margin-bottom:2px}
.cfoot{display:flex;align-items:center;justify-content:space-between;margin-top:11px;font-size:.77em}
.cstatus{font-weight:500}
.ctrend{font-size:1.1em}
.co2hero{padding:20px 22px}
.co2hero .cval{font-size:3.2em}
.co2row{display:flex;align-items:flex-end;justify-content:space-between;flex-wrap:wrap;gap:16px}
.qbar-wrap{flex:1;min-width:200px}
.qticks{display:flex;justify-content:space-between;font-size:.63em;color:var(--mu);margin-bottom:4px}
.qtrack{height:7px;background:rgba(255,255,255,.07);border-radius:4px;overflow:hidden}
.qfill{height:100%;border-radius:4px;transition:width .6s ease,background .6s ease}
.qlabel{font-size:.8em;font-weight:600;margin-top:8px}
.chart-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}
.chart-ttl{font-size:.7em;font-weight:600;letter-spacing:1.2px;text-transform:uppercase;color:var(--mu)}
.chart-now{font-size:1.25em;font-weight:700}
canvas{display:block;width:100%}
.stbl{width:100%;border-collapse:collapse}
.stbl td{padding:9px 0;border-bottom:1px solid var(--bd);font-size:.85em;vertical-align:middle}
.stbl td:first-child{color:var(--mu);width:52%}
.stbl tr:last-child td{border:none}
.pill{display:inline-flex;align-items:center;gap:4px;padding:2px 9px;border-radius:10px;font-size:.77em;font-weight:600}
.ok{background:rgba(34,197,94,.1);color:var(--ok)}.err{background:rgba(239,68,68,.1);color:var(--err)}
.mono{font-family:'SF Mono',ui-monospace,monospace;font-size:.84em}
.dlrow{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
.btn{padding:9px 20px;border-radius:7px;border:1px solid var(--bd);background:var(--sf2);color:var(--tx);font-size:.84em;font-weight:500;cursor:pointer;text-decoration:none;transition:border-color .15s,color .15s;display:inline-block}
.btn:hover{border-color:var(--co2);color:var(--co2)}
.btn.pri{background:linear-gradient(135deg,#0284c7,#4f46e5);border:none;color:#fff}
.btn.pri:hover{opacity:.88}
.uprow{background:var(--sf);border:1px solid var(--bd);border-radius:11px;padding:15px 18px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:12px;margin-top:13px}
@media(max-width:680px){.g4,.gchart{grid-template-columns:1fr 1fr}.co2row{flex-direction:column}.cval{font-size:2em}.co2hero .cval{font-size:2.4em}.tab{padding:9px 10px;font-size:.8em}}
@media(max-width:400px){.g4,.g2,.gchart{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="wrap">
<header>
  <div class="logo">
    <div class="logo-ic">&#127807;</div>
    <div><div class="logo-nm">Borneo Sensor</div><div class="logo-sb">Soil Respiration Monitor</div></div>
  </div>
  <div class="hdr-r">
    <span class="live"><span class="live-dot"></span>Live</span>
    <span class="last-up" id="last-up">connecting...</span>
  </div>
</header>
<nav>
  <button class="tab on"  data-t="dash"   onclick="T('dash')">Dashboard</button>
  <button class="tab"     data-t="charts" onclick="T('charts')">Charts</button>
  <button class="tab"     data-t="system" onclick="T('system')">System</button>
</nav>
<!-- DASHBOARD -->
<div id="pane-dash" class="pane on">
  <div style="margin-bottom:13px">
    <div class="card co2hero">
      <div class="cap" style="background:linear-gradient(90deg,#38bdf8,#818cf8)"></div>
      <div class="clabel">CO&#8322; Concentration</div>
      <div class="co2row">
        <div>
          <div class="cval"><span id="v-co2" style="color:#38bdf8">--</span><span class="cunit">ppm</span></div>
          <div class="qlabel" id="q-label" style="color:#38bdf8;margin-top:9px">--</div>
        </div>
        <div class="qbar-wrap">
          <div class="qticks"><span>0</span><span>1000</span><span>2000</span><span>5000+</span></div>
          <div class="qtrack"><div class="qfill" id="q-bar" style="width:0;background:#38bdf8"></div></div>
        </div>
      </div>
    </div>
  </div>
  <div class="grid g4">
    <div class="card">
      <div class="cap" style="background:#fb923c"></div>
      <div class="clabel">Soil Temperature</div>
      <div class="cval"><span id="v-soil" style="color:#fb923c">--</span><span class="cunit">&#176;C</span></div>
      <div class="cfoot"><span class="cstatus" id="s-soil">--</span><span class="ctrend" id="t-soil"></span></div>
    </div>
    <div class="card">
      <div class="cap" style="background:#4ade80"></div>
      <div class="clabel">Air Temperature</div>
      <div class="cval"><span id="v-air" style="color:#4ade80">--</span><span class="cunit">&#176;C</span></div>
      <div class="cfoot"><span class="cstatus" id="s-air">--</span><span class="ctrend" id="t-air"></span></div>
    </div>
    <div class="card">
      <div class="cap" style="background:#818cf8"></div>
      <div class="clabel">Humidity</div>
      <div class="cval"><span id="v-hum" style="color:#818cf8">--</span><span class="cunit">%</span></div>
      <div class="cfoot"><span class="cstatus" id="s-hum">--</span><span class="ctrend" id="t-hum"></span></div>
    </div>
    <div class="card">
      <div class="cap" style="background:#c084fc"></div>
      <div class="clabel">Pressure</div>
      <div class="cval"><span id="v-prs" style="color:#c084fc">--</span><span class="cunit">hPa</span></div>
      <div class="cfoot"><span>Atmospheric</span><span class="ctrend" id="t-prs"></span></div>
    </div>
  </div>
  <div class="uprow">
    <div>
      <div class="clabel" style="margin-bottom:4px">System Uptime</div>
      <div style="font-size:1.55em;font-weight:700;font-family:'SF Mono',monospace" id="v-uptime">--:--:--</div>
    </div>
    <div style="text-align:right">
      <div class="clabel" style="margin-bottom:4px">Timestamp</div>
      <div class="mono" id="v-dt" style="color:var(--tx)">--</div>
    </div>
  </div>
</div>
<!-- CHARTS -->
<div id="pane-charts" class="pane">
  <div class="grid gchart">
    <div class="card">
      <div class="chart-hdr">
        <span class="chart-ttl">CO&#8322; Concentration</span>
        <span class="chart-now" style="color:#38bdf8"><span id="cn-co2">--</span><small style="font-size:.5em;color:var(--mu);margin-left:3px">ppm</small></span>
      </div>
      <canvas id="cCO2" height="170"></canvas>
    </div>
    <div class="card">
      <div class="chart-hdr">
        <span class="chart-ttl">Temperature</span>
        <span class="chart-now" style="font-size:1em">
          <span style="color:#fb923c" id="cn-soil">--</span>&#176;&thinsp;
          <span style="color:#4ade80" id="cn-air">--</span>&#176;
        </span>
      </div>
      <canvas id="cTemp" height="170"></canvas>
    </div>
    <div class="card">
      <div class="chart-hdr">
        <span class="chart-ttl">Relative Humidity</span>
        <span class="chart-now" style="color:#818cf8"><span id="cn-hum">--</span><small style="font-size:.5em;color:var(--mu);margin-left:3px">%</small></span>
      </div>
      <canvas id="cHum" height="170"></canvas>
    </div>
    <div class="card">
      <div class="chart-hdr">
        <span class="chart-ttl">Barometric Pressure</span>
        <span class="chart-now" style="color:#c084fc"><span id="cn-prs">--</span><small style="font-size:.5em;color:var(--mu);margin-left:3px">hPa</small></span>
      </div>
      <canvas id="cPrs" height="170"></canvas>
    </div>
  </div>
</div>
<!-- SYSTEM -->
<div id="pane-system" class="pane">
  <div class="grid g2">
    <div class="card">
      <div class="cap" style="background:linear-gradient(90deg,#4ade80,#38bdf8)"></div>
      <div class="clabel" style="margin-bottom:14px">Sensor Status</div>
      <table class="stbl">
        <tr><td>DS18B20 Soil Probe</td><td><span class="pill ok">&#9679; Online</span></td></tr>
        <tr><td>BMP280 Pressure</td><td><span class="pill" id="p-bmp">--</span></td></tr>
        <tr><td>AHT20 Humidity</td><td><span class="pill" id="p-aht">--</span></td></tr>
        <tr><td>MQ135 CO&#8322;</td><td><span class="pill ok">&#9679; Online</span></td></tr>
        <tr><td>DS3231 RTC</td><td><span class="pill ok">&#9679; Online</span></td></tr>
        <tr><td>SD Card</td><td><span class="pill" id="p-sd">--</span></td></tr>
      </table>
    </div>
    <div class="card">
      <div class="cap" style="background:linear-gradient(90deg,#818cf8,#c084fc)"></div>
      <div class="clabel" style="margin-bottom:14px">Device Information</div>
      <table class="stbl">
        <tr><td>Access Point</td><td class="mono">SoilSensor_Borneo</td></tr>
        <tr><td>IP Address</td><td class="mono">192.168.4.1</td></tr>
        <tr><td>MAC Address</td><td class="mono" id="si-mac">--</td></tr>
        <tr><td>Firmware</td><td class="mono">v2.1.0</td></tr>
        <tr><td>Connected Clients</td><td class="mono" id="si-cli">--</td></tr>
        <tr><td>RAM Readings</td><td class="mono" id="si-rdg">--</td></tr>
        <tr><td>Log File</td><td class="mono" id="si-log" style="font-size:.8em">--</td></tr>
      </table>
    </div>
  </div>
  <div class="card" style="margin-top:13px">
    <div class="cap" style="background:linear-gradient(90deg,#fb923c,#ef4444)"></div>
    <div class="clabel" style="margin-bottom:6px">Data Export</div>
    <p style="color:var(--mu);font-size:.83em">
      SD Card: <strong id="si-sd" style="color:var(--ok)">--</strong>
      &nbsp;&#183;&nbsp; Log: <strong class="mono" id="si-log2">--</strong>
    </p>
    <div class="dlrow">
      <a href="/download" class="btn pri">&#8595; Download CSV</a>
      <a href="/list" class="btn">List Files</a>
      <a href="/data" class="btn">Raw JSON</a>
    </div>
  </div>
</div>
</div>
<script>
function T(n){
  document.querySelectorAll('.pane').forEach(function(e){e.classList.remove('on');});
  document.querySelectorAll('.tab').forEach(function(e){e.classList.remove('on');});
  document.getElementById('pane-'+n).classList.add('on');
  document.querySelector('[data-t="'+n+'"]').classList.add('on');
  if(n==='charts')setTimeout(redrawAll,40);
}
function pad(n){return String(n).padStart(2,'0');}
function fmtUp(s){
  var d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),m=Math.floor((s%3600)/60),ss=s%60;
  if(d>0)return d+'d '+pad(h)+'h '+pad(m)+'m';
  return pad(h)+':'+pad(m)+':'+pad(ss);
}
function co2q(v){
  if(v<400)  return{label:'Excellent',color:'#22c55e',pct:v/5000*100};
  if(v<1000) return{label:'Good',     color:'#38bdf8',pct:v/5000*100};
  if(v<2000) return{label:'Elevated', color:'#facc15',pct:v/5000*100};
  if(v<5000) return{label:'High',     color:'#f97316',pct:v/5000*100};
  return{label:'Dangerous',color:'#ef4444',pct:100};
}
var prev={};
function trendOf(k,v){
  var p=prev[k]; prev[k]=v;
  if(p===undefined)return '';
  var d=v-p;
  if(Math.abs(d)<0.15)return '<span style="color:#475569">&#8594;</span>';
  return d>0?'<span style="color:#f97316">&#8593;</span>':'<span style="color:#22c55e">&#8595;</span>';
}
var disp={};
function countTo(id,target,dp){
  var el=document.getElementById(id); if(!el)return;
  var from=disp[id]!==undefined?disp[id]:target;
  var diff=target-from,steps=14,i=0;
  var iv=setInterval(function(){
    i++; el.textContent=(from+diff*(i/steps)).toFixed(dp);
    if(i>=steps){disp[id]=target;clearInterval(iv);}
  },25);
  disp[id]=target;
}
var A={co2:[],soil:[],air:[],hum:[],prs:[]};
var lastDt='';
fetch('/history').then(function(r){return r.json();}).then(function(h){
  if(h.co2) A.co2=h.co2; if(h.soil)A.soil=h.soil; if(h.air)A.air=h.air;
  if(h.hum) A.hum=h.hum; if(h.prs) A.prs=h.prs;
  redrawAll();
}).catch(function(){});
function poll(){
  fetch('/data').then(function(r){return r.json();}).then(function(d){
    countTo('v-co2', d.co2,      1);
    countTo('v-soil',d.soil_temp,1);
    countTo('v-air', d.air_temp, 1);
    countTo('v-hum', d.humidity, 1);
    countTo('v-prs', d.pressure, 1);
    document.getElementById('v-uptime').textContent=fmtUp(d.uptime);
    document.getElementById('v-dt').textContent=d.datetime;
    document.getElementById('last-up').textContent='Updated '+new Date().toLocaleTimeString();
    var q=co2q(d.co2);
    var bar=document.getElementById('q-bar');
    bar.style.width=Math.min(q.pct,100)+'%'; bar.style.background=q.color;
    var ql=document.getElementById('q-label'); ql.textContent=q.label; ql.style.color=q.color;
    document.getElementById('v-co2').style.color=q.color;
    document.getElementById('t-soil').innerHTML=trendOf('soil',d.soil_temp);
    document.getElementById('t-air').innerHTML =trendOf('air', d.air_temp);
    document.getElementById('t-hum').innerHTML =trendOf('hum', d.humidity);
    document.getElementById('t-prs').innerHTML =trendOf('prs', d.pressure);
    var sl=document.getElementById('s-soil');
    var sok=d.soil_temp>-10&&d.soil_temp<80;
    sl.textContent=sok?'Sensor active':'Check probe'; sl.style.color=sok?'#22c55e':'#ef4444';
    document.getElementById('s-hum').textContent=d.humidity<30?'Dry':d.humidity<70?'Normal':'Humid';
    document.getElementById('s-air').textContent=d.air_temp<15?'Cool':d.air_temp<30?'Nominal':'Warm';
    if(d.bmp_ok!==undefined){
      var b=document.getElementById('p-bmp');
      b.textContent=d.bmp_ok?'&#9679; Online':'&#9679; Offline'; b.className='pill '+(d.bmp_ok?'ok':'err');
      var a=document.getElementById('p-aht');
      a.textContent=d.aht_ok?'&#9679; Online':'&#9679; Offline'; a.className='pill '+(d.aht_ok?'ok':'err');
    }
    if(d.sd_ok!==undefined){
      var s=document.getElementById('p-sd');
      s.textContent=d.sd_ok?'&#9679; Online':'&#9679; Offline'; s.className='pill '+(d.sd_ok?'ok':'err');
      var sd2=document.getElementById('si-sd');
      sd2.textContent=d.sd_ok?'Available':'Unavailable'; sd2.style.color=d.sd_ok?'#22c55e':'#ef4444';
    }
    if(d.mac)    document.getElementById('si-mac').textContent=d.mac;
    if(d.logfile){
      document.getElementById('si-log').textContent=d.logfile||'--';
      document.getElementById('si-log2').textContent=d.logfile||'--';
    }
    if(d.clients!==undefined) document.getElementById('si-cli').textContent=d.clients;
    if(d.readings!==undefined)document.getElementById('si-rdg').textContent=d.readings+' / 100';
    if(d.datetime!==lastDt){
      lastDt=d.datetime;
      function push(arr,v){arr.push(v);if(arr.length>80)arr.shift();}
      push(A.co2,d.co2); push(A.soil,d.soil_temp); push(A.air,d.air_temp);
      push(A.hum,d.humidity); push(A.prs,d.pressure);
      document.getElementById('cn-co2').textContent=d.co2.toFixed(1);
      document.getElementById('cn-soil').textContent=d.soil_temp.toFixed(1);
      document.getElementById('cn-air').textContent=d.air_temp.toFixed(1);
      document.getElementById('cn-hum').textContent=d.humidity.toFixed(1);
      document.getElementById('cn-prs').textContent=d.pressure.toFixed(1);
      redrawAll();
    }
  }).catch(function(){});
}
poll(); setInterval(poll,3000);
function drawChart(id,datasets,colors,labels){
  var cv=document.getElementById(id); if(!cv)return;
  var dpr=window.devicePixelRatio||1;
  var cw=cv.parentElement.clientWidth-36,ch=170;
  cv.width=cw*dpr; cv.height=ch*dpr;
  cv.style.width=cw+'px'; cv.style.height=ch+'px';
  var ctx=cv.getContext('2d'); ctx.scale(dpr,dpr);
  var W=cw,H=ch,p={l:50,r:14,t:10,b:26};
  ctx.clearRect(0,0,W,H);
  var all=[];
  datasets.forEach(function(ds){ds.forEach(function(v){if(!isNaN(v))all.push(v);});});
  if(all.length<2){
    ctx.fillStyle='#475569'; ctx.font='13px system-ui'; ctx.textAlign='center';
    ctx.fillText('Collecting data...',W/2,H/2); return;
  }
  var mn=Math.min.apply(null,all),mx=Math.max.apply(null,all);
  var span=mx-mn; mn-=span*0.08||0.5; mx+=span*0.08||0.5;
  if(mn===mx){mn-=1;mx+=1;}
  function tx(i,n){return p.l+(i/Math.max(n-1,1))*(W-p.l-p.r);}
  function ty(v){return p.t+(1-(v-mn)/(mx-mn))*(H-p.t-p.b);}
  ctx.strokeStyle='rgba(240,246,252,0.06)'; ctx.lineWidth=1;
  for(var r=0;r<=4;r++){
    var y=p.t+r*(H-p.t-p.b)/4;
    ctx.beginPath(); ctx.moveTo(p.l,y); ctx.lineTo(W-p.r,y); ctx.stroke();
  }
  ctx.fillStyle='#4b5563'; ctx.font='10px system-ui'; ctx.textAlign='right';
  for(var r=0;r<=4;r++){
    var v=mx-r*(mx-mn)/4,y=p.t+r*(H-p.t-p.b)/4;
    ctx.fillText(v>999?v.toFixed(0):(v>99?v.toFixed(0):v.toFixed(1)),p.l-5,y+3.5);
  }
  datasets.forEach(function(data,di){
    var n=data.length; if(n<2)return;
    var col=colors[di];
    var grd=ctx.createLinearGradient(0,p.t,0,H-p.b);
    grd.addColorStop(0,col+'45'); grd.addColorStop(1,col+'00');
    ctx.beginPath();
    ctx.moveTo(tx(0,n),H-p.b); ctx.lineTo(tx(0,n),ty(data[0]));
    for(var i=1;i<n;i++){
      var x0=tx(i-1,n),y0=ty(data[i-1]),x1=tx(i,n),y1=ty(data[i]),cx=(x0+x1)/2;
      ctx.bezierCurveTo(cx,y0,cx,y1,x1,y1);
    }
    ctx.lineTo(tx(n-1,n),H-p.b); ctx.closePath();
    ctx.fillStyle=grd; ctx.fill();
    ctx.beginPath(); ctx.moveTo(tx(0,n),ty(data[0]));
    for(var i=1;i<n;i++){
      var x0=tx(i-1,n),y0=ty(data[i-1]),x1=tx(i,n),y1=ty(data[i]),cx=(x0+x1)/2;
      ctx.bezierCurveTo(cx,y0,cx,y1,x1,y1);
    }
    ctx.strokeStyle=col; ctx.lineWidth=2; ctx.lineJoin='round'; ctx.stroke();
    var lx=tx(n-1,n),ly=ty(data[n-1]);
    ctx.beginPath(); ctx.arc(lx,ly,3.5,0,Math.PI*2); ctx.fillStyle=col; ctx.fill();
    var legX=p.l+di*130;
    ctx.fillStyle=col; ctx.beginPath(); ctx.arc(legX+5,H-8,3.5,0,Math.PI*2); ctx.fill();
    ctx.fillStyle='#6b7280'; ctx.textAlign='left'; ctx.font='10px system-ui';
    ctx.fillText(labels[di],legX+13,H-4);
  });
}
function redrawAll(){
  drawChart('cCO2', [A.co2],         ['#38bdf8'],           ['CO2 ppm']);
  drawChart('cTemp',[A.soil,A.air],  ['#fb923c','#4ade80'], ['Soil C','Air C']);
  drawChart('cHum', [A.hum],         ['#818cf8'],           ['Humidity %']);
  drawChart('cPrs', [A.prs],         ['#c084fc'],           ['Pressure hPa']);
}
redrawAll();
window.addEventListener('resize',redrawAll);
</script>
</body>
</html>
)rawliteral");
  server.sendContent("");
}

void handleData() {
  float co2      = isnan(currentData.co2_ppm)  ? 0.0f : currentData.co2_ppm;
  float soil     = isnan(currentData.soil_temp) ? 0.0f : currentData.soil_temp;
  float air      = isnan(currentData.air_temp)  ? 0.0f : currentData.air_temp;
  float humidity = isnan(currentData.humidity)  ? 0.0f : currentData.humidity;
  float pressure = isnan(currentData.pressure)  ? 0.0f : currentData.pressure;
  char json[420];
  snprintf(json, sizeof(json),
    "{\"co2\":%.1f,\"soil_temp\":%.1f,\"air_temp\":%.1f,\"humidity\":%.1f,"
    "\"pressure\":%.1f,\"uptime\":%lu,\"datetime\":\"%s\","
    "\"bmp_ok\":%s,\"aht_ok\":%s,\"sd_ok\":%s,\"clients\":%d,\"readings\":%d,"
    "\"mac\":\"%s\",\"logfile\":\"%s\"}",
    co2, soil, air, humidity, pressure, millis() / 1000, currentData.datetime,
    bmpAvailable    ? "true" : "false",
    ahtAvailable    ? "true" : "false",
    sdCardAvailable ? "true" : "false",
    WiFi.softAPgetStationNum(), historyCount,
    WiFi.softAPmacAddress().c_str(), currentLogFile.c_str());
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "application/json", json);
}

void handleHistory() {
  String json = "{\"co2\":[";
  for (int i = 0; i < historyCount; i++) { if (i) json += ','; json += String(dataHistory[i].co2_ppm,  1); }
  json += "],\"soil\":[";
  for (int i = 0; i < historyCount; i++) { if (i) json += ','; json += String(dataHistory[i].soil_temp, 1); }
  json += "],\"air\":[";
  for (int i = 0; i < historyCount; i++) { if (i) json += ','; json += String(dataHistory[i].air_temp,  1); }
  json += "],\"hum\":[";
  for (int i = 0; i < historyCount; i++) { if (i) json += ','; json += String(dataHistory[i].humidity,  1); }
  json += "],\"prs\":[";
  for (int i = 0; i < historyCount; i++) { if (i) json += ','; json += String(dataHistory[i].pressure,  1); }
  json += "]}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
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
