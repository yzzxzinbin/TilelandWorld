#pragma once

#include <vector>
#include <cstddef> // For size_t

// Include the original zlib header directly
#include "zlib/zlib.h" // Provides Bytef, uInt, uLong etc.

namespace SimpZlib {

    // Example status enum (can be expanded)
    enum class Status {
        OK,
        OutputBufferError,
        MemoryError,
        DataError,
        StreamError,
        UnknownError
    };

    // Compresses data using zlib default settings or a specified level.
    // Uses types directly from zlib.h
    Status compress(const std::vector<Bytef>& input, std::vector<Bytef>& output, int level = -1); // Use std::vector<Bytef>

    // Decompresses data. known_uncompressed_size must be the exact size of the original data.
    // Uses types directly from zlib.h
    Status uncompress(const std::vector<Bytef>& input, std::vector<Bytef>& output, uLong known_uncompressed_size); // Use uLong

    // Optional: Function to get error string (implement in cpp)
    // std::string getStatusString(Status s);

} // namespace SimpZlib
