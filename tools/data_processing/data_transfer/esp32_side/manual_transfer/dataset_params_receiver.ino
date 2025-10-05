/*
- this program receivesa a csv file from the host computer via serial connection.
- open serial monitor and copy-paste the csv file content into the serial monitor.
*/


#include "STL_MCU.h"
#include "LittleFS.h"
#include "Rf_file_manager.h"

using namespace  mcu;

const uint32_t BAUD_RATE = 115200;


void setup() {
    Serial.begin(BAUD_RATE);  
    while (!Serial);       // <-- Waits for Serial monitor to connect (important for USB CDC)
    Serial.println("Hello from ESP32!");

    // 1. Mount LittleFS
    if (!LittleFS.begin(true)) {
      Serial.println("❌ LittleFS Mount Failed!");
      while (true) { delay(1000); }
    }

    reception_data();
    /*
    or:
    reception_data(0, false); //  no exact column count, do not print file after reception.
    reception_data(30); // Expect exactly 30 columns each row, print file after reception.
    */
}

void loop() {
  // nothing to do here
}