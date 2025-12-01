#pragma once
#ifndef TILELANDWORLD_CHUNKGENERATORPOOL_H
#define TILELANDWORLD_CHUNKGENERATORPOOL_H

#include "../Map.h"
#include "../Chunk.h"
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

namespace TilelandWorld {

    class ChunkGeneratorPool {
    public:
        // 构造函数：启动指定数量的工作线程 (默认: 硬件并发数 - 1)
        ChunkGeneratorPool(const Map& map, int threadCount = -1);
        ~ChunkGeneratorPool();

        // 请求生成一个区块 (非阻塞)
        void requestChunk(int cx, int cy, int cz);

        // 获取所有已完成的区块 (非阻塞，一次性取走所有)
        std::vector<std::unique_ptr<Chunk>> getFinishedChunks();

        // 获取当前待处理的请求数量 (用于调试/监控)
        size_t getPendingCount() const;

    private:
        const Map& map; // 引用 Map 以调用 createChunkIsolated
        std::vector<std::thread> workers;
        std::atomic<bool> running{true};

        // 请求队列
        struct Request { int cx, cy, cz; };
        std::queue<Request> requestQueue;
        mutable std::mutex requestMutex;
        std::condition_variable requestCv;

        // 完成队列
        std::vector<std::unique_ptr<Chunk>> finishedQueue;
        mutable std::mutex finishedMutex;

        // 工作线程函数
        void workerThread();
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_CHUNKGENERATORPOOL_H
