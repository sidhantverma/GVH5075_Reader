# BLE Data Parsing Analysis: Python vs ESP32 C++

## Executive Summary

This document analyzes the data parsing discrepancy encountered when porting Govee H5075 BLE sensor reading code from Python (using bleak library) to ESP32 C++ (using Arduino BLE library). The root cause was a **2-byte offset difference** in how manufacturer data is returned by each library.

---

## Problem Statement

When implementing the same BLE data parsing logic on ESP32 that worked correctly in Python, the following issues occurred:

- **Temperature**: Calculated as 0.1°C instead of 21.6°C
- **Humidity**: Calculated as 84.8% instead of 15.4%
- **Battery**: Calculated as 117% instead of 100%

All values were clearly incorrect, indicating a systematic parsing error rather than hardware issues.

---

## Root Cause Analysis

### Library API Differences

The fundamental difference lies in how each BLE library returns manufacturer advertisement data:

#### Python (bleak library)

**Code Example:**
```python
from bleak import BleakScanner

async def detection_callback(device, advertisement_data):
    manufacturer_data = advertisement_data.manufacturer_data
    if 60552 in manufacturer_data:  # 0xEC88 in decimal
        data = manufacturer_data[60552]
        # data is a byte array WITHOUT manufacturer ID
```

**Data Structure:**
- Manufacturer data returned as **dictionary**: `{manufacturer_id: data_bytes}`
- Manufacturer ID (0xEC88 = 60552) is the **dictionary key**
- Data payload **excludes** the manufacturer ID bytes
- Direct access to sensor data starting at index 0

**Byte Layout:**
```
Index:  [0]  [1]  [2]  [3]  [4]  [5]
Data:   00   03   4C   5A   64   00
        ↑    └────┴────┘    ↑    ↑
       pad  temp/humidity  bat  pad
```

---

#### ESP32 C++ (Arduino BLE)

**Code Example:**
```cpp
#include <BLEAdvertisedDevice.h>

void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveManufacturerData()) {
        std::string mfgData = advertisedDevice.getManufacturerData();
        // mfgData includes manufacturer ID as first 2 bytes
    }
}
```

**Data Structure:**
- Manufacturer data returned as **raw byte string**
- First 2 bytes **contain** the manufacturer ID (little-endian)
- Complete BLE advertisement packet preserved
- Requires manual parsing of manufacturer ID

**Byte Layout:**
```
Index:  [0]  [1]  [2]  [3]  [4]  [5]  [6]  [7]
Data:   88   EC   00   03   4C   5A   64   00
        └────┘    ↑    └────┴────┘    ↑    ↑
      Mfg ID    pad  temp/humidity  bat  pad
     (0xEC88)
```

---

## Byte Position Mapping

### Comparison Table

| Data Component | Python Index | ESP32 Index | Byte Value | Notes |
|----------------|--------------|-------------|------------|-------|
| Manufacturer ID (low byte) | N/A (dict key) | [0] | 0x88 | Little-endian |
| Manufacturer ID (high byte) | N/A (dict key) | [1] | 0xEC | Forms 0xEC88 |
| Padding byte | [0] | [2] | 0x00 | Unknown purpose |
| Temperature/Humidity MSB | [2] | [3] | 0x03 | Big-endian encoding |
| Temperature/Humidity mid | [3] | [4] | 0x4C | Big-endian encoding |
| Temperature/Humidity LSB | [4] | [5] | 0x5A | Big-endian encoding |
| Battery percentage | [5] | [6] | 0x64 | 100 decimal |
| Padding byte | N/A | [7] | 0x00 | Optional |

### Visual Offset Diagram

```
Python bleak:     [00] [03] [4C] [5A] [64] [00]
                   ↓    ↓    ↓    ↓    ↓    ↓
ESP32 BLE:   [88] [EC] [00] [03] [4C] [5A] [64] [00]
             └─────┘
           +2 byte offset from manufacturer ID
```

