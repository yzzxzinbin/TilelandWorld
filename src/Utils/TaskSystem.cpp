#include "TaskSystem.h"
#include "Logger.h"
#include <algorithm>

namespace TilelandWorld {

    TaskSystem::TaskSystem(int threadCount) {
        if (threadCount <= 0) {
            // 至少保留一个核心给主逻辑/渲染，最少开启1个工作线程
            threadCount = std::max(1u, std::thread::hardware_concurrency() - 1);
        }

        LOG_INFO("Initializing TaskSystem with " + std::to_string(threadCount) + " worker threads.");

        for (int i = 0; i < threadCount; ++i) {
            workers.emplace_back(&TaskSystem::workerThread, this);
        }
    }

    TaskSystem::~TaskSystem() {
        stop();
    }

    void TaskSystem::submit(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            tasks.emplace(std::move(task));
        }
        condition.notify_one();
    }

    void TaskSystem::stop() {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (stopFlag) return; // 已经停止
            stopFlag = true;
        }
        condition.notify_all();

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        LOG_INFO("TaskSystem stopped.");
    }

    void TaskSystem::workerThread() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                condition.wait(lock, [this] { return stopFlag || !tasks.empty(); });

                if (stopFlag && tasks.empty()) {
                    return;
                }

                if (tasks.empty()) continue;

                task = std::move(tasks.front());
                tasks.pop();
            }

            // 执行任务
            try {
                task();
            } catch (const std::exception& e) {
                LOG_ERROR("Exception in TaskSystem worker thread: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("Unknown exception in TaskSystem worker thread.");
            }
        }
    }

} // namespace TilelandWorld
