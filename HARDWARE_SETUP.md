# 🔧 Hardware Setup Guide - Borneo Soil Respiration Sensor

## 📋 Required Components

### Main Components
1. **ESP32 Development Board** (ESP32-WROOM-32)
2. **MQ135 Gas Sensor** - Air quality/CO₂ detection
3. **DS18B20** - Waterproof temperature probe (soil temperature)
4. **BMP280** - Barometric pressure & temperature sensor
5. **AHT20** - Temperature & humidity sensor
6. **DS3231** - Real-time clock (RTC) — I2C address 0x68
7. **MicroSD Card Module** - Data logging
7. **WS2812B LED Ring** (optional) - Status indicator
8. **18650 Battery** with BMS charging circuit
9. **Weatherproof Enclosure** (PETG/ASA 3D printed)

### Additional Components
- Jumper wires
- Breadboard (for testing)
- USB-C cable
- MicroSD card (8GB or larger, formatted as FAT32)
- 4.7kΩ resistor (for DS18B20)
- 10kΩ resistor (for MQ135, if not included)

## 🔌 Pin Connections

### ESP32 Pin Assignments

```
ESP32 Pin    →    Component
─────────────────────────────────────
GPIO 4       →    DS18B20 Data (with 4.7kΩ pullup to 3.3V)
GPIO 34      →    MQ135 Analog Output
GPIO 21      →    SDA (I2C bus 1) - BMP280, AHT20 (address: 0x76, 0x38)
GPIO 22      →    SCL (I2C bus 1) - BMP280, AHT20
GPIO 25      →    SDA (I2C bus 2) - DS3231 (address: 0x68)
GPIO 26      →    SCL (I2C bus 2) - DS3231
GPIO 5       →    SD Card CS (Chip Select)
GPIO 18      →    SD Card SCK (SPI Clock)
GPIO 19      →    SD Card MISO
GPIO 23      →    SD Card MOSI
GPIO 2       →    Built-in LED
GPIO 27      →    WS2812B LED Ring DIN (via 300Ω series resistor)
3.3V         →    Power for sensors
GND          →    Common ground
```

### Detailed Wiring Diagram

#### DS18B20 Temperature Sensor
```
DS18B20          ESP32
─────────────────────────
VDD (Red)    →   3.3V
DATA (Yellow)→   GPIO 4 (with 4.7kΩ resistor to 3.3V)
GND (Black)  →   GND
```

#### MQ135 Gas Sensor
```
MQ135            ESP32
─────────────────────────
VCC          →   5V (required for accurate readings)
GND          →   GND
AOUT         →   GPIO 34 (Analog)
DOUT         →   Not connected
```
Note: Must use 5V — 3.3V will give inaccurate readings. Allow 24–48 hours warm-up before trusting CO₂ values.

#### BMP280 + AHT20 Combo Module
```
Module Pin   →   ESP32
─────────────────────────
SCL          →   GPIO 22
GND          →   GND
SDA          →   GPIO 21
VDD          →   3.3V
```
Note: Single board with both sensors. BMP280 at I2C address 0x76, AHT20 at 0x38.

#### DS3231 Real-Time Clock
```
Module Pin   →   ESP32
─────────────────────────
+ (VCC)      →   3.3V
D (SDA)      →   GPIO 25 (I2C bus 2)
C (SCL)      →   GPIO 26 (I2C bus 2)
NC           →   Not connected
- (GND)      →   GND
```
Note: I2C address 0x68. Uses dedicated I2C bus 2 (separate from BMP280+AHT20).

#### MicroSD Card Module
```
SD Module        ESP32
─────────────────────────
VCC          →   5V or 3.3V (check module)
GND          →   GND
CS           →   GPIO 5
CLK          →   GPIO 18
MISO         →   GPIO 19
MOSI         →   GPIO 23
```

#### WS2812B LED Ring (8 LEDs, optional)
```
WS2812B          ESP32
──────────────────────────────────────────
VCC (5V)     →   5V   (NOT 3.3V)
GND          →   GND
DIN          →   300Ω resistor → GPIO 27
```
Note: Place a 300–500Ω resistor in series on the data line.
Add a 100µF capacitor across VCC/GND near the ring to prevent power spikes.

## 🛠️ Assembly Instructions

### Step 1: Prepare the Breadboard (Testing Phase)

1. Insert ESP32 into breadboard
2. Connect power rails (3.3V and GND)
3. Add all sensors one by one, testing each

### Step 2: Connect BMP280 + AHT20 Combo Module

1. Connect the combo board using pin order: SCL → GND → SDA → VDD
2. BMP280 address: 0x76, AHT20 address: 0x38
3. Use short wires to minimize interference

### Step 3: Connect DS18B20 Temperature Probe

1. Connect VDD to 3.3V
2. Connect GND to ground
3. Connect DATA to GPIO 4
4. **Important:** Add 4.7kΩ pullup resistor between DATA and 3.3V

### Step 4: Connect MQ135 Gas Sensor

1. Connect VCC to 5V (or 3.3V if 5V not available)
2. Connect GND to ground
3. Connect AOUT to GPIO 34
4. **Note:** MQ135 needs 24-48 hours warm-up for accurate readings

### Step 5: Connect SD Card Module

1. Connect VCC to 5V or 3.3V (check module voltage)
2. Connect all SPI pins as shown above
3. Insert formatted microSD card
4. Test with simple write/read before final assembly

### Step 6: Power Supply

