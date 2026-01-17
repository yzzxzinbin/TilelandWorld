#include "FastNoiseTerrainGenerator.h"
#include "../Constants.h"
#include "../TerrainTypes.h"
#include "../Utils/Logger.h"

#include <vector>
#include <string>
#include <stdexcept> // For std::runtime_error
#include <memory>    // For std::unique_ptr (though SmartNode handles ownership)

// --- Include necessary FastNoise headers ---
#include <FastNoise/Generators/BasicGenerators.h> // Contains Perlin, OpenSimplex2, Value
#include <FastNoise/Generators/Cellular.h>        // Contains CellularDistance, CellularValue etc.
#include <FastNoise/Generators/Fractal.h>         // Contains FractalFBm, FractalRidged
#include <FastNoise/FastNoise.h>                  // Main header
#include <FastSIMD/FastSIMD.h>                    // *** Include for FastSIMD levels ***

namespace TilelandWorld
{

    // --- FastNoiseTerrainGenerator Implementation ---

    FastNoiseTerrainGenerator::FastNoiseTerrainGenerator(
        int seed, float frequency, const std::string &noiseTypeStr, const std::string &fractalTypeStr,
        int octaves, float lacunarity, float gain)
        : seed(seed), frequency(frequency)
{
    // Normalize and trim inputs
    auto trim = [](std::string s) {
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
        return s.substr(start, end - start);
    };

    std::string noiseType = trim(noiseTypeStr);
    std::string fractalType = trim(fractalTypeStr);

    if (noiseType.empty()) {
        noiseType = "Perlin";
    }
    if (fractalType == "None") {
        fractalType.clear();
    }

    auto toLower = [](const std::string &s){ std::string r = s; std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return r; };
    std::string noiseLower = toLower(noiseType);
    std::string fractalLower = toLower(fractalType);

    // Log the configuration attempt
    LOG_INFO("Configuring FastNoiseTerrainGenerator:");
        LOG_INFO("  Seed: " + std::to_string(seed));
        LOG_INFO("  Frequency: " + std::to_string(frequency));
        LOG_INFO("  Base Noise: '" + noiseType + "'");
        if (!fractalType.empty())
        {
            LOG_INFO("  Fractal Modifier: '" + fractalType + "' (Maps to internal FastNoise type)");
            LOG_INFO("    Octaves: " + std::to_string(octaves));
            LOG_INFO("    Lacunarity: " + std::to_string(lacunarity));
            LOG_INFO("    Gain: " + std::to_string(gain));
        }
        else
        {
            LOG_INFO("  Fractal Modifier: None");
        }
        
        try
        {
            // --- Specify target SIMD level ---
            // *** Try forcing SSE4.1 (Level 4) ***
            // You could also try FastSIMD::Level_SSE2 (Level 4) if SSE4.1 fails

            LOG_INFO("Requesting FastNoise nodes with SIMD level: SSE4.1 (32)");

            // --- Create Base Noise Node ---
            FastNoise::SmartNode<> baseNoiseNode;
            if (noiseLower == "perlin")
            {
                baseNoiseNode = FastNoise::New<FastNoise::Perlin>(targetLevel);
            }
            else if (noiseLower == "opensimplex2" || noiseLower == "open simplex2" || noiseLower == "open_simplex2")
            {
                baseNoiseNode = FastNoise::New<FastNoise::OpenSimplex2>(targetLevel);
            }
            else if (noiseLower == "value")
            {
                baseNoiseNode = FastNoise::New<FastNoise::Value>(targetLevel);
            }
            else if (noiseLower == "cellulardistance" || noiseLower == "cellular_distance" || noiseLower == "cellular distance")
            {
                baseNoiseNode = FastNoise::New<FastNoise::CellularDistance>(targetLevel);
            }
            else if (noiseLower == "cellularvalue" || noiseLower == "cellular_value" || noiseLower == "cellular value")
            {
                baseNoiseNode = FastNoise::New<FastNoise::CellularValue>(targetLevel);
            }
            else
            {
                throw std::runtime_error("Unsupported base noise type: " + noiseType);
            }

            if (!baseNoiseNode)
            { // Sanity check after creation
                throw std::runtime_error("Failed to create base noise node of type: " + noiseType);
            }
            LOG_INFO("Base noise node '" + noiseType + "' created.");

            // --- Create and Configure Fractal Node (if requested) ---
            if (!fractalType.empty())
            {
                FastNoise::SmartNode<> finalNode; // This will hold the configured fractal node

                if (fractalLower == "fbm")
                {
                    auto fbmNode = FastNoise::New<FastNoise::FractalFBm>(targetLevel);
                    if (!fbmNode)
                        throw std::runtime_error("Failed to create FractalFBm node.");
                    fbmNode->SetSource(baseNoiseNode);
                    fbmNode->SetOctaveCount(octaves);
                    fbmNode->SetLacunarity(lacunarity);
                    fbmNode->SetGain(gain);
                    finalNode = fbmNode;
                    LOG_INFO("FractalFBm node created and configured for input type 'FBm'.");
                }
                else if (fractalLower == "ridged")
                {
                    auto ridgedNode = FastNoise::New<FastNoise::FractalRidged>(targetLevel);
                    if (!ridgedNode)
                        throw std::runtime_error("Failed to create FractalRidged node.");
                    ridgedNode->SetSource(baseNoiseNode);
                    ridgedNode->SetOctaveCount(octaves);
                    ridgedNode->SetLacunarity(lacunarity);
                    ridgedNode->SetGain(gain);
                    finalNode = ridgedNode;
                    LOG_INFO("FractalRidged node created and configured for input type 'Ridged'.");
                }
                // Add else if blocks for other fractal types here...
                else
                {
                    throw std::runtime_error("Unsupported fractal type string provided: " + fractalType);
                }

                if (!finalNode)
                {
                    throw std::runtime_error("Fractal node configuration failed for type: " + fractalType);
                }
                this->noiseSource = finalNode;
            }
            else
            {
                this->noiseSource = baseNoiseNode;
                LOG_INFO("Using base noise node directly (no fractal).");
            }

            // --- Validate actual created SIMD level ---
            if (this->noiseSource)
            {
                FastSIMD::eLevel actualLevel = this->noiseSource->GetSIMDLevel();
                LOG_INFO("Actual SIMD level of created noiseSource: " + std::to_string(actualLevel));
                if (actualLevel != targetLevel)
                {
                    // Log a warning, but maybe don't throw if a lower level was created?
                    // Or throw if it's higher than requested (shouldn't happen here)
                    LOG_WARNING("SIMD level mismatch after creation! Requested " + std::to_string(targetLevel) + ", got " + std::to_string(actualLevel));
                    // Decide if this is a fatal error for your application
                    // throw std::runtime_error("Failed to enforce requested SIMD level during node creation.");
                }
            }
            else
            {
                LOG_ERROR("CRITICAL FAILURE: noiseSource is null after node creation attempt!");
                throw std::runtime_error("Failed to create noise source node.");
            }
        }
        catch (const std::runtime_error &e)
        {
            LOG_ERROR("Failed to initialize FastNoise: " + std::string(e.what()));
            LOG_ERROR("Parameters: noiseType='" + noiseType + "' (len=" + std::to_string(noiseType.size()) + ") fractal='" + fractalType + "' (len=" + std::to_string(fractalType.size()) + ") seed=" + std::to_string(seed));
            LOG_WARNING("Falling back to default Perlin noise configuration (SSE4.1)."); // Updated log
            // *** Ensure fallback also uses targetLevel (SSE4.1) ***
            this->noiseSource = FastNoise::New<FastNoise::Perlin>(targetLevel);
            if (!this->noiseSource)
            { // Check fallback immediately
                LOG_ERROR("CRITICAL FAILURE: Fallback to Perlin noise (SSE4.1) also failed!");
                // Try Scalar as a last resort?
                LOG_WARNING("Attempting last resort fallback to Perlin noise (Scalar).");
                this->noiseSource = FastNoise::New<FastNoise::Perlin>(FastSIMD::Level_Scalar);
            }
        }
        catch (...)
        {
            LOG_ERROR("An unknown error occurred during FastNoise initialization.");
            LOG_ERROR("Parameters: noiseType='" + noiseType + "' (len=" + std::to_string(noiseType.size()) + ") fractal='" + fractalType + "' (len=" + std::to_string(fractalType.size()) + ") seed=" + std::to_string(seed));
            LOG_WARNING("Falling back to default Perlin noise configuration (SSE4.1)."); // Updated log
            // *** Ensure fallback also uses targetLevel (SSE4.1) ***
            this->noiseSource = FastNoise::New<FastNoise::Perlin>(targetLevel);
            if (!this->noiseSource)
            { // Check fallback immediately
                LOG_ERROR("CRITICAL FAILURE: Fallback to Perlin noise (SSE4.1) also failed!");
                LOG_WARNING("Attempting last resort fallback to Perlin noise (Scalar).");
                this->noiseSource = FastNoise::New<FastNoise::Perlin>(FastSIMD::Level_Scalar);
            }
        }

        // Final sanity check
        if (!this->noiseSource)
        {
            LOG_ERROR("CRITICAL FAILURE: noiseSource is null after initialization and all fallback attempts!");
            throw std::runtime_error("Failed to initialize noise source even with fallbacks.");
        }
    }

