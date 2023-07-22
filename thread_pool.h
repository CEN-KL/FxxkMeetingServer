//
// Created by CEN-KL on 2023/7/22.
//

#ifndef CPPDRAFT_THREAD_POOL_H
#define CPPDRAFT_THREAD_POOL_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>
#include <functional>

template<typename T>
class SafeQueue {
    std::queue<T> m_queue;
    mutable std::mutex m_mtx;
    std::condition_variable m_cv;
    bool m_stop = false;
public:
    SafeQueue() = default;

    SafeQueue(const SafeQueue&) = delete;
    SafeQueue &operator=(const SafeQueue&) = delete;

    void push(T &&t) {
        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_queue.push(std::forward<T>(t));
        }
        m_cv.notify_one();
    }

    bool pop(T &t) {
        std::unique_lock<std::mutex> lock(m_mtx);
        m_cv.wait(lock, [&] { return m_stop || !m_queue.empty(); });
        if (!m_queue.empty()) {
            t = std::move(m_queue.front());
            m_queue.pop();
            return true;
        }
        return false;
    }

    bool empty() const {
        std::unique_lock<std::mutex> lock(m_mtx);
        return m_queue.empty();
    }

    size_t size() const {
        std::unique_lock<std::mutex> lock(m_mtx);
        return m_queue.size();
    }

    void stop() {
        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_stop = true;
        }
        m_cv.notify_all();
    }
};

class ThreadPool {
    using Task_t = std::function<void()>;
    std::vector<std::thread> m_workers;
    SafeQueue<Task_t> m_queue;
    size_t n_threads;
public:
    explicit ThreadPool(size_t _n = std::thread::hardware_concurrency()) : n_threads(_n) {
        for (size_t i = 0; i < n_threads; ++i) {
            m_workers.emplace_back([&] {
                for (;;) {
                    Task_t task;
                    if (!m_queue.pop(task))
                        return;
                    task();
                }
            });
        }
    }

    template<typename F, typename ... Args>
    auto submit(F &&f, Args &&... args) -> std::future<decltype(f(args...))>{
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        auto wrap_func = [task_ptr] { (*task_ptr)(); };
        m_queue.push(std::move(wrap_func));
        return task_ptr->get_future();
    }

    ~ThreadPool() {
        m_queue.stop();
        for (auto &w: m_workers)
            w.join();
    }

};

#endif //CPPDRAFT_THREAD_POOL_H
