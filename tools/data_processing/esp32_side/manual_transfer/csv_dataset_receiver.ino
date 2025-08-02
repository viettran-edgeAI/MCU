#include "STL_MCU.h"
#include "FS.h"
#include "SPIFFS.h"
#include "Rf_file_manager.h"

using namespace  mcu;

const uint32_t BAUD_RATE = 115200;
const uint16_t NUMBER_OF_COLUMNS = 234; // Number of columns in the CSV dataset, includes label column


void setup() {
    Serial.begin(BAUD_RATE);  
    while (!Serial);       // <-- Waits for Serial monitor to connect (important for USB CDC)
    delay(2000);

    // 1. Mount SPIFFS
    if (!SPIFFS.begin(true)) {
      Serial.println("❌ SPIFFS Mount Failed!");
      while (true) { delay(1000); }
    }
    // this will delete all lines that do not have the exact columns number equal to NUM_OF_COLUMNS
    String csv_path = reception_data(NUMBER_OF_COLUMNS);    
    // or :
    // reception_data(0, false); //  no exact column count, do not print file after reception.
    // reception_data();  // default , no exact column count, print file after reception.

    // after reception, convert csv file to binary format
    if(csv_path.length() > 0) {
        Rf_data dataset;
        const char* base_name = csv_path.c_str();
        if (csv_path.endsWith(".csv")) {
            base_name = csv_path.substring(0, csv_path.length() - 4).c_str();
        }
        dataset.flag = Rf_data_flags::BASE_DATA; // set flag for base data
        dataset.filename = String(base_name) + ".bin"; // save to binary format with
        dataset.loadCSVData(csv_path, NUMBER_OF_COLUMNS - 1); // -1 because first column is label
        dataset.releaseData(); // save to binary format
        Serial.println("✅ CSV data loaded and saved to binary format.");
    } else {
        Serial.println("❌ No valid file received.");
    }


}

void loop() {
  // nothing to do here
}

