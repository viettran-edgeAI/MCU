/*
- this program receivesa a csv file from the host computer via serial connection.
- open serial monitor and copy-paste the csv file content into the serial monitor.
*/
#include "Rf_file_manager.h"

using namespace  mcu;

const uint32_t BAUD_RATE = 115200;


void setup() {
    // Initialize file system
    if (!RF_FS_BEGIN()) {
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