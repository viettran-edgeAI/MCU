/*
- this program receivesa a csv file from the host computer via serial connection.
- open serial monitor and copy-paste the csv file content into the serial monitor.
*/
#include "Rf_file_manager.h"

using namespace  mcu;

// --- Storage Configuration ---
const RfStorageType STORAGE_MODE = RfStorageType::FLASH;  // Change to SD_MMC_1BIT, SD_MMC_4BIT, or SD_SPI as needed

const uint32_t BAUD_RATE = 115200;


void setup() {
    // Initialize file system with selected storage mode
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        RF_DEBUGLN("❌ File system initialization failed!");
        return;
    }

    reception_data();

    /*
    or:
    reception_data(0, false); //  no exact column count, do not print file after reception.
    reception_data(30); // Expect exactly 30 columns each row, print file after reception.
    */
}

void loop() {
    manage_files();
}