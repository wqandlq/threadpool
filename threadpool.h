//
// Created by 王强 on 2026/4/16.
//

#ifndef THREADPOOL_THREADPOOL_H
#define THREADPOOL_THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

#include <thread>
#include <future>
#include <type_traits>
#include <utility>
#include <stdexcept>


// 线程池支持的模式
enum PoolMode
{
    MODE_FIXED,  // 固定数量的线程
    MODE_CACHED, // 线程数量可动态增长
};

// 线程类型
class Thread
{
public:
    // 线程函数对象类型
    using ThreadFunc = std::function<void()>;

    // 线程构造
    Thread(ThreadFunc func);

    // 线程析构
    ~Thread();

    // 启动线程
    void start();

    void join();

private:
    ThreadFunc func_;

    std::thread thread_;
};

// 线程池类型
class ThreadPool
{
public:
    // 线程池构造
    ThreadPool();

    // 线程池析构
    ~ThreadPool();

    // 开启线程池
    void start(int initThreadSize = 4);

    void setThreadSizeThreshHold(int theshHold);

    // 设置线程池模式
    void setMode(PoolMode mode);

    // 设置task任务队列上限阈值
    void setTaskQueMaxThreshHold(int taskQueMaxThreshHold);

    // 等待所有任务完成
    void waitAllTaskDone();

    // 给线程池提交任务
    template <typename Func, typename... Args>
    auto submitTask(Func &&func, Args &&...args)
        -> std::future<std::invoke_result_t<Func, Args...>>
    {
        using RType = std::invoke_result_t<Func, Args...>;

        auto task = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);

        auto packagedTask = std::make_shared<std::packaged_task<RType()>>(std::move(task));

        std::future<RType> result = packagedTask->get_future();

        {
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            if (!isPoolRunning_)
            {
                throw std::runtime_error("thread pool is not running");
            }

            if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool
                                   { return tasksQue_.size() < (size_t)taskQueMaxThreshHold_ || !isPoolRunning_; }))
            {
                throw std::runtime_error("task queue is full, submit task failed");
            }

            if (!isPoolRunning_)
            {
                throw std::runtime_error("thread pool is not running");
            }

            tasksQue_.emplace([packagedTask]()
                              { (*packagedTask)(); });

            task_Size_++;

            // 如果线程池的模式是动态的，并且任务数量超过了空闲线程数量，并且当前线程数量没有超过线程数量上限，那么就创建一个新的线程
            if(poolMode_ ==PoolMode::MODE_CACHED &&task_Size_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_)
            {
                std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
                threads_.emplace_back(std::move(ptr));
                threads_.back()->start();
                curThreadSize_++;
                idleThreadSize_++;
            }
        }

        notEmpty_.notify_one();
        return result;
    }

    // 设置初始的线程数量
    //  void setInitThreadSize(int threadSize);

    // 不让用户拷贝，赋值
    ThreadPool(const ThreadPool &) = delete;

    ThreadPool &operator=(const ThreadPool &) = delete;

private:
    // 定义线程函数
    void threadFunc();

private:
    std::vector<std::unique_ptr<Thread>> threads_; // 线程列表
    size_t initThreadSize_;                        // 初始的线程数量
    PoolMode poolMode_;                            // 线程可以设置的模式

    bool isPoolRunning_;

    std::queue<std::function<void()>> tasksQue_; // 任务队列
    std::atomic_int task_Size_;                  // 任务的数量
    int taskQueMaxThreshHold_;                   // 任务队列上限的阈值

    std::mutex taskQueMtx_;            // 保证任务队列的线程安全
    std::condition_variable notFull_;  // 表示任务队列不满
    std::condition_variable notEmpty_; // 表示任务队列不空
    std::condition_variable allTaskDone_;

    size_t threadSizeThreshHold_; // 线程数量上限的阈值
    std::atomic_int curThreadSize_; // 当前线程数量
    std::atomic_int idleThreadSize_; // 空闲线程数量

};

#endif // THREADPOOL_THREADPOOL_H