    // --- generateChunk Method ---
    void FastNoiseTerrainGenerator::generateChunk(Chunk &chunk) const
    {
        if (!noiseSource)
        {
            LOG_ERROR("Cannot generate chunk: FastNoise source is not valid.");
            // Fill with default pattern or return early
            for (int lz = 0; lz < CHUNK_DEPTH; ++lz)
            { /* ... fill with UNKNOWN ... */
            }
            return;
        }

        int baseWX = chunk.getChunkX() * CHUNK_WIDTH;
        int baseWY = chunk.getChunkY() * CHUNK_HEIGHT;
        int baseWZ = chunk.getChunkZ() * CHUNK_DEPTH;

        std::vector<float> noiseOutput(CHUNK_VOLUME);

        // --- Runtime SIMD Level Check (Optional but good for verification) ---
        FastSIMD::eLevel detectedLevel = noiseSource->GetSIMDLevel();
        // LOG_INFO("Runtime SIMD level: " + std::to_string(detectedLevel));
        // *** Check against the requested SSE4.1 level ***
        if (detectedLevel != targetLevel)
        {
            // Log potentially non-fatal mismatch if constructor fallback occurred
            LOG_WARNING("SIMD level mismatch detected at runtime!  got: " + std::to_string(detectedLevel));
            // If the level is lower (e.g., Scalar fallback), maybe allow it?
            // If it's higher (shouldn't happen), it's an error.
            // For now, let's just log and continue, assuming the constructor handled fallbacks.
            // If strict SSE4.1 is required, uncomment the return:
            // return;
        }
        // --- End Runtime Check ---

        // Generate 3D noise grid using the configured noiseSource
        // This should now call the SSE4.1 (or fallback) version of GenUniformGrid3D
        noiseSource->GenUniformGrid3D(noiseOutput.data(),
                                      baseWX, baseWY, baseWZ,
                                      CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH,
                                      this->frequency, this->seed);

        // Map noise to terrain
        for (int lz = 0; lz < CHUNK_DEPTH; ++lz)
        {
            int currentWZ = baseWZ + lz;
            for (int ly = 0; ly < CHUNK_HEIGHT; ++ly)
            {
                for (int lx = 0; lx < CHUNK_WIDTH; ++lx)
                {
                    int index = lx + ly * CHUNK_WIDTH + lz * (CHUNK_WIDTH * CHUNK_HEIGHT);
                    float noiseValue = noiseOutput[index];

                    TerrainType currentType = mapNoiseToTerrain(noiseValue, currentWZ);
                    const auto &props = getTerrainProperties(currentType);

                    Tile &tile = chunk.getLocalTile(lx, ly, lz);
                    tile.terrain = currentType;
                    tile.canEnterSameLevel = props.allowEnterSameLevel;
                    tile.canStandOnTop = props.allowStandOnTop;
                    tile.movementCost = props.defaultMovementCost;
                    tile.lightLevel = MAX_LIGHT_LEVEL;
                    tile.isExplored = true;
                }
            }
        }
    }

