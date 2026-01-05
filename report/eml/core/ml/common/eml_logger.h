#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include "eml/core/containers/stl_mcu/STL_MCU.h"
#include "eml/core/ml/common/eml_common_defs.h"

namespace eml {

    class Eml_logger {
        char time_log_path[RF_PATH_BUFFER] = {'\0'};
        char memory_log_path[RF_PATH_BUFFER] = {'\0'};
        b_vector<time_anchor> time_anchors;
    public:
        uint32_t freeHeap;
        uint32_t largestBlock;
        long unsigned starting_time;
        uint8_t fragmentation;
        uint32_t lowest_ram;
        uint64_t lowest_rom; 
        uint64_t freeDisk;
        float log_time;

    
    public:
        Eml_logger() : freeHeap(0), largestBlock(0), starting_time(0), fragmentation(0), log_time(0.0f) {
        }

        Eml_logger(Rf_base* base, bool keep_old_file = false) : freeHeap(0), largestBlock(0), starting_time(0), fragmentation(0), log_time(0.0f) {
            init(base, keep_old_file);
        }
        
        void init(Rf_base* base, bool keep_old_file = false){
            eml_debug(2, "🔧 Initializing logger");
            time_anchors.clear();
            starting_time = rf_time_now(MILLISECONDS);
            drop_anchor(); // initial anchor at index 0

            lowest_ram = UINT32_MAX;
            lowest_rom = UINT64_MAX;

            base->get_time_log_path(this->time_log_path);
            base->get_memory_log_path(this->memory_log_path);

            if(time_log_path[0] == '\0' || memory_log_path[0] == '\0'){
                eml_debug(1, "❌ Cannot init logger: log file paths not set correctly");
                return;
            }

            if(!keep_old_file){
                if(RF_FS_EXISTS(time_log_path)){
                    RF_FS_REMOVE(time_log_path); 
                }
                // write header to time log file
                File logFile = RF_FS_OPEN(time_log_path, FILE_WRITE);
                if (logFile) {
                    logFile.println("Event,\t\tTime(ms),duration,Unit");
                    logFile.close();
                }
            }
            t_log("init tracker"); // Initial log without printing

            if(!keep_old_file){                
                // clear file system log file if it exists
                if(RF_FS_EXISTS(memory_log_path)){
                    RF_FS_REMOVE(memory_log_path); 
                }
                // write header to log file
                File logFile = RF_FS_OPEN(memory_log_path, FILE_WRITE);
                if (logFile) {
                    logFile.println("Time(s),FreeHeap,Largest_Block,FreeDisk");
                    logFile.close();
                } 
            }
            m_log("init tracker", true); // Initial log without printing
        }

        void m_log(const char* msg, bool log = true){
            auto heap_status = eml_memory_status();
            freeHeap = heap_status.first;
            largestBlock = heap_status.second;
            // Calculate free disk properly based on the active storage backend
            uint64_t totalBytes = RF_TOTAL_BYTES();
            uint64_t usedBytes = RF_USED_BYTES();
            uint64_t availableBytes = 0;
            if (totalBytes >= usedBytes) availableBytes = totalBytes - usedBytes;
            freeDisk = availableBytes;

            if(freeHeap < lowest_ram) lowest_ram = freeHeap;
            if(freeDisk < lowest_rom) lowest_rom = freeDisk;

            fragmentation = 100 - (largestBlock * 100 / freeHeap);

            // Log to file with timestamp
            if(log) {        
                log_time = (rf_time_now(MILLISECONDS) - starting_time)/1000.0f; 
                File logFile = RF_FS_OPEN(memory_log_path, FILE_APPEND);
                if (logFile) {
                    logFile.printf("%.2f,\t%u,\t%u,\t%llu",
                                    log_time, freeHeap, largestBlock, (unsigned long long)freeDisk);
                    if(msg && strlen(msg) > 0){
                        logFile.printf(",\t%s\n", msg);
                    } else {
                        logFile.println();
                    }
                    logFile.close();
                } else eml_debug(1, "❌ Failed to open memory log file for appending: ", memory_log_path);
            }
        }

        // fast log : just for measure and update : lowest ram and fragmentation
        void m_log(){
            m_log("", false);
        }
        
        uint16_t drop_anchor(){
            time_anchor anchor;
            anchor.anchor_time = rf_time_now(MILLISECONDS);
            anchor.index = time_anchors.size();
            time_anchors.push_back(anchor);
            return anchor.index;
        }

        uint16_t current_anchor() const {
            return time_anchors.size() > 0 ? time_anchors.back().index : 0;
        }

        size_t memory_usage() const {
            size_t total = sizeof(Eml_logger);
            return total;
        }

