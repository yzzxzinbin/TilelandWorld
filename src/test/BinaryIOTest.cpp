#include "../src/BinaryFileInfrastructure/BinaryWriter.h"
#include "../src/BinaryFileInfrastructure/BinaryReader.h"
#include "../src/BinaryFileInfrastructure/FileFormat.h" // For FileHeader struct
#include "../src/BinaryFileInfrastructure/Checksum.h"   // For checksum functions
#include "../Utils/Logger.h" // <-- 包含 Logger
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <limits>
#include <cstring> // For std::memcmp
#include <iomanip> // For std::hex, std::setw, std::setfill

// 使用 TilelandWorld 命名空间
using namespace TilelandWorld;

// 定义测试文件名
const std::string testFilePath = "binary_io_test.bin";

// 辅助函数：比较浮点数
bool areFloatsEqual(float a, float b, float epsilon = std::numeric_limits<float>::epsilon()) {
    return std::abs(a - b) <= epsilon * std::max(1.0f, std::max(std::abs(a), std::abs(b)));
}

// 辅助函数：打印字节向量为十六进制
void printBytes(const std::vector<uint8_t>& bytes) {
    std::cout << "[";
    for (size_t i = 0; i < bytes.size(); ++i) {
        std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]) << std::dec;
        if (i < bytes.size() - 1) {
            std::cout << ", ";
        }
    }
    std::cout << "]";
}
void printBytes(const std::vector<char>& bytes) {
    std::cout << "[";
    for (size_t i = 0; i < bytes.size(); ++i) {
        std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<uint8_t>(bytes[i])) << std::dec;
        if (i < bytes.size() - 1) {
            std::cout << ", ";
        }
    }
    std::cout << "]";
}

