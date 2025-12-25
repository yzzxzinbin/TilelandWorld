#pragma once
#ifndef TILELANDWORLD_SAVEMETADATA_H
#define TILELANDWORLD_SAVEMETADATA_H

#include <string>
#include <cstdint>

namespace TilelandWorld {

struct WorldMetadata {
    int64_t seed{1337};
    float frequency{0.025f};
    std::string noiseType{"OpenSimplex2"};
    std::string fractalType{"FBm"};
    int octaves{5};
    float lacunarity{2.0f};
    float gain{0.5f};
};

} // namespace TilelandWorld

#endif // TILELANDWORLD_SAVEMETADATA_H
