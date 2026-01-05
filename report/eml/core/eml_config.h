#pragma once

/**
 * @file eml_config.h
 * @brief Global configuration for EML framework
 */

#include <cstdint>

namespace eml {

// Default configuration values
#ifndef EML_MAX_PATH
#define EML_MAX_PATH 64
#endif

#ifndef EML_CHUNK_SIZE
#define EML_CHUNK_SIZE 1024
#endif

#ifndef EML_DEBUG_LEVEL
#define EML_DEBUG_LEVEL 1
#endif

} // namespace eml
