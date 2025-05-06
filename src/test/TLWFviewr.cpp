// filepath: k:\test program\TilelandWorld_Exp\src\test\TlwfViewer.cpp
#include "../Map.h"
#include "../BinaryFileInfrastructure/MapSerializer.h"
#include "../BinaryFileInfrastructure/BinaryReader.h"
#include "../BinaryFileInfrastructure/FileFormat.h"
#include "../BinaryFileInfrastructure/Checksum.h"
#include "../Constants.h"
#include "../Tile.h"
#include "../TerrainTypes.h"
#include "../Utils/Logger.h"
#include "../Chunk.h"                                          // For coordinate constants if needed
#include "../MapGenInfrastructure/FastNoiseTerrainGenerator.h" // Needed for option C
#include "../MapGenInfrastructure/FlatTerrainGenerator.h"      // Needed for default generator info
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <iomanip>
#include <limits>    // For numeric_limits
#include <algorithm> // For std::min/max
#include <set>       // To store unique Z levels
#include <sstream>   // For string streams

// Platform-specific includes and setup
#ifdef _WIN32
#include <windows.h> // Needed for GetAsyncKeyState, Sleep, console functions
#include <conio.h>   // Needed for _getch in main menu
#else
// POSIX input code removed as requested
#error "This viewer currently only supports Windows due to GetAsyncKeyState usage."
#endif

using namespace TilelandWorld;

// --- Console Utilities ---
void clearScreen()
{
    // CSI[2J clears screen, CSI[H moves cursor to top-left
    std::cout << "\x1b[2J\x1b[H" << std::flush;
}

void moveCursor(int row, int col)
{
    // ANSI escape code CSI[<row>;<col>H
    std::cout << "\x1b[" << row << ";" << col << "H";
}

void hideCursor()
{
    // ANSI escape code CSI[?25l
    std::cout << "\x1b[?25l" << std::flush;
}

void showCursor()
{
    // ANSI escape code CSI[?25h
    std::cout << "\x1b[?25h" << std::flush;
}

void setupConsole()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode))
    {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
    SetConsoleOutputCP(65001); // UTF-8 Output
    SetConsoleCP(65001);       // UTF-8 Input
#endif
}

// --- Copied Visualization Helper ---
std::string formatTileForTerminal(const Tile &tile)
{
    const auto &props = getTerrainProperties(tile.terrain);

    if (!props.isVisible)
    {
        return "  \x1b[0m"; // Invisible tile (reset color)
    }

    RGBColor fg = tile.getForegroundColor();
    RGBColor bg = tile.getBackgroundColor();
    std::string displayChar = props.displayChar;

    std::string fgCode = "\x1b[38;2;" + std::to_string(fg.r) + ";" + std::to_string(fg.g) + ";" + std::to_string(fg.b) + "m";
    std::string bgCode = "\x1b[48;2;" + std::to_string(bg.r) + ";" + std::to_string(bg.g) + ";" + std::to_string(bg.b) + "m";
    std::string resetCode = "\x1b[0m";

    // Use double char for better aspect ratio
    return bgCode + fgCode + displayChar + displayChar + resetCode;
}

