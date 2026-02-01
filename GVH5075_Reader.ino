/*
   Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleScan.cpp
   Ported to Arduino ESP32 by Evandro Copercini
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

int scanTime = 5;  //In seconds
BLEScan *pBLEScan;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Check if device name starts with GVH5075
    String deviceName = advertisedDevice.getName();
    if (deviceName.startsWith("GVH5075")) {
      // Output manufacturer data if available
      if (advertisedDevice.haveManufacturerData()) {
        String manufacturerData = advertisedDevice.getManufacturerData();
        
        // Debug: Print raw manufacturer data
        Serial.printf("\n%s - Raw data (%d bytes): ", deviceName.c_str(), manufacturerData.length());
        for (int i = 0; i < manufacturerData.length(); i++) {
          Serial.printf("%02X ", (unsigned char)manufacturerData[i]);
        }
        Serial.println();
        
        // Check for correct manufacturer ID 0xEC88 (little-endian: 88 EC)
        if (manufacturerData.length() >= 8 && 
            (unsigned char)manufacturerData[0] == 0x88 && 
            (unsigned char)manufacturerData[1] == 0xEC) {
          
          Serial.println("✓ Govee manufacturer ID detected (0xEC88)");
          
          // Extract bytes 3-5 for temp/humidity (skipping manufacturer ID at 0-1 and padding at 2)
          // manufacturerData format: [88 EC] [00] [03 50 55] [64] [00]
          //                          [ID  ] [? ] [encoded ] [bat][? ]
          uint32_t encoded = ((unsigned char)manufacturerData[3] << 16) | 
                            ((unsigned char)manufacturerData[4] << 8) | 
                            (unsigned char)manufacturerData[5];
          
          Serial.printf("Bytes[3-5]: %02X %02X %02X = 0x%06X = %u\n",
                       (unsigned char)manufacturerData[3],
                       (unsigned char)manufacturerData[4],
                       (unsigned char)manufacturerData[5],
                       encoded, encoded);
          
          // Decode temperature and humidity
          // Combined value = (Temperature × 1000) + (Humidity × 10)
          float temperature = (encoded / 1000) / 10.0;
          float humidity = (encoded % 1000) / 10.0;
          int battery = (unsigned char)manufacturerData[6];  // Battery is at index 6
          
          // Validate ranges
          if (temperature >= -40.0 && temperature <= 60.0 &&
              humidity >= 0.0 && humidity <= 100.0 &&
              battery >= 0 && battery <= 100) {
            
            // Serial output: Device, Temperature, Humidity, Battery
            Serial.printf("✓ %s: %.1f°C, %.1f%%, %d%%\n\n", 
                         deviceName.c_str(), temperature, humidity, battery);
          } else {
            Serial.printf("✗ Values out of range: Temp=%.1f°C, Hum=%.1f%%, Bat=%d%%\n\n",
                         temperature, humidity, battery);
          }
        } else {
          // Not Govee manufacturer data - likely iBeacon or other format
          uint16_t mfgId = 0;
          if (manufacturerData.length() >= 2) {
            mfgId = ((unsigned char)manufacturerData[1] << 8) | (unsigned char)manufacturerData[0];
          }
          Serial.printf("⊘ Skipping non-Govee data (Manufacturer ID: 0x%04X, expected 0xEC88)\n\n", mfgId);
        }
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Scanning...");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();  //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);  //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value
}

void loop() {
  // put your main code here, to run repeatedly:
  BLEScanResults *foundDevices = pBLEScan->start(scanTime, false);  
  pBLEScan->clearResults();  // delete results fromBLEScan buffer to release memory
  delay(2000);
}