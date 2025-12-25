#include "TerrainGeneratorFactory.h"
#include "FastNoiseTerrainGenerator.h"
#include "FlatTerrainGenerator.h"
#include <algorithm>

namespace TilelandWorld {

std::unique_ptr<TerrainGenerator> createTerrainGeneratorFromMetadata(const WorldMetadata& meta) {
    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    std::string noise = toLower(meta.noiseType);
    if (noise.empty() || noise == "flat") {
        return std::make_unique<FlatTerrainGenerator>(0);
    }

    return std::make_unique<FastNoiseTerrainGenerator>(
        static_cast<int>(meta.seed),
        meta.frequency,
        meta.noiseType,
        meta.fractalType,
        meta.octaves,
        meta.lacunarity,
        meta.gain);
}

} // namespace TilelandWorld