// --- File Info Display ---
bool displayFileInfo(const std::string &filepath)
{
    try
    {
        BinaryReader reader(filepath);
        FileHeader header = {};
        std::vector<ChunkIndexEntry> index;

        // Read Header
        std::streampos headerStartPos = reader.tell();
        if (!reader.read(header))
        {
            LOG_ERROR("Failed to read file header.");
            return false;
        }
        std::streampos headerEndPos = reader.tell();

        // Validate Header Checksum
        uint32_t calculatedHeaderChecksum = 0;
        try
        {
            size_t headerSizeToVerify = sizeof(FileHeader) - sizeof(uint32_t);
            std::vector<char> headerBytesBuffer(headerSizeToVerify);
            reader.seek(headerStartPos);
            if (reader.readBytes(headerBytesBuffer.data(), headerSizeToVerify) != headerSizeToVerify)
            {
                LOG_WARNING("Could not read header bytes for checksum verification.");
            }
            else
            {
                calculatedHeaderChecksum = calculateCRC32(headerBytesBuffer.data(), headerSizeToVerify);
            }
            reader.seek(headerEndPos);
        }
        catch (const std::exception &e)
        {
            LOG_WARNING("Exception during header checksum verification: " + std::string(e.what()));
            reader.seek(headerEndPos);
        }

        // Read Index
        if (header.indexOffset > 0 && header.indexOffset < reader.fileSize())
        {
            if (reader.seek(header.indexOffset))
            {
                size_t indexCount = 0;
                if (reader.read(indexCount) && indexCount > 0)
                {
                    index.resize(indexCount);
                    size_t indexBytesToRead = indexCount * sizeof(ChunkIndexEntry);
                    if (reader.readBytes(reinterpret_cast<char *>(index.data()), indexBytesToRead) != indexBytesToRead)
                    {
                        LOG_WARNING("Failed to read complete index data.");
                        index.clear();
                    }
                }
                else if (indexCount == 0)
                {
                    LOG_INFO("Index count is zero.");
                }
                else
                {
                    LOG_WARNING("Failed to read index count.");
                }
            }
            else
            {
                LOG_WARNING("Failed to seek to index offset specified in header.");
            }
        }
        else
        {
            LOG_WARNING("Invalid or zero index offset in header.");
        }

        // Print Info
        clearScreen();
        moveCursor(1, 1);
        std::cout << "--- TLWF File Information ---" << std::endl;
        std::cout << "File: " << filepath << std::endl;
        std::cout << "Size: " << reader.fileSize() << " bytes" << std::endl;
        std::cout << std::endl;
        std::cout << "[Header]" << std::endl;
        std::cout << "  Magic:      0x" << std::hex << header.magicNumber << std::dec
                  << " (" << (header.magicNumber == MAGIC_NUMBER ? "OK" : "Mismatch!") << ")" << std::endl;
        std::cout << "  Version:    " << (int)header.versionMajor << "." << (int)header.versionMinor << std::endl;
        std::cout << "  Endianness: " << (int)header.endianness
                  << " (" << (header.endianness == ENDIANNESS_LITTLE ? "Little" : (header.endianness == ENDIANNESS_BIG ? "Big" : "Unknown")) << ")" << std::endl;
        std::cout << "  Checksum:   " << (int)header.checksumType
                  << " (" << (header.checksumType == CHECKSUM_TYPE_CRC32 ? "CRC32" : (header.checksumType == CHECKSUM_TYPE_XOR ? "XOR" : "Unknown")) << ")" << std::endl;
        std::cout << "  Meta Offset:" << header.metadataOffset << std::endl;
        std::cout << "  Index Offset:" << header.indexOffset << std::endl;
        std::cout << "  Data Offset: " << header.dataOffset << std::endl;
        std::cout << "  Hdr Checksum: 0x" << std::hex << header.headerChecksum << std::dec;
        if (calculatedHeaderChecksum != 0)
        {
            std::cout << " (Calculated: 0x" << std::hex << calculatedHeaderChecksum << std::dec
                      << (calculatedHeaderChecksum == header.headerChecksum ? ", OK" : ", MISMATCH!") << ")";
        }
        std::cout << std::endl;

        std::cout << std::endl;
        std::cout << "[Chunk Index]" << std::endl;
        std::cout << "  Entries: " << index.size() << std::endl;
        if (!index.empty())
        {
            int min_cx = std::numeric_limits<int>::max(), max_cx = std::numeric_limits<int>::min();
            int min_cy = std::numeric_limits<int>::max(), max_cy = std::numeric_limits<int>::min();
            int min_cz = std::numeric_limits<int>::max(), max_cz = std::numeric_limits<int>::min();
            for (const auto &entry : index)
            {
                min_cx = std::min(min_cx, entry.cx);
                max_cx = std::max(max_cx, entry.cx);
                min_cy = std::min(min_cy, entry.cy);
                max_cy = std::max(max_cy, entry.cy);
                min_cz = std::min(min_cz, entry.cz);
                max_cz = std::max(max_cz, entry.cz);
            }
            std::cout << "  Chunk Coords Range:" << std::endl;
            std::cout << "    CX: " << min_cx << " to " << max_cx << std::endl;
            std::cout << "    CY: " << min_cy << " to " << max_cy << std::endl;
            std::cout << "    CZ: " << min_cz << " to " << max_cz << std::endl;
        }

        return true;
    }
    catch (const std::exception &e)
    {
        clearScreen();
        moveCursor(1, 1);
        LOG_ERROR("Error reading file info: " + std::string(e.what()));
        std::cerr << "Error reading file info: " << e.what() << std::endl;
        return false;
    }
}

