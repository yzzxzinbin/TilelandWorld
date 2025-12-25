#pragma once
#ifndef TILELANDWORLD_TERRAINGENERATORFACTORY_H
#define TILELANDWORLD_TERRAINGENERATORFACTORY_H

#include "../SaveMetadata.h"
#include "TerrainGenerator.h"
#include <memory>

namespace TilelandWorld {

// 简单工厂：从 WorldMetadata 创建合适的地形生成器。
std::unique_ptr<TerrainGenerator> createTerrainGeneratorFromMetadata(const WorldMetadata& meta);

} // namespace TilelandWorld

#endif // TILELANDWORLD_TERRAINGENERATORFACTORY_H
