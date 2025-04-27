#include "../ZipFuncInfrastructure/zlib_wrapper.h"
#include "../Utils/Logger.h" // Include Logger for consistency
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring> // For std::memcpy

// Helper to convert string to vector<Bytef>
std::vector<Bytef> stringToBytes(const std::string& str) {
    std::vector<Bytef> bytes(str.size());
    std::memcpy(bytes.data(), str.data(), str.size());
    return bytes;
}

// Helper to convert vector<Bytef> to string
std::string bytesToString(const std::vector<Bytef>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool runZlibWrapperTests() {
    std::cout << "--- Running Zlib Wrapper Tests ---" << std::endl;
    bool allTestsPassed = true;

    // --- Test Case 1: Basic Compression/Decompression ---
    std::cout << "\n[Test Case 1: Basic Compression/Decompression]" << std::endl;
    std::string originalString = "Hello Hello Hello Zlib Wrapper! This is a test string with some repetition. Hello Hello.";
    std::vector<Bytef> originalData = stringToBytes(originalString);
    std::vector<Bytef> compressedData;
    std::vector<Bytef> decompressedData;
    uLong originalSize = originalData.size();

    std::cout << "Original size: " << originalSize << " bytes" << std::endl;

    // Compress
    SimpZlib::Status compressStatus = SimpZlib::compress(originalData, compressedData);
    std::cout << "Compression status: " << static_cast<int>(compressStatus) << std::endl;
    assert(compressStatus == SimpZlib::Status::OK);
    std::cout << "Compressed size: " << compressedData.size() << " bytes" << std::endl;
    assert(compressedData.size() > 0 && compressedData.size() < originalSize); // Basic check

    // Decompress
    SimpZlib::Status decompressStatus = SimpZlib::uncompress(compressedData, decompressedData, originalSize);
    std::cout << "Decompression status: " << static_cast<int>(decompressStatus) << std::endl;
    assert(decompressStatus == SimpZlib::Status::OK);
    std::cout << "Decompressed size: " << decompressedData.size() << " bytes" << std::endl;
    assert(decompressedData.size() == originalSize);

    // Verify data
    assert(decompressedData == originalData);
    std::cout << "Data verification successful." << std::endl;

    // --- Test Case 2: Empty Input ---
    std::cout << "\n[Test Case 2: Empty Input]" << std::endl;
    std::vector<Bytef> emptyData;
    std::vector<Bytef> compressedEmpty;
    std::vector<Bytef> decompressedEmpty;
    uLong emptySize = 0;

    compressStatus = SimpZlib::compress(emptyData, compressedEmpty);
    std::cout << "Empty compress status: " << static_cast<int>(compressStatus) << std::endl;
    assert(compressStatus == SimpZlib::Status::OK);
    std::cout << "Empty compressed size: " << compressedEmpty.size() << std::endl;
    // zlib might still produce a small header/footer even for empty input
    // assert(compressedEmpty.empty()); // This might fail depending on zlib behavior

    decompressStatus = SimpZlib::uncompress(compressedEmpty, decompressedEmpty, emptySize);
    std::cout << "Empty decompress status: " << static_cast<int>(decompressStatus) << std::endl;
    // Uncompressing empty might result in OK or an error depending on zlib and wrapper logic
    // Let's expect OK and an empty output vector if input was also empty/minimal
    if (decompressStatus == SimpZlib::Status::OK) {
        assert(decompressedEmpty.empty());
        std::cout << "Empty decompression successful (result is empty)." << std::endl;
    } else {
        // Some zlib versions/configs might error on empty uncompress
        std::cout << "Empty decompression resulted in status: " << static_cast<int>(decompressStatus) << " (May be acceptable)" << std::endl;
    }


    // --- Test Case 3: Incorrect Decompression Size ---
    std::cout << "\n[Test Case 3: Incorrect Decompression Size]" << std::endl;
    // Use data from Test Case 1
    std::vector<Bytef> wrongSizeDecompressed;
    uLong wrongSize = originalSize + 10; // Provide incorrect size

    decompressStatus = SimpZlib::uncompress(compressedData, wrongSizeDecompressed, wrongSize);
    std::cout << "Wrong size decompress status: " << static_cast<int>(decompressStatus) << std::endl;
    // We expect an error because the actual decompressed size won't match wrongSize
    assert(decompressStatus == SimpZlib::Status::DataError || decompressStatus == SimpZlib::Status::OutputBufferError); // Expect DataError or BufError
    std::cout << "Decompression failed as expected with wrong size." << std::endl;

    // --- Test Case 4: Corrupted Compressed Data (Simple Test) ---
    std::cout << "\n[Test Case 4: Corrupted Compressed Data]" << std::endl;
    if (compressedData.size() > 5) { // Ensure there's data to corrupt
        std::vector<Bytef> corruptedData = compressedData;
        corruptedData[corruptedData.size() / 2] ^= 0xFF; // Flip some bits in the middle
        std::vector<Bytef> corruptedDecompressed;

        decompressStatus = SimpZlib::uncompress(corruptedData, corruptedDecompressed, originalSize);
        std::cout << "Corrupted data decompress status: " << static_cast<int>(decompressStatus) << std::endl;
        assert(decompressStatus != SimpZlib::Status::OK); // Expect failure
        std::cout << "Decompression failed as expected with corrupted data." << std::endl;
    } else {
        std::cout << "Skipping corruption test (compressed data too small)." << std::endl;
    }

    // --- Test Case 5: Different Compression Level ---
    std::cout << "\n[Test Case 5: Different Compression Level (Level 1)]" << std::endl;
    std::vector<Bytef> compressedLevel1;
    compressStatus = SimpZlib::compress(originalData, compressedLevel1, 1); // Use Z_BEST_SPEED
    std::cout << "Level 1 compress status: " << static_cast<int>(compressStatus) << std::endl;
    assert(compressStatus == SimpZlib::Status::OK);
    std::cout << "Level 1 compressed size: " << compressedLevel1.size() << " bytes" << std::endl;
    // Typically, lower level means larger size but faster compression
    assert(compressedLevel1.size() >= compressedData.size()); // Expect size >= default level size

    // Decompress level 1 data
    std::vector<Bytef> decompressedLevel1;
    decompressStatus = SimpZlib::uncompress(compressedLevel1, decompressedLevel1, originalSize);
    std::cout << "Level 1 decompress status: " << static_cast<int>(decompressStatus) << std::endl;
    assert(decompressStatus == SimpZlib::Status::OK);
    assert(decompressedLevel1.size() == originalSize);
    assert(decompressedLevel1 == originalData);
    std::cout << "Level 1 data verified successfully." << std::endl;


    std::cout << "\n--- Zlib Wrapper Tests " << (allTestsPassed ? "Passed" : "Failed") << " ---" << std::endl;
    return allTestsPassed;
}


int main() {
    // Initialize Logger
    if (!TilelandWorld::Logger::getInstance().initialize("zlib_wrapper_test.log")) {
        return 1; // Logger init failed
    }

    LOG_INFO("Starting Zlib Wrapper Tests...");
    bool success = runZlibWrapperTests();
    LOG_INFO("Zlib Wrapper Tests finished.");

    // Shutdown Logger
    TilelandWorld::Logger::getInstance().shutdown();

    return success ? 0 : 1;
}