// --- TUI Viewer ---
// --- TUI Viewer ---
void runTuiViewer(Map &map)
{
    // 关闭IO同步以提高性能
    std::ios::sync_with_stdio(false);
    std::cout.tie(nullptr);
    std::cin.tie(nullptr); // cin is not used in the loop

    clearScreen();
    hideCursor();

    // --- State ---
    int viewWidth = 64;  // Width of the map view area
    int viewHeight = 48; // Height of the map view area
    int viewX = 0;       // Top-left world X coordinate of the view
    int viewY = 0;       // Top-left world Y coordinate of the view

    // Find available Z layers and initial Z
    std::set<int> availableZLayers;
    int minZ = std::numeric_limits<int>::max();
    int maxZ = std::numeric_limits<int>::min();
    if (map.getLoadedChunkCount() > 0)
    {
        for (auto it = map.begin(); it != map.end(); ++it)
        {
            int cz = it->first.cz;
            for (int lz = 0; lz < CHUNK_DEPTH; ++lz)
            {
                int worldZ = cz * CHUNK_DEPTH + lz;
                availableZLayers.insert(worldZ);
                minZ = std::min(minZ, worldZ);
                maxZ = std::max(maxZ, worldZ);
            }
        }
        if (availableZLayers.empty())
        {
            minZ = 0;
            maxZ = 0;
            availableZLayers.insert(0);
        }
    }
    else
    {
        minZ = 0;
        maxZ = 0;
        availableZLayers.insert(0);
    }
    int currentZ = std::clamp(0, minZ, maxZ);

    // --- UI Layout Constants ---
    const int layerBarRow = 1;
    const int mapRowStart = layerBarRow + 2;
    const int mapColStart = 5; // Leave space for Y-axis labels
    const int infoRow = mapRowStart + viewHeight + 1;

    bool running = true;
    bool needsRedraw = true; // Start with a redraw needed

    // Variables to track key state for layer changes (prevent rapid changes)
    bool leftArrowPressedLastFrame = false;
    bool rightArrowPressedLastFrame = false;

    std::stringstream frameBuffer; // Use a stringstream to build the frame

    while (running)
    {
        // --- Input Handling (using GetAsyncKeyState) ---
        bool stateChanged = false;

        // Check movement keys (W, A, S, D)
        // 0x8000 is the bit indicating the key is currently down
        if (GetAsyncKeyState('W') & 0x8000)
        {
            viewY--;
            stateChanged = true;
        }
        if (GetAsyncKeyState('S') & 0x8000)
        {
            viewY++;
            stateChanged = true;
        }
        if (GetAsyncKeyState('A') & 0x8000)
        {
            viewX--;
            stateChanged = true;
        }
        if (GetAsyncKeyState('D') & 0x8000)
        {
            viewX++;
            stateChanged = true;
        }

        // Check layer change keys (Left/Right Arrow) with state tracking
        bool leftArrowCurrentlyPressed = (GetAsyncKeyState(VK_LEFT) & 0x8000);
        if (leftArrowCurrentlyPressed && !leftArrowPressedLastFrame)
        { // Trigger on press down edge
            auto it = availableZLayers.find(currentZ);
            if (it != availableZLayers.begin())
            {
                currentZ = *(--it);
                stateChanged = true;
            }
        }
        leftArrowPressedLastFrame = leftArrowCurrentlyPressed;

        bool rightArrowCurrentlyPressed = (GetAsyncKeyState(VK_RIGHT) & 0x8000);
        if (rightArrowCurrentlyPressed && !rightArrowPressedLastFrame)
        { // Trigger on press down edge
            auto it = availableZLayers.find(currentZ);
            if (it != availableZLayers.end())
            {
                ++it;
                if (it != availableZLayers.end())
                {
                    currentZ = *it;
                    stateChanged = true;
                }
            }
        }
        rightArrowPressedLastFrame = rightArrowCurrentlyPressed;

        // Check quit key (Q)
        if (GetAsyncKeyState('Q') & 0x8000)
        {
            running = false;
            // No need to redraw if quitting immediately
        }

        // --- Redrawing Logic ---
        if (stateChanged)
        {
            needsRedraw = true;
        }

        if (needsRedraw && running)
        { // Only redraw if needed and not quitting
            frameBuffer.str(""); // Clear the buffer for the new frame

            // --- Draw Layer Selector ---
            frameBuffer << "\x1b[" << layerBarRow << ";1H"; // Move cursor using escape code in buffer
            std::string layerText = "Layer (<-/->): ";
            std::stringstream ssLayers;
            int displayCount = 0;
            const int maxLayersToShow = 10;
            auto it_z = availableZLayers.lower_bound(currentZ - maxLayersToShow / 2);
            int countBefore = 0;
            auto temp_it = it_z;
            while (temp_it != availableZLayers.begin() && countBefore < maxLayersToShow / 2)
            {
                --temp_it;
                ++countBefore;
            }
            if (countBefore < maxLayersToShow / 2)
            {
                it_z = availableZLayers.begin();
            }

            for (; it_z != availableZLayers.end() && displayCount < maxLayersToShow; ++it_z)
            {
                if (*it_z == currentZ)
                    ssLayers << "\x1b[7m"; // Inverse
                ssLayers << " " << *it_z << " ";
                if (*it_z == currentZ)
                    ssLayers << "\x1b[0m"; // Reset
                displayCount++;
            }
            layerText += ssLayers.str();
            frameBuffer << layerText << std::string(80 - layerText.length(), ' '); // Pad

            // --- Draw Axes ---
            // Y-axis
            for (int i = 0; i < viewHeight; ++i)
            {
                frameBuffer << "\x1b[" << (mapRowStart + i) << ";1H"; // Move cursor
                frameBuffer << std::setw(3) << (viewY + i);
            }
            // X-axis
            frameBuffer << "\x1b[" << (mapRowStart - 1) << ";" << mapColStart << "H"; // Move cursor
            std::stringstream ssXAxis;
            for (int i = 0; i < viewWidth; ++i)
            {
                int xCoord = viewX + i;
                int xDisplay = (xCoord % 100 + 100) % 100; // Positive modulo 100
                ssXAxis << std::setw(2) << std::setfill('0') << xDisplay;
            }
            frameBuffer << ssXAxis.str() << std::setfill(' ');

            // --- Draw Map View ---
            for (int y = 0; y < viewHeight; ++y)
            {
                // Move cursor to the beginning of the map line *once* per line
                frameBuffer << "\x1b[" << (mapRowStart + y) << ";" << mapColStart << "H";
                for (int x = 0; x < viewWidth; ++x)
                {
                    int wx = viewX + x;
                    int wy = viewY + y;
                    int wz = currentZ;

                    try
                    {
                        const Tile &tile = map.getTile(wx, wy, wz);
                        // Append tile string directly to buffer, no individual cout
                        frameBuffer << formatTileForTerminal(tile);
                    }
                    catch (const std::out_of_range &oor) { frameBuffer << "\x1b[41mOR\x1b[0m"; }
                    catch (const std::runtime_error &rte) { frameBuffer << "\x1b[41mRE\x1b[0m"; }
                    catch (const std::exception &e) { frameBuffer << "\x1b[41mEX\x1b[0m"; }
                }
            }

            // --- Draw Info ---
            frameBuffer << "\x1b[" << infoRow << ";1H"; // Move cursor
            std::string infoText = "Coords: (X=" + std::to_string(viewX) +
                                   ", Y=" + std::to_string(viewY) +
                                   ", Z=" + std::to_string(currentZ) +
                                   ")  |  WASD: Move, <-/->: Change Layer, Q: Quit";
            frameBuffer << infoText << std::string(80 - infoText.length(), ' '); // Pad

            // --- Output the entire frame buffer at once ---
            std::cout << frameBuffer.str() << std::flush; // Single output + flush

            needsRedraw = false; // Redraw finished
        } // End if(needsRedraw && running)

        // --- Add a small delay to prevent high CPU usage and overly fast movement ---
        // Sleep(0); // Yield CPU time, prevents 100% usage

    } // End while(running)

    showCursor();
    clearScreen(); // Clear screen on exit
}

