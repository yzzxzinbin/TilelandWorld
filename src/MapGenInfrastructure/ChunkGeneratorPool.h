#pragma once
#ifndef TILELANDWORLD_CHUNKGENERATORPOOL_H
#define TILELANDWORLD_CHUNKGENERATORPOOL_H

#include "../Map.h"
#include "../Chunk.h"
#include "../Utils/TaskSystem.h" // 引入通用任务系统
#include <vector>
#include <mutex>
#include <memory>

namespace TilelandWorld {

    /**
     * @brief 区块生成任务管理器。
     * 
     * 它不再拥有自己的线程，而是将生成请求打包成任务提交给全局 TaskSystem。
     * 它负责收集生成好的区块结果。
     */
    class ChunkGeneratorPool {
    public:
        // 构造函数：需要传入 Map 和 TaskSystem
        ChunkGeneratorPool(const Map& map, TaskSystem& taskSystem);
        ~ChunkGeneratorPool() = default;

        // 请求生成一个区块 (提交到 TaskSystem)
        void requestChunk(int cx, int cy, int cz);

        // 获取所有已完成的区块
        std::vector<std::unique_ptr<Chunk>> getFinishedChunks();

        // 获取当前待处理的请求数量 (注意：这里只能统计 TaskSystem 中尚未被取走的任务，比较困难，
        // 简化为不提供或仅提供本地计数，这里暂时移除 getPendingCount 以简化，
        // 因为实际 pending 状态由 Controller 的 pendingChunks 集合管理)
        // size_t getPendingCount() const; 

    private:
        const Map& map;
        TaskSystem& taskSystem; // 引用全局任务系统

        // 完成队列
        std::vector<std::unique_ptr<Chunk>> finishedQueue;
        mutable std::mutex finishedMutex;
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_CHUNKGENERATORPOOL_H