    // --- mapNoiseToTerrain Method ---
    TerrainType FastNoiseTerrainGenerator::mapNoiseToTerrain(float noiseValue, int worldZ) const
    {
        // Example mapping logic (adjust thresholds and complexity)
        if (worldZ < -5)
        {
            return TerrainType::WALL;
        }
        else if (worldZ < 0)
        { // Underground caves
            if (noiseValue < -0.5f)
                return TerrainType::WATER; // Underground lake/river
            else if (noiseValue > 0.4f)
                return TerrainType::WALL; // Cave wall
            else
                return TerrainType::FLOOR; // Cave floor
        }
        else if (worldZ == 0)
        { // Surface level
            if (noiseValue < -0.3f)
                return TerrainType::WATER; // Water body
            else if (noiseValue < 0.3f)
                return TerrainType::GRASS; // Grassland
            else
                return TerrainType::WALL; // Hills/Mountains base
        }
        else if (worldZ < 5)
        { // Low altitude above surface
            if (noiseValue > 0.6f)
                return TerrainType::WALL; // Mountain peaks? Floating islands?
            else
                return TerrainType::VOIDBLOCK; // Air
        }
        else
        {                                  // High altitude
            return TerrainType::VOIDBLOCK; // Air/Void
        }
    }

} // namespace TilelandWorld