#include "ChunkGeneratorPool.h"
#include "../Utils/Logger.h"

namespace TilelandWorld {

    ChunkGeneratorPool::ChunkGeneratorPool(const Map& mapRef, int threadCount) 
        : map(mapRef) {
        
        if (threadCount <= 0) {
            // 至少保留一个核心给主逻辑/渲染，最少开启1个生成线程
            threadCount = std::max(1u, std::thread::hardware_concurrency() - 2);
        }

        LOG_INFO("Initializing ChunkGeneratorPool with " + std::to_string(threadCount) + " threads.");

        for (int i = 0; i < threadCount; ++i) {
            workers.emplace_back(&ChunkGeneratorPool::workerThread, this);
        }
    }

    ChunkGeneratorPool::~ChunkGeneratorPool() {
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            running = false;
        }
        requestCv.notify_all();

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void ChunkGeneratorPool::requestChunk(int cx, int cy, int cz) {
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            requestQueue.push({cx, cy, cz});
        }
        requestCv.notify_one();
    }

    std::vector<std::unique_ptr<Chunk>> ChunkGeneratorPool::getFinishedChunks() {
        std::vector<std::unique_ptr<Chunk>> result;
        {
            std::lock_guard<std::mutex> lock(finishedMutex);
            if (finishedQueue.empty()) return result;
            // 快速交换，最小化锁持有时间
            result.swap(finishedQueue);
        }
        return result;
    }

    size_t ChunkGeneratorPool::getPendingCount() const {
        std::lock_guard<std::mutex> lock(requestMutex);
        return requestQueue.size();
    }

    void ChunkGeneratorPool::workerThread() {
        while (true) {
            Request req;
            {
                std::unique_lock<std::mutex> lock(requestMutex);
                requestCv.wait(lock, [this] { return !requestQueue.empty() || !running; });

                if (!running && requestQueue.empty()) {
                    return;
                }

                if (requestQueue.empty()) continue;

                req = requestQueue.front();
                requestQueue.pop();
            }

            // 执行耗时的生成操作 (无锁，并行)
            try {
                auto chunk = map.createChunkIsolated(req.cx, req.cy, req.cz);
                
                {
                    std::lock_guard<std::mutex> lock(finishedMutex);
                    finishedQueue.push_back(std::move(chunk));
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Error generating chunk in worker thread: " + std::string(e.what()));
            }
        }
    }

} // namespace TilelandWorld
