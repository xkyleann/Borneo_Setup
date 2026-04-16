# 🌿 Borneo Soil Respiration Sensor

A weatherproof, autonomous, field-deployable soil respiration monitoring system for tropical rainforest research.

![Borneo Sensor](https://img.shields.io/badge/Status-Active-success)
![ESP32](https://img.shields.io/badge/Platform-ESP32-blue)
![License](https://img.shields.io/badge/License-MIT-green)

## 📋 Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Web Interface](#web-interface)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)

## 🔬 Overview

The Borneo Soil Respiration Sensor is a specialized environmental monitoring device designed for long-term deployment in tropical rainforest conditions. It measures soil CO₂ efflux, temperature, and humidity to study soil respiration patterns in Borneo's unique ecosystem.

### Key Specifications

- **Dimensions**: Ø 65mm × ~115mm (body only)
- **Weight**: ~300g (without batteries)
- **Rating**: IP65 minimum (weatherproof)
- **Power**: USB-C charging with BMS
- **Connectivity**: ESP32 WiFi Access Point
- **Sensors**: 
  - MQ135 Gas Sensor (CO₂ detection)
  - DS18B20 Temperature Probe
  - BMP280 Pressure/Humidity Sensor
  - AHT20 Temperature/Humidity Sensor

## ✨ Features

- 🌐 **WiFi Access Point** - Creates its own network for field access
- 📊 **Real-time Monitoring** - Live CO₂, temperature, and humidity readings
- 📈 **Data Visualization** - Interactive graphs and charts
- 💾 **Data Logging** - Automatic data storage with timestamps
- 🔋 **Battery Monitoring** - Real-time battery level and charging status
- 🌈 **LED Status Ring** - WS2812B RGB indicators for system status
- 📱 **Responsive Design** - Works on phones, tablets, and computers
- ⚡ **Low Power Mode** - Extended battery life for field deployment

## 🛠 Hardware Requirements

### Main Components

1. **ESP32 Development Board** (ESP32-WROOM-32)
2. **MQ135 Gas Sensor** (Air quality/CO₂)
3. **DS18B20** Temperature Probe (Soil temperature)
4. **BMP280** Barometric Pressure Sensor
5. **AHT20** Temperature/Humidity Sensor
6. **WS2812B LED Ring** (Status indicator)
7. **18650 Battery** with BMS charging circuit
8. **Weatherproof Enclosure** (PETG/ASA 3D printed)

### Optional Components

- Solar panel for extended deployment
- SD card module for offline data logging
- External antenna for better WiFi range

## 💻 Software Requirements

### Prerequisites

- **Python 3.7+** (for PlatformIO)
- **PlatformIO** (for ESP32 development)
- **Git** (optional, for version control)

### Supported Operating Systems

- ✅ macOS
- ✅ Windows 10/11
- ✅ Linux (Ubuntu, Debian, etc.)

## 📦 Installation

### Step 1: Install Python

**macOS/Linux:**
```bash
# Check if Python is installed
python3 --version

# If not installed, download from python.org
```

**Windows:**
Download and install from [python.org](https://www.python.org/downloads/)

### Step 2: Install PlatformIO

```bash
# Install PlatformIO CLI
pip3 install platformio

# Verify installation
platformio --version
```

### Step 3: Clone or Download Project

```bash
# If using Git
git clone https://github.com/yourusername/borneo_sensor.git
cd borneo_sensor

# Or download and extract ZIP file
```

### Step 4: Connect ESP32

1. Connect your ESP32 to your computer via USB cable
2. Note the COM port (Windows) or device path (macOS/Linux)

### Step 5: Compile and Upload

```bash
# Navigate to project directory
cd borneo_sensor

# Compile the project
platformio run

# Upload to ESP32
platformio run --target upload

# Monitor serial output (optional)
platformio device monitor
```

## 🚀 Usage

### First Time Setup

1. **Power on the device** - Press the power switch
2. **Wait for LED ring** - Should show blue/green startup sequence
3. **Connect to WiFi**:
   - Network Name: `SoilSensor_Borneo`
   - Password: (none - open network)
4. **Open web browser** and navigate to: `http://192.168.4.1`

### Normal Operation

The sensor will:
- ✅ Start automatically when powered on
- ✅ Create WiFi access point within 10 seconds
- ✅ Begin collecting sensor data immediately
- ✅ Store data in internal memory
- ✅ Display real-time readings on web interface

### LED Status Indicators

| Color | Status |
|-------|--------|
| 🔵 Blue Pulse | Starting up |
| 🟢 Green Solid | System ready |
| 🟡 Yellow Pulse | Collecting data |
| 🔴 Red Flash | Error/Warning |
| 🟣 Purple | Low battery |
| 🌈 Rainbow | Calibrating sensors |

## 🌐 Web Interface

### Dashboard Features

#### 📊 Real-Time Monitoring
- **CO₂ Concentration** - Live ppm readings with trend graph
- **Soil Temperature** - DS18B20 probe readings
- **Air Temperature** - Ambient temperature from BMP280/AHT20
- **Humidity** - Relative humidity percentage
- **Barometric Pressure** - Atmospheric pressure in hPa

#### 📈 Data Visualization
- **Time-series Graphs** - Last 24 hours of data
- **Statistical Summary** - Min, max, average values
- **Export Data** - Download CSV files for analysis

#### ⚙️ System Information
- Connected clients count
- System uptime
- Battery level and charging status
- WiFi signal strength
- Firmware version
- Memory usage

### Accessing the Interface

1. **Connect to WiFi**: `SoilSensor_Borneo`
2. **Open browser**: Navigate to `http://192.168.4.1`
3. **Auto-refresh**: Page updates every 5 seconds

### Mobile Access

The interface is fully responsive and works on:
- 📱 Smartphones (iOS/Android)
- 📱 Tablets
- 💻 Laptops
- 🖥️ Desktop computers

## 🔧 Configuration

### WiFi Settings

Edit in `src/main.cpp`:
```cpp
const char* AP_SSID     = "SoilSensor_Borneo";  // Change network name
const char* AP_PASSWORD = "";                    // Add password if needed
```

### Sensor Calibration

```cpp
// MQ135 calibration (adjust based on your sensor)
#define MQ135_RZERO 76.63

// Temperature offset (if needed)
#define TEMP_OFFSET 0.0
```

### Data Logging Interval

```cpp
// Change logging interval (milliseconds)
#define LOG_INTERVAL 60000  // 60 seconds
```

## 🐛 Troubleshooting

### Cannot Connect to WiFi

**Problem**: WiFi network not visible

**Solutions**:
- ✅ Check if device is powered on (LED ring should be lit)
- ✅ Wait 10-15 seconds after power on
- ✅ Restart your phone/computer WiFi
- ✅ Move closer to the device (within 10 meters)

### Web Page Not Loading

**Problem**: `http://192.168.4.1` doesn't load

**Solutions**:
- ✅ Verify you're connected to `SoilSensor_Borneo` network
- ✅ Try `http://192.168.4.1` (not https)
- ✅ Clear browser cache
- ✅ Try a different browser
- ✅ Disable mobile data (use WiFi only)

### Upload Failed

**Problem**: Cannot upload code to ESP32

**Solutions**:
- ✅ Check USB cable (use data cable, not charge-only)
- ✅ Install CH340/CP2102 drivers if needed
- ✅ Try different USB port
- ✅ Hold BOOT button during upload
- ✅ Check COM port in Device Manager (Windows)

### Sensor Readings Incorrect

**Problem**: Strange or unrealistic values

**Solutions**:
- ✅ Allow 5-10 minutes warm-up time for MQ135
- ✅ Calibrate sensors in known conditions
- ✅ Check sensor connections
- ✅ Verify sensor power supply (3.3V or 5V)

## 📊 Data Export

### CSV Format

Data is logged in CSV format:
```csv
Timestamp,CO2_ppm,Soil_Temp_C,Air_Temp_C,Humidity_%,Pressure_hPa
2026-04-16 10:30:00,450,24.5,26.2,78.5,1013.2
```

### Download Data

1. Open web interface
2. Navigate to "Data" tab
3. Click "Download CSV"
4. Choose date range
5. Save file

## 🔋 Battery Life

### Expected Runtime

- **Continuous Operation**: 24-48 hours
- **Low Power Mode**: 3-5 days
- **With Solar Panel**: Indefinite (weather dependent)

### Charging

- **Input**: USB-C, 5V 1-2A
- **Charge Time**: 3-4 hours (empty to full)
- **Indicator**: Red LED while charging, Green when full

## 🌧️ Weatherproofing

### IP65 Rating

- ✅ Protected against dust
- ✅ Protected against water jets
- ✅ Suitable for outdoor use
- ⚠️ Not submersible

### Maintenance

- Clean vents monthly
- Check seals before deployment
- Replace desiccant if present
- Inspect cable glands

## 📝 Development

### Project Structure

```
borneo_sensor/
├── src/
│   └── main.cpp          # Main application code
├── platformio.ini        # PlatformIO configuration
├── README.md            # This file
└── .gitignore           # Git ignore rules
```

### Building from Source

```bash
# Clean build
platformio run --target clean

# Build only (no upload)
platformio run

# Build and upload
platformio run --target upload

# Monitor serial output
platformio device monitor
```

### Modifying the Web Interface

The HTML/CSS/JavaScript is embedded in `main.cpp`:
1. Edit the `handleRoot()` function
2. Modify the HTML in the raw string literal
3. Recompile and upload

## 🤝 Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## 📄 License

This project is licensed under the MIT License - see LICENSE file for details.

## 👥 Authors

- **Your Name** - Initial work

## 🙏 Acknowledgments

- Borneo Rainforest Research Team
- ESP32 Community
- PlatformIO Team

## 📞 Support

For issues, questions, or suggestions:
- 📧 Email: support@example.com
- 🐛 Issues: [GitHub Issues](https://github.com/yourusername/borneo_sensor/issues)
- 💬 Discussions: [GitHub Discussions](https://github.com/yourusername/borneo_sensor/discussions)

## 🔗 Links

- [ESP32 Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [PlatformIO Documentation](https://docs.platformio.org/)
- [Sensor Datasheets](./docs/datasheets/)

---

**Made with ❤️ for Borneo Rainforest Research**

*Last Updated: April 2026*
