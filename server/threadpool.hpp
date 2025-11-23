#pragma once
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex mtx;
    std::condition_variable cv;
    bool stop = false;

public:
    explicit ThreadPool(size_t num_threads) {
        for (size_t i = 0; i < num_threads; i++) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;

                    // Wait until a task is available or pool is stopping
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        cv.wait(lock, [this]() {
                            return stop || !tasks.empty();
                        });

                        if (stop && tasks.empty())
                            return;

                        task = std::move(tasks.front());
                        tasks.pop();
                    }

                    task();  // execute task
                }
            });
        }
    }

    // Add new work to the queue
    void enqueue(std::function<void()> func) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            tasks.emplace(func);
        }
        cv.notify_one();
    }

    // Graceful shutdown
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            stop = true;
        }
        cv.notify_all();

        for (auto& t : workers)
            t.join();
    }
};