---

## Correct Parsing Implementation

### Python Version (govee_h5075_reader.py)

```python
def parse_advertisement_data(manufacturer_data):
    """
    Parse Govee H5075 manufacturer data (Python/bleak)
    manufacturer_data is already extracted as data[60552]
    """
    if len(manufacturer_data) < 6:
        return None
    
    # Bytes 2-4 contain encoded temperature and humidity (big-endian)
    encoded = (manufacturer_data[2] << 16) | \
              (manufacturer_data[3] << 8) | \
              manufacturer_data[4]
    
    # Decode: (temp_celsius × 1000 + humidity) × 10
    temperature = ((encoded / 1000) / 10)  # °C
    humidity = ((encoded % 1000) / 10)      # %
    battery = manufacturer_data[5]          # %
    
    return temperature, humidity, battery
```

### ESP32 Version (main.cpp)

```cpp
bool parseGoveeData(std::string& manufacturerData) {
    /*
     * Parse Govee H5075 manufacturer data (ESP32)
     * manufacturerData includes 2-byte manufacturer ID prefix
     */
    if (manufacturerData.length() < 8) return false;
    
    // Verify manufacturer ID (0xEC88 in little-endian)
    if ((unsigned char)manufacturerData[0] != 0x88 || 
        (unsigned char)manufacturerData[1] != 0xEC) {
        return false;
    }
    
    // Bytes 3-5 contain encoded temperature and humidity (big-endian)
    uint32_t encoded = ((unsigned char)manufacturerData[3] << 16) | 
                      ((unsigned char)manufacturerData[4] << 8) | 
                      (unsigned char)manufacturerData[5];
    
    // Decode: (temp_celsius × 1000 + humidity) × 10
    float temperature = (encoded / 1000) / 10.0;  // °C
    float humidity = (encoded % 1000) / 10.0;      // %
    int battery = (unsigned char)manufacturerData[6];  // %
    
    return true;
}
```

---

## Decoding Formula Verification

### Example Calculation

**Raw Data:** `88 EC 00 03 4C 5A 64 00`

**Step 1: Extract encoded value (bytes 3-5 for ESP32)**
```
0x03 << 16 = 0x030000 = 196608
0x4C << 8  = 0x004C00 = 19456
0x5A       = 0x00005A = 90
-------------------------------------
Encoded    = 0x034C5A = 216154 (decimal)
```

**Step 2: Decode temperature**
```
Temperature = (encoded / 1000) / 10
            = (216154 / 1000) / 10
            = 216.154 / 10
            = 21.6154°C
            ≈ 21.6°C ✓
```

**Step 3: Decode humidity**
```
Humidity = (encoded % 1000) / 10
         = (216154 % 1000) / 10
         = 154 / 10
         = 15.4% ✓
```

**Step 4: Extract battery**
```
Battery = manufacturerData[6]
        = 0x64
        = 100 (decimal)
        = 100% ✓
```

---

## Debugging Process

### Issue Discovery

1. **Initial Implementation**: Used same byte indices `[2-4]` from Python on ESP32
2. **Observed Values**: 0.1°C, 84.8%, 117% (clearly wrong)
3. **Added Debug Output**: Printed raw manufacturer data hex dump
4. **Key Observation**: ESP32 data started with `88 EC` (manufacturer ID)

### Debug Output Example

```
GVH5075_7C8E - Raw data (8 bytes): 88 EC 00 03 4C 5A 64 00 
✓ Govee manufacturer ID detected (0xEC88)
Bytes[3-5]: 03 4C 5A = 0x034C5A = 216154
✓ GVH5075_7C8E: 21.6°C, 15.4%, 100%
```

### Multiple Advertisement Types

Govee H5075 devices broadcast **multiple** BLE advertisement packets:

1. **iBeacon Advertisement** (0x004C = Apple)
   ```
   4C 00 02 15 49 4E 54 45 4C 4C 49 5F 52 4F 43 4B 53 5F 48 57 50 75 F2 FF 0C
   ```
   - Must be **skipped** (wrong manufacturer ID)

2. **Govee Advertisement** (0xEC88 = Govee)
   ```
   88 EC 00 03 4C 5A 64 00
   ```
   - This is the **correct** packet to parse

**Solution**: Filter by manufacturer ID before parsing

---

## Platform-Specific Considerations

### Why Libraries Differ

| Aspect | Python (bleak) | ESP32 (Arduino BLE) |
|--------|----------------|---------------------|
| **Abstraction Level** | High-level, parsed | Low-level, raw |
| **Design Philosophy** | Developer convenience | Memory efficiency |
| **Target Use Case** | Cross-platform scanning | Embedded real-time processing |
| **Memory Model** | Dynamic allocation | Static/stack allocation |
| **Data Ownership** | Library manages lifecycle | Developer manages buffer |

### Performance Implications

**Python:**
- Dictionary lookup overhead
- Automatic memory management
- Higher memory usage per device
- Easier debugging with structured data

**ESP32:**
- Direct buffer access (faster)
- Manual memory management
- Lower memory footprint
- More error-prone parsing

---

## Lessons Learned

### Key Takeaways

1. **Never assume API equivalence** across platforms/languages
2. **Always use debug output** to visualize raw data structures
3. **Verify byte offsets** when porting BLE parsing code
4. **Check library documentation** for manufacturer data format
5. **Test with known values** to validate parsing logic
6. **Consider multiple advertisement types** from same device

### Best Practices

✅ **Print raw hex dumps** during development  
✅ **Validate against spec** if available  
✅ **Use manufacturer ID filtering** to avoid wrong packets  
✅ **Implement range checking** for decoded values  
✅ **Document byte layouts** in code comments  
✅ **Compare outputs** between platforms during porting  

### Common Pitfalls

❌ Assuming byte indices are portable  
❌ Ignoring manufacturer ID in raw data  
❌ Not filtering multiple advertisement types  
❌ Skipping sanity checks on decoded values  
❌ Using magic numbers without comments  

---

## References

### Documentation

- **Python bleak**: https://bleak.readthedocs.io/
- **ESP32 BLE Library**: https://github.com/nkolban/ESP32_BLE_Arduino
- **Bluetooth SIG**: https://www.bluetooth.com/specifications/assigned-numbers/

### Related Files

- `govee_h5075_reader.py` - Python implementation (working)
- `main.cpp` - ESP32 implementation (corrected)
- `ESP32_Govee_Reader/ESP32_Govee_Reader.ino` - Full ESP32 HTTP server

### Govee H5075 Specifications

- **Manufacturer ID**: 0xEC88 (60552 decimal)
- **Data Packet Size**: 6-8 bytes
- **Encoding**: Big-endian for temp/humidity
- **Formula**: `(temp_celsius × 1000 + humidity) × 10`
- **Temperature Range**: -40°C to 60°C
- **Humidity Range**: 0% to 100%
- **Battery Range**: 0% to 100%

---

## Conclusion

The 2-byte offset difference between Python's bleak library and ESP32's BLE library was caused by fundamental architectural differences in how manufacturer data is returned:

- **Python**: Pre-parsed dictionary structure (manufacturer ID separated)
- **ESP32**: Raw byte stream (manufacturer ID included)

This analysis demonstrates the importance of understanding platform-specific API behaviors when porting code, especially for low-level protocols like BLE where byte-level precision is critical.

**Resolution**: Adjust byte indices by +2 for ESP32 to account for manufacturer ID prefix, and always validate with debug output.

---

*Document created: February 1, 2026*  
*Project: Govee H5075 BLE Reader*  
*Platforms: Python 3.13 (Windows) & ESP32 (Arduino)*
