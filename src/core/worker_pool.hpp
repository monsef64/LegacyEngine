#pragma once
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

// CPU-only jobs. Tasks own their inputs/results; never capture a renderer/ECS or
// issue graphics calls here. Bounded admission prevents one thread per asset.
inline unsigned backgroundWorkerCount() {
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    return std::clamp((cores - 1) / 2, 1u, 4u);
}
class WorkerPool {
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
    WorkerPool() {
        try {
            for (unsigned i = 0; i < backgroundWorkerCount(); ++i) {
                workers_.emplace_back([this] {
                    for (;;) {
                        std::function<void()> job;
                        {
                            std::unique_lock<std::mutex> lock(mutex_);
                            changed_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                            if (stopping_ && queue_.empty()) {
                                return;
                            }
                            job = std::move(queue_.front());
                            queue_.pop_front();
                        }
                        job(); // packaged_task captures exceptions in its future.
                    }
                });
            }
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
            }
            changed_.notify_all();
            for (auto& worker : workers_) {
                worker.join();
            }
            throw;
        }
    }

  public:
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    ~WorkerPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_all();
        for (auto& worker : workers_) {
            worker.join();
        }
    }
    static WorkerPool& shared() {
        static WorkerPool pool;
        return pool;
    }
    template <class Function>
    auto submit(Function function) -> std::optional<std::future<std::invoke_result_t<Function>>> {
        using Result = std::invoke_result_t<Function>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::move(function));
        auto future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || queue_.size() >= 32) {
                return std::nullopt;
            }
            queue_.emplace_back([task] { (*task)(); });
        }
        changed_.notify_one();
        return std::optional<std::future<Result>>(std::move(future));
    }
};