// 运行所有二进制 I/O 测试
bool runBinaryIOTests() {
    std::cout << "--- Running Binary I/O Tests ---" << std::endl;
    bool allTestsPassed = true;

    // --- Test Data ---
    const int32_t testInt = -12345;
    const uint64_t testUInt64 = 9876543210ULL;
    const float testFloat = 3.14159f;
    const double testDouble = 2.718281828459;
    const bool testBoolTrue = true;
    const bool testBoolFalse = false;
    const uint8_t testUInt8 = 200;
    const std::string testString = "Hello, Binary World! \U0001F310"; // Includes UTF-8
    const std::string emptyString = "";
    const std::vector<uint8_t> testBytes = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    FileHeader testHeader = {}; // Use default values + magic/version
    testHeader.magicNumber = MAGIC_NUMBER;
    testHeader.versionMajor = FORMAT_VERSION_MAJOR;
    testHeader.versionMinor = FORMAT_VERSION_MINOR;
    testHeader.dataOffset = 1024;
    testHeader.indexOffset = 512;
    const int32_t overwriteValue = 9999;
    std::streampos intPosition = -1; // 用于记录 int 的位置

    // --- Write Test ---
    std::cout << "Testing BinaryWriter..." << std::endl;
    try {
        BinaryWriter writer(testFilePath);
        assert(writer.good());

        intPosition = writer.tell(); // 记录初始位置，即 int 的位置
        assert(writer.write(testInt));
        assert(writer.write(testUInt64));
        assert(writer.write(testFloat));
        assert(writer.write(testDouble));
        assert(writer.write(testBoolTrue));
        assert(writer.write(testBoolFalse));
        assert(writer.write(testUInt8));
        assert(writer.writeString(testString));
        assert(writer.writeString(emptyString));
        assert(writer.writeBytes(reinterpret_cast<const char*>(testBytes.data()), testBytes.size()));
        assert(writer.write(testHeader)); // FileHeader is trivially copyable

        // 测试 seekp 和覆盖
        std::streampos endPos = writer.tell();
        assert(writer.seek(intPosition)); // 回到 int 的位置
        assert(writer.write(overwriteValue)); // 覆盖 int
        assert(writer.seek(endPos)); // 回到末尾 (确保后续写入在正确位置)

        // 写入结束
        std::cout << "BinaryWriter tests finished." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "BinaryWriter test failed with exception: " << e.what() << std::endl;
        allTestsPassed = false;
    }

    if (!allTestsPassed) return false; // 如果写入失败，则不继续读取

    // --- Read Test ---
    std::cout << "Testing BinaryReader..." << std::endl;
    try {
        BinaryReader reader(testFilePath);
        assert(reader.good());

        // 检查文件大小是否合理 (至少包含写入的数据)
        std::streampos fileSize = reader.fileSize();
        std::cout << "Reported file size: " << fileSize << std::endl;
        assert(fileSize > 0);

        int32_t readInt = 0;
        uint64_t readUInt64 = 0;
        float readFloat = 0.0f;
        double readDouble = 0.0;
        bool readBoolTrue = false;
        bool readBoolFalse = true;
        uint8_t readUInt8 = 0;
        std::string readString;
        std::string readEmptyString;
        std::vector<char> readBytesBuffer(testBytes.size());
        FileHeader readHeader = {};

        std::cout << "\n--- Reading and Verifying Data ---" << std::endl;

        // 读取被覆盖的 int
        assert(reader.read(readInt));
        std::cout << "Overwritten Int32: Expected: " << overwriteValue << ", Got: " << readInt << std::endl;
        assert(readInt == overwriteValue); // 验证覆盖是否成功

        // 读取其他数据
        assert(reader.read(readUInt64));
        std::cout << "UInt64:          Expected: " << testUInt64 << ", Got: " << readUInt64 << std::endl;
        assert(readUInt64 == testUInt64);

        assert(reader.read(readFloat));
        std::cout << "Float:           Expected: " << testFloat << ", Got: " << readFloat << std::endl;
        assert(areFloatsEqual(readFloat, testFloat));

        assert(reader.read(readDouble));
        std::cout << "Double:          Expected: " << testDouble << ", Got: " << readDouble << std::endl;
        assert(readDouble == testDouble);

        assert(reader.read(readBoolTrue));
        std::cout << "Bool (true):     Expected: " << std::boolalpha << testBoolTrue << ", Got: " << readBoolTrue << std::endl;
        assert(readBoolTrue == testBoolTrue);

        assert(reader.read(readBoolFalse));
        std::cout << "Bool (false):    Expected: " << std::boolalpha << testBoolFalse << ", Got: " << readBoolFalse << std::endl;
        assert(readBoolFalse == testBoolFalse);

        assert(reader.read(readUInt8));
        std::cout << "UInt8:           Expected: " << static_cast<int>(testUInt8) << ", Got: " << static_cast<int>(readUInt8) << std::endl;
        assert(readUInt8 == testUInt8);

        assert(reader.readString(readString));
        std::cout << "String:          Expected: \"" << testString << "\", Got: \"" << readString << "\"" << std::endl;
        assert(readString == testString);

        assert(reader.readString(readEmptyString));
        std::cout << "Empty String:    Expected: \"\", Got: \"" << readEmptyString << "\"" << std::endl;
        assert(readEmptyString == emptyString);

        size_t bytesActuallyRead = reader.readBytes(readBytesBuffer.data(), readBytesBuffer.size());
        std::cout << "Bytes:           Expected: "; printBytes(testBytes);
        std::cout << ", Got: "; printBytes(readBytesBuffer); std::cout << std::endl;
        assert(bytesActuallyRead == testBytes.size());
        assert(std::memcmp(readBytesBuffer.data(), testBytes.data(), testBytes.size()) == 0);

        assert(reader.read(readHeader));
        std::cout << "FileHeader Magic: Expected: 0x" << std::hex << testHeader.magicNumber << ", Got: 0x" << readHeader.magicNumber << std::dec << std::endl;
        std::cout << "FileHeader Ver:   Expected: " << testHeader.versionMajor << "." << testHeader.versionMinor
                  << ", Got: " << readHeader.versionMajor << "." << readHeader.versionMinor << std::endl;
        std::cout << "FileHeader Offsets: Expected data=" << testHeader.dataOffset << ", index=" << testHeader.indexOffset
                  << ", Got data=" << readHeader.dataOffset << ", index=" << readHeader.indexOffset << std::endl;
        // 比较结构体内容 (除了 checksum，因为它在写入时未计算)
        assert(readHeader.magicNumber == testHeader.magicNumber);
        assert(readHeader.versionMajor == testHeader.versionMajor);
        assert(readHeader.versionMinor == testHeader.versionMinor);
        assert(readHeader.dataOffset == testHeader.dataOffset);
        assert(readHeader.indexOffset == testHeader.indexOffset);
        // checksum is not compared here as it wasn't properly calculated/written in this simple test

        // 测试读取到文件末尾
        std::cout << "\n--- Testing EOF ---" << std::endl;
        assert(!reader.eof()); // 此时应该刚好读完
        std::cout << "Before final read attempt: eof() = " << std::boolalpha << reader.eof() << std::endl;
        uint8_t dummyByte;
        bool finalReadSuccess = reader.read(dummyByte); // 尝试再读一个字节
        std::cout << "After final read attempt: read success = " << std::boolalpha << finalReadSuccess
                  << ", eof() = " << reader.eof() << std::endl;
        assert(!finalReadSuccess); // 应该失败
        assert(reader.eof()); // 现在应该到达 eof

        // 测试 seekg
        std::cout << "\n--- Testing Seek ---" << std::endl;
        assert(reader.seek(0)); // 回到开头
        std::cout << "Seek to beginning successful." << std::endl;
        assert(reader.read(readInt));
        std::cout << "Read Int32 after seek: Expected: " << overwriteValue << ", Got: " << readInt << std::endl;
        assert(readInt == overwriteValue); // 再次验证

        std::cout << "\nBinaryReader tests finished." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "BinaryReader test failed with exception: " << e.what() << std::endl;
        allTestsPassed = false;
    }

    // --- Checksum Test ---
    std::cout << "\nTesting Checksum..." << std::endl; // 添加换行
    try {
        // 使用 CRC32 进行测试
        uint32_t checksum1 = calculateCRC32(testBytes.data(), testBytes.size());
        uint32_t checksum2 = calculateCRC32(testString.c_str(), testString.length());
        std::cout << "CRC32 calculated for testBytes: 0x" << std::hex << checksum1 << std::dec << std::endl;
        std::cout << "CRC32 calculated for testString: 0x" << std::hex << checksum2 << std::dec << std::endl;
        // 简单的非零断言
        assert(checksum1 != 0 || testBytes.empty());
        assert(checksum2 != 0 || testString.empty());

        std::cout << "Checksum tests finished." << std::endl;
    } catch (const std::exception& e) {
         std::cerr << "Checksum test failed with exception: " << e.what() << std::endl;
         allTestsPassed = false;
    }


    std::cout << "\n--- Binary I/O Tests " << (allTestsPassed ? "Passed" : "Failed") << " ---" << std::endl; // 添加换行
    return allTestsPassed;
}

int main() {
    // 初始化日志
    if (!TilelandWorld::Logger::getInstance().initialize("binary_io_test.log")) {
        return 1; // 日志初始化失败
    }

    LOG_INFO("Starting Binary I/O Tests...");
    bool success = runBinaryIOTests();
    LOG_INFO("Binary I/O Tests finished.");

    // 关闭日志
    TilelandWorld::Logger::getInstance().shutdown();

    return success ? 0 : 1;
}
