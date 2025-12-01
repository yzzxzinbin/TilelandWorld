#include "ChunkGeneratorPool.h"
#include "../Utils/Logger.h"

namespace TilelandWorld {

    ChunkGeneratorPool::ChunkGeneratorPool(const Map& mapRef, TaskSystem& taskSystemRef) 
        : map(mapRef), taskSystem(taskSystemRef) {
        // 不再创建线程
    }

    void ChunkGeneratorPool::requestChunk(int cx, int cy, int cz) {
        // 构造一个 lambda 任务
        // 注意：必须按值捕获 cx, cy, cz
        // 注意：捕获 this 指针，因此必须确保 ChunkGeneratorPool 的生命周期长于任务执行时间
        taskSystem.submit([this, cx, cy, cz]() {
            // 1. 执行耗时的生成操作 (在工作线程中)
            // createChunkIsolated 内部已经包含了计时日志和异常处理(部分)
            // 但为了安全，TaskSystem 也会捕获异常
            auto chunk = map.createChunkIsolated(cx, cy, cz);
            
            // 2. 将结果放入完成队列
            {
                std::lock_guard<std::mutex> lock(finishedMutex);
                finishedQueue.push_back(std::move(chunk));
            }
        });
    }

    std::vector<std::unique_ptr<Chunk>> ChunkGeneratorPool::getFinishedChunks() {
        std::vector<std::unique_ptr<Chunk>> result;
        {
            std::lock_guard<std::mutex> lock(finishedMutex);
            if (finishedQueue.empty()) return result;
            result.swap(finishedQueue);
        }
        return result;
    }

} // namespace TilelandWorld