1. Connect 18650 battery to BMS module
2. Connect BMS output to ESP32 VIN and GND
3. Connect USB-C for charging
4. Test battery voltage and charging

## 🧪 Testing Procedure

### 1. Test Each Sensor Individually

```bash
# Upload test code and monitor serial output
platformio run --target upload
platformio device monitor
```

### 2. Verify Sensor Readings

Check serial monitor for:
- ✅ DS18B20: Temperature reading (should be ~20-30°C)
- ✅ BMP280: Pressure ~1013 hPa, Temperature ~20-30°C
- ✅ AHT20: Humidity 30-80%, Temperature ~20-30°C
- ✅ MQ135: CO₂ ~400-1000 ppm (after warm-up)
- ✅ SD Card: "SUCCESS" message

### 3. Test WiFi Access Point

1. Look for "SoilSensor_Borneo" network
2. Connect with phone/laptop
3. Open browser to http://192.168.4.1
4. Verify all readings display correctly

### 4. Test Data Logging

1. Let device run for 5 minutes
2. Click "Download Current CSV" on web interface
3. Open CSV file and verify data

## 📦 Final Assembly

### 1. Prepare Enclosure

1. 3D print enclosure (files in `/hardware` folder)
2. Drill holes for:
   - DS18B20 cable gland
   - MQ135 vent (with mesh)
   - USB-C charging port
   - Power switch
   - Antenna exit

### 2. Mount Components

1. Secure ESP32 with standoffs
2. Mount sensors with hot glue or screws
3. Route wires neatly
4. Secure battery with velcro or holder
5. Install SD card

### 3. Weatherproofing

1. Apply silicone sealant around cable glands
2. Install hydrophobic membrane over vents
3. Test seal with water spray (avoid electronics)
4. Add desiccant pack inside enclosure

### 4. Field Deployment

1. Fully charge battery
2. Test all functions
3. Insert DS18B20 probe into soil
4. Mount device on tripod or stake
5. Ensure MQ135 vent faces down (rain protection)

## 🔍 Troubleshooting

### Sensor Not Detected

**DS18B20:**
- Check 4.7kΩ pullup resistor
- Verify wire connections
- Try different GPIO pin

**BMP280/AHT20:**
- Check I2C address (use I2C scanner)
- Verify SDA/SCL connections
- Check power supply (3.3V)

**MQ135:**
- Allow 24-48 hours warm-up
- Check analog pin connection
- Verify power supply

**SD Card:**
- Format as FAT32
- Check SPI connections
- Try different SD card
- Verify CS pin (GPIO 5)

### No WiFi Network

- Check ESP32 power
- Wait 10-15 seconds after boot
- Restart device
- Check antenna connection

### Data Not Logging

- Verify SD card is inserted
- Check SD card format (FAT32)
- Monitor serial output for errors
- Try different SD card

## 📊 Calibration

### MQ135 CO₂ Sensor

1. Place sensor in fresh outdoor air (400 ppm CO₂)
2. Let warm up for 24 hours
3. Note the RZero value from serial monitor
4. Update in code: `#define MQ135_RZERO [your_value]`

### DS18B20 Temperature

1. Compare with calibrated thermometer
2. Add offset in code if needed:
   ```cpp
   #define TEMP_OFFSET 0.0  // Adjust as needed
   ```

### BMP280 Pressure

1. Check local weather station pressure
2. Adjust sea level pressure if needed

## 🔋 Battery Life Optimization

### Power Saving Tips

1. **Reduce WiFi transmit power:**
   ```cpp
   WiFi.setTxPower(WIFI_POWER_11dBm);
   ```

2. **Increase logging interval:**
   ```cpp
   const unsigned long LOG_INTERVAL = 300000;  // 5 minutes
   ```

3. **Use deep sleep between readings:**
   ```cpp
   esp_sleep_enable_timer_wakeup(60 * 1000000);  // 60 seconds
   esp_deep_sleep_start();
   ```

4. **Disable WiFi when not needed:**
   ```cpp
   WiFi.mode(WIFI_OFF);
   ```

### Expected Battery Life

- **Continuous operation:** 24-48 hours
- **With 5-min logging:** 3-5 days
- **With deep sleep:** 1-2 weeks
- **With solar panel:** Indefinite

## 🌧️ Weatherproofing Checklist

- [ ] All cable glands sealed with silicone
- [ ] Vents covered with hydrophobic membrane
- [ ] Enclosure seams sealed
- [ ] Desiccant pack installed
- [ ] IP65 rating verified
- [ ] Water spray test passed
- [ ] MQ135 vent faces downward

## 📝 Maintenance Schedule

### Weekly
- Check battery level
- Verify data logging
- Clean sensor vents

### Monthly
- Download and backup data
- Clean enclosure exterior
- Check seal integrity
- Replace desiccant if needed

### Quarterly
- Recalibrate sensors
- Check battery health
- Update firmware if available
- Inspect all connections

## 🚀 Deployment Checklist

Before deploying to Borneo:

- [ ] All sensors tested and working
- [ ] SD card logging verified
- [ ] Battery fully charged
- [ ] Weatherproofing complete
- [ ] Firmware updated to latest version
- [ ] Backup of all data
- [ ] Spare batteries packed
- [ ] Spare SD cards packed
- [ ] Tools for maintenance
- [ ] Documentation printed

## 📞 Support

For hardware issues:
- Check serial monitor output
- Review wiring diagram
- Test components individually
- Contact: support@example.com

---

**Last Updated:** April 2026  
**Hardware Version:** 2.0  
**Compatible Firmware:** v2.0.0+