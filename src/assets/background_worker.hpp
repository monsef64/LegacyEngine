#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

// A bounded single-worker queue. Jobs never touch Vulkan or ECS state. Futures
// returned by packaged_task can be discarded without waiting for a running job.
class BackgroundWorker {
  public:
    explicit BackgroundWorker(std::size_t queueLimit = 2)
        : limit_(queueLimit), worker_([this] { run(); }) {}
    ~BackgroundWorker() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            queue_.clear();
        }
        ready_.notify_one();
        worker_.join();
    }
    BackgroundWorker(const BackgroundWorker&) = delete;
    BackgroundWorker& operator=(const BackgroundWorker&) = delete;

    template <class F> auto submit(F&& job) -> std::optional<std::future<std::invoke_result_t<F>>> {
        using Result = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(job));
        auto result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || queue_.size() >= limit_) {
                return std::nullopt;
            }
            queue_.emplace_back([task] { (*task)(); });
        }
        ready_.notify_one();
        return std::optional<std::future<Result>>(std::move(result));
    }
    // Queued work is abandoned immediately; an executing task is allowed to
    // finish using its own captured inputs and its result can be discarded.
    void cancelQueued() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

  private:
    void run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_) {
                    return;
                }
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            job();
        }
    }
    std::size_t limit_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> queue_;
    bool stopping_ = false;
    std::thread worker_;
};