        // for durtion measurement between two anchors
        long unsigned t_log(const char* msg, size_t begin_anchor_index, size_t end_anchor_idex, const char* unit = "ms"){
            float ratio = 1;  // default to ms 
            if(strcmp(unit, "s") == 0 || strcmp(unit, "second") == 0) ratio = 1000.0f;
            else if(strcmp(unit, "us") == 0 || strcmp(unit, "microsecond") == 0) ratio = 0.001f;

            if(time_anchors.size() == 0) return 0; // no anchors set
            if(begin_anchor_index >= time_anchors.size() || end_anchor_idex >= time_anchors.size()) return 0; // invalid index
            if(end_anchor_idex <= begin_anchor_index) {
                std::swap(begin_anchor_index, end_anchor_idex);
            }

            long unsigned begin_time = time_anchors[begin_anchor_index].anchor_time;
            long unsigned end_time = time_anchors[end_anchor_idex].anchor_time;
            float elapsed = (end_time - begin_time)/ratio;

            // Log to file with timestamp      ; 
            File logFile = RF_FS_OPEN(time_log_path, FILE_APPEND);
            if (logFile) {
                if(msg && strlen(msg) > 0){
                    logFile.printf("%s,\t%.1f,\t%.2f,\t%s\n", msg, begin_time/1000.0f, elapsed, unit);     // time always in s
                } else {
                    if(ratio > 1.1f)
                        logFile.printf("unknown event,\t%.1f,\t%.2f,\t%s\n", begin_time/1000.0f, elapsed, unit); 
                    else 
                        logFile.printf("unknown event,\t%.1f,\t%lu,\t%s\n", begin_time/1000.0f, (long unsigned)elapsed, unit);
                }
                logFile.close();
            }else{
                eml_debug(1, "❌ Failed to open time log file: ", time_log_path);
            }

            time_anchors[end_anchor_idex].anchor_time = rf_time_now(MILLISECONDS); // reset end anchor to current time
            return (long unsigned)elapsed;
        }
    
        /**
         * @brief for duration measurement from an anchor to now
         * @param msg name of the event
         * @param begin_anchor_index index of the begin anchor
         * @param unit time unit, "ms" (default), "s", "us" 
         * @param print whether to print to // Serial, will be disabled if RF_DEBUG_LEVEL <= 1
         * @note : this action will create a new anchor at the current time
         */
        long unsigned t_log(const char* msg, size_t begin_anchor_index, const char* unit = "ms"){
            time_anchor end_anchor;
            end_anchor.anchor_time = rf_time_now(MILLISECONDS);
            end_anchor.index = time_anchors.size();
            time_anchors.push_back(end_anchor);
            return t_log(msg, begin_anchor_index, end_anchor.index, unit);
        }

        /**
         * @brief log time from starting point to now
         * @param msg name of the event
         * @param print whether to print to // Serial, will be disabled if RF_DEBUG_LEVEL <= 1
         * @note : this action will NOT create a new anchor
         */
        long unsigned t_log(const char* msg){
            long unsigned current_time = rf_time_now(MILLISECONDS) - starting_time;

            // Log to file with timestamp
            File logFile = RF_FS_OPEN(time_log_path, FILE_APPEND);
            if (logFile) {
                if(msg && strlen(msg) > 0){
                    logFile.printf("%s,\t%.1f,\t_,\tms\n", msg, current_time/1000.0f); // time always in s
                } else {
                    logFile.printf("unknown event,\t%.1f,\t_,\tms\n", current_time/1000.0f); // time always in s
                }
                logFile.close();
            }else{
                eml_debug(1, "❌ Failed to open time log file: ", time_log_path);
            }
            return current_time;
        }
        
        // print out memory_log file to // Serial
        void print_m_log(){
            if(memory_log_path[0] == '\0'){
                eml_debug(1, "❌ Cannot print memory log: log file path not set correctly");
                return;
            }
            if(!RF_FS_EXISTS(memory_log_path)){
                eml_debug(1, "❌ Cannot print memory log: log file does not exist");
                return;
            }
            File file = RF_FS_OPEN(memory_log_path, RF_FILE_READ);
            if(!file){
                eml_debug(1, "❌ Cannot open memory log file for reading: ", memory_log_path);
                return;
            }
            String line;
            while(file.available()){
                line = file.readStringUntil('\n');
                eml_debug(0, line.c_str());
            }
            file.close();
        }

        // print out time_log file to // Serial
        void print_t_log(){
            if(time_log_path[0] == '\0'){
                eml_debug(1, "❌ Cannot print time log: log file path not set correctly");
                return;
            }
            if(!RF_FS_EXISTS(time_log_path)){
                eml_debug(1, "❌ Cannot print time log: log file does not exist");
                return;
            }
            File file = RF_FS_OPEN(time_log_path, RF_FILE_READ);
            if(!file){
                eml_debug(1, "❌ Cannot open time log file for reading: ", time_log_path);
                return;
            }
            String line;
            while(file.available()){
                line = file.readStringUntil('\n');
                eml_debug(0, line.c_str());
            }
            file.close();
        }
    };

} // namespace eml
