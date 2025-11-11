#include "STL_MCU.h"
#include "Rf_component.h"
#include "Rf_file_manager.h"

using namespace  mcu;

// --- Storage Configuration ---
const RfStorageType STORAGE_MODE = RfStorageType::LITTLEFS;  // Change to SD_MMC or SD_SPI as needed

const uint32_t BAUD_RATE = 115200;
const uint16_t NUMBER_OF_COLUMNS = 234; // Number of columns in the CSV dataset, includes label column


void setup() {
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        RF_DEBUGLN("❌ File system initialization failed!");
        return;
    }
    // this will delete all lines that do not have the exact columns number equal to NUM_OF_COLUMNS
    String csv_path = reception_data(NUMBER_OF_COLUMNS);    
    // or :
    // reception_data(0, false); //  no exact column count, do not print file after reception.
    // reception_data();  // default , no exact column count, print file after reception.

    // after reception, convert csv file to binary format
    Rf_data new_data;
    data.convertCSVtoBinary(csv_path.c_str());
}

void loop() {
  manage_files();
}

