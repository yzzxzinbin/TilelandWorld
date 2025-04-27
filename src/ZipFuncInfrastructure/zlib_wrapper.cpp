#include "zlib_wrapper.h"

// zlib.h is already included via zlib_wrapper.h, but including it again is harmless
// #include "zlib/zlib.h" // No longer strictly needed here if included in .h

#include <limits> // For numeric_limits
#include <stdexcept> // For std::length_error

// Define byte_vector locally for convenience if needed, or use std::vector<Bytef> directly
using byte_vector = std::vector<Bytef>;

namespace SimpZlib {

    // Helper to map zlib errors to our status
    Status mapZlibError(int zlib_err) {
        switch (zlib_err) {
            case Z_OK:           return Status::OK;
            case Z_MEM_ERROR:    return Status::MemoryError;
            case Z_BUF_ERROR:    return Status::OutputBufferError;
            case Z_DATA_ERROR:   return Status::DataError;
            case Z_STREAM_ERROR: return Status::StreamError;
            default:             return Status::UnknownError;
        }
    }

    // Wrapper function signature uses types from zlib.h
    Status compress(const byte_vector& input, byte_vector& output, int level) {
        // Use z_uLong (from zlib.h via Z_PREFIX) for internal length variables
        // These types are directly available because zlib.h is included.
        z_uLong destLenEst = ::z_compressBound(input.size());
        output.resize(destLenEst);

        z_uLong destLen = destLenEst;

        if (input.size() > std::numeric_limits<z_uLong>::max()) {
             output.clear();
             return Status::StreamError;
        }
        z_uLong sourceLen = input.size(); // Direct assignment, size_t to z_uLong

        // Call zlib function - types match due to zlib.h inclusion and Z_PREFIX
        int ret = ::z_compress2(output.data(), &destLen, input.data(), sourceLen, level);

        Status status = mapZlibError(ret);
        if (status == Status::OK) {
            output.resize(destLen);
        } else {
            output.clear();
        }
        return status;
    }

    // Wrapper function signature uses types from zlib.h
    Status uncompress(const byte_vector& input, byte_vector& output, uLong known_uncompressed_size) {
        // known_uncompressed_size is already uLong (which becomes z_uLong due to Z_PREFIX)
        z_uLong destLenExpected = known_uncompressed_size; // Direct use

        if (destLenExpected == 0) {
             output.clear();
             return Status::StreamError;
        }

        output.resize(destLenExpected);
        z_uLong destLen = destLenExpected;

        if (input.size() > std::numeric_limits<z_uLong>::max()) {
             output.clear();
             return Status::StreamError;
        }
        z_uLong sourceLen = input.size(); // Direct assignment

        // Call zlib function - types match
        int ret = ::z_uncompress(output.data(), &destLen, input.data(), sourceLen);

        Status status = mapZlibError(ret);
        if (status == Status::OK) {
            if (destLen != destLenExpected) {
                 output.resize(destLen);
                 return Status::DataError;
            }
        } else {
            output.clear();
        }
        return status;
    }

    // Optional: Implement getStatusString(Status s) here if needed

} // namespace SimpZlib
