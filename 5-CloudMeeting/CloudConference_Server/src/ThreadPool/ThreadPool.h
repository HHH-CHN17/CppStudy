/*******************************************************************************
* @author       LP
* @version:     1.0
* @date         25-1-26
* @description:
*******************************************************************************/

#ifndef CLOUDCONFERENCE_THREADPOOL_H
#define CLOUDCONFERENCE_THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <vector>
#include <unistd.h>
#include "MsgQueue.h"

//namespace Lanuch{
//    struct DEFAULT {
//
//    } Default;
//
//    struct UNARY {
//
//    } Unary;
//}
//enum class Lanuch
//{
//    Default = 1,
//    Unary = 2
//};

inline std::size_t default_thread_pool_size()noexcept {
    std::size_t num_threads = std::thread::hardware_concurrency() * 2;
    num_threads = num_threads == 0 ? 2 : num_threads;
    return num_threads;
}

template<size_t MAXQUEUELEN>
class ThreadPool {
private:
    using Task = std::packaged_task<void(void)>;
    using smart_ptr = typename lock_free_queue<Task, MAXQUEUELEN>::smart_ptr;

    std::mutex                  mutex_;
    std::condition_variable     cv_;
    std::atomic<bool>           stop_;
    std::atomic<std::size_t>    atnNumThread_;
    lock_free_queue<Task, MAXQUEUELEN>   tasks_;
    std::vector<std::thread>    pool_;

public:
     ThreadPool()
        : stop_{ true }, atnNumThread_{0 }, tasks_{}, pool_{} {

     };
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    explicit ThreadPool(std::size_t num_thread)
            : stop_{ false }, atnNumThread_{num_thread }, tasks_{}, pool_{} {
        start();
    }

    ~ThreadPool() {
        stop();
    }

    template<typename F, typename... Args>
    std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>
    submit(F&& f, Args&&...args) {
        using RetType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
        if (stop_.load()) {
            throw std::runtime_error("ThreadPool is stopped");
        }

        auto task = std::make_shared<std::packaged_task<RetType()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<RetType> ret = task->get_future();

        {
            std::lock_guard<std::mutex> lc{ mutex_ };
            //tasks_.emplace([task] {(*task)(); });
            Task t{ [task] { (*task)();} };
            tasks_.push(std::move(t));// 由于消息队列结合内存池实现，所以只能值赋值，如果按引用传递，则后续内存无法释放，造成内存泄漏。
        }
        cv_.notify_one();
        return ret;
    }

    void set_and_start(std::size_t num_thread = default_thread_pool_size()){
        if (stop_.load())
        {
            //printf("%d thread pool starting...\n", getpid());
            atnNumThread_.store(num_thread);
            stop_.store(false);
            start();
        }
    }

    void stop() {
        stop_.store(true);
        cv_.notify_all();
        for (auto& thread : pool_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        pool_.clear();
    }

private:
    void start() {
        for (std::size_t i = 0; i < atnNumThread_; ++i) {
            pool_.emplace_back([this] {
                while (!stop_) {
                    smart_ptr task;
                    {
                        std::unique_lock<std::mutex> lc{ mutex_ };
                        cv_.wait(lc, [this] {return stop_ || !tasks_.empty(); });
                        if (tasks_.empty())
                            break;
                        task = std::move(tasks_.pop());
                    }
                    (*task)();
                    printf("%d end running...\n", gettid());
                }
            });
        }
    }
};

#endif //CLOUDCONFERENCE_THREADPOOL_H