// --- Main Function ---
int main(int argc, char *argv[])
{
    if (!TilelandWorld::Logger::getInstance().initialize("tlwf_viewer.log"))
    {
        std::cerr << "Failed to initialize logger." << std::endl;
    }

    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <filepath.tlwf>" << std::endl;
        LOG_ERROR("Incorrect number of arguments.");
        TilelandWorld::Logger::getInstance().shutdown();
        return 1;
    }

    std::string filepath = argv[1];
    LOG_INFO("Starting TLWF Viewer for file: " + filepath);

    setupConsole(); // Enable ANSI codes and UTF-8

    // 1. Display File Info
    if (!displayFileInfo(filepath))
    {
        std::cout << "\nPress Enter to exit." << std::endl;
        // Use _getch() here for simple blocking input for the menu
        while (_kbhit())
            _getch(); // Clear potential leftover input buffer
        _getch();
        TilelandWorld::Logger::getInstance().shutdown();
        return 1;
    }

    // 2. Ask user action
    std::cout << "\nOptions: [V]iew Map TUI / [C]reate/View with Noise / [Q]uit" << std::endl;
    char choice = 0;
    bool useNoiseGenerator = false;
    while (choice != 'v' && choice != 'q' && choice != 'c')
    {
        // Use _getch() here for simple blocking input for the menu
        choice = tolower(_getch());
    }

    if (choice == 'q')
    {
        clearScreen();
        LOG_INFO("User chose to quit.");
        TilelandWorld::Logger::getInstance().shutdown();
        return 0;
    }

    if (choice == 'c')
    {
        useNoiseGenerator = true;
        LOG_INFO("User chose Create/View with Noise Generator.");
    }
    else
    {
        LOG_INFO("User chose View Map TUI (read-only).");
    }

    // 3. Load Map for TUI
    LOG_INFO("Loading map data for TUI...");
    std::unique_ptr<Map> map = MapSerializer::loadMap(filepath);

    if (!map)
    {
        clearScreen();
        moveCursor(1, 1);
        LOG_ERROR("Failed to load map data from file for TUI.");
        std::cerr << "Error: Failed to load map data from '" << filepath << "' for viewing." << std::endl;
        std::cout << "\nPress Enter to exit." << std::endl;
        while (_kbhit())
            _getch(); // Clear potential leftover input buffer
        _getch();
        TilelandWorld::Logger::getInstance().shutdown();
        return 1;
    }
    LOG_INFO("Map loaded successfully.");

    // 3.5. (Optional) Replace generator if 'C' was chosen
    if (useNoiseGenerator)
    {
        LOG_INFO("Replacing map terrain generator with FastNoiseTerrainGenerator.");
        try
        {
            auto noiseGenerator = std::make_unique<TilelandWorld::FastNoiseTerrainGenerator>(
                1337, 0.025f, "OpenSimplex2", "FBm", 5, 2.0f, 0.5f);
            map->setTerrainGenerator(std::move(noiseGenerator));
            LOG_INFO("FastNoiseTerrainGenerator set successfully.");
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to create or set FastNoiseTerrainGenerator: " + std::string(e.what()));
            std::cerr << "Error setting up noise generator: " << e.what() << std::endl;
            std::cout << "\nWarning: Could not set noise generator. Proceeding with default.\nPress Enter to continue." << std::endl;
            while (_kbhit())
                _getch(); // Clear potential leftover input buffer
            _getch();
        }
    }

    // 4. Run TUI Viewer
    LOG_INFO("Entering TUI mode.");
    runTuiViewer(*map); // Pass the potentially modified map (non-const)

    LOG_INFO("Exited TUI mode.");
    TilelandWorld::Logger::getInstance().shutdown();
    return 0;
}