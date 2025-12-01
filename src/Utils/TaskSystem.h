#pragma once
#ifndef TILELANDWORLD_TASKSYSTEM_H
#define TILELANDWORLD_TASKSYSTEM_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace TilelandWorld {

    /**
     * @brief 通用多线程任务系统。
     * 
     * 维护一个固定数量的工作线程池，处理所有类型的异步任务。
     * 任务以 std::function<void()> 的形式提交。
     */
    class TaskSystem {
    public:
        /**
         * @brief 构造函数。
         * @param threadCount 工作线程数量。如果为 -1，则自动设置为 (硬件核心数 - 1)。
         */
        explicit TaskSystem(int threadCount = -1);
        ~TaskSystem();

        /**
         * @brief 提交一个任务到队列。
         * @param task 一个无参数、无返回值的函数对象 (lambda, std::bind 等)。
         *             注意：任务内部应自行处理异常，避免导致工作线程崩溃。
         */
        void submit(std::function<void()> task);

        /**
         * @brief 停止所有线程并等待任务完成 (通常在析构时自动调用，也可手动调用)。
         */
        void stop();

    private:
        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;
        
        std::mutex queueMutex;
        std::condition_variable condition;
        std::atomic<bool> stopFlag{false};

        void workerThread();
    };

} // namespace TilelandWorld

#endif // TILELANDWORLD_TASKSYSTEM_H
