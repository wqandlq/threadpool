//
// Created by 王强 on 2026/4/16.
//
#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TaskQueueMaxThreshHold = 1024;
const int ThreadSizeThreshHold = 1024;

// 线程池构造
ThreadPool::ThreadPool() : initThreadSize_(4),
                           poolMode_(PoolMode::MODE_FIXED),
                           task_Size_(0),
                           taskQueMaxThreshHold_(TaskQueueMaxThreshHold),
                           isPoolRunning_(false),
                           curThreadSize_(0),
                           idleThreadSize_(0)
{
}

// 线程池析构
ThreadPool::~ThreadPool()
{
    {
        // 线程池要销毁了，先把线程池的运行状态设置为false
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        isPoolRunning_ = false;
    }

    notEmpty_.notify_all(); // 通知所有线程池中的线程，线程池要关闭了，赶快退出
    notFull_.notify_all();  // 通知所有线程池中的线程，线程池要关闭了，赶快退出

    for (auto &thread : threads_)
    {
        thread->join(); // 等待线程池中的线程都执行完毕，线程池才能销毁
    }
}

// 设置线程池模式
void ThreadPool::setMode(PoolMode mode)
{
    // 如果线程池已经运行了，就不让设置模式了
    if (isPoolRunning_)
    {
        std::cerr << "thread pool is already running, can't set mode!" << std::endl;
        return;
    }
    poolMode_ = mode;
}

// 设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int taskQueMaxThreshHold)
{
    // 如果线程池已经运行了，就不让设置任务队列上限阈值了
    if (isPoolRunning_)
    {
        std::cerr << "thread pool is already running, can't set task queue max thresh hold!" << std::endl;
        return;
    }
    taskQueMaxThreshHold_ = taskQueMaxThreshHold;
}

void ThreadPool::setThreadSizeThreshHold(int theshHold)
{
    // 如果线程池已经运行了，就不让设置线程数量上限阈值了
    if (isPoolRunning_)
    {
        std::cerr << "thread pool is already running, can't set thread size thresh hold!" << std::endl;
        return;
    }
    threadSizeThreshHold_ = theshHold;
}

// 开启线程池
void ThreadPool::start(int initThreadSize)
{
    // 如果线程池已经运行了，就不让再次开启了
    if (isPoolRunning_)
    {
        std::cerr << "thread pool is already running!" << std::endl;
        return;
    }

    if (initThreadSize <= 0)
    {
        initThreadSize = 1;
    }
    // 记录初始线程个数
    initThreadSize_ = initThreadSize;
    isPoolRunning_ = true;

    curThreadSize_ = initThreadSize_;  // 当前线程数量
    idleThreadSize_ = initThreadSize_; // 空闲线程数量

    // 创建线程对象
    for (int i = 0; i < initThreadSize_; i++)
    {
        // 创建thread线程对象的时候，把线程函数给到thread对象
        std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
        threads_.emplace_back(std::move(ptr));
    }

    // 启动所有线程    std::vector<Thread *> threads_;
    for (int i = 0; i < initThreadSize_; i++)
    {
        threads_[i]->start(); // 线程启动需要去执行一个线程函数
    }
}

// 定义线程函数    线程池的所有线程从任务队列里消费任务
void ThreadPool::threadFunc()
{
    const int THREAD_MAX_IDLE_TIME = 60; // 线程最大空闲时间，单位是秒
    for (;;)
    {
        std::function<void()> task; // 定义一个任务对象，类型是函数对象，初始值是空的
        {
            // 先获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);
            if (poolMode_ == MODE_CACHED)
            {
                // 线程池是动态的，如果线程空闲时间超过了THREAD_MAX_IDLE_TIME，那么就让线程退出
                while (tasksQue_.empty() && isPoolRunning_)
                {
                    if (notEmpty_.wait_for(lock, std::chrono::seconds(THREAD_MAX_IDLE_TIME)) == std::cv_status::timeout)
                    {
                        // 如果线程空闲时间超过了THREAD_MAX_IDLE_TIME，那么就让线程退出
                        if (curThreadSize_ > initThreadSize_)
                        {
                            // 线程空闲时间超过了THREAD_MAX_IDLE_TIME，并且任务队列又空了，并且线程池又是运行的，那么就让线程退出
                            curThreadSize_--;  // 当前线程数量减1
                            idleThreadSize_--; // 空闲线程数量减1
                            return;
                        }
                    }
                }
            }
            else
            {
                // 线程池是固定的，如果任务队列为空了，并且线程池是运行的，那么就让线程等待在notEmpty_条件变量上，等待生产者生产任务了，消费者才有任务可消费了
                notEmpty_.wait(lock, [&]() -> bool
                               { return !tasksQue_.empty() || !isPoolRunning_; });
                // 如果线程池不运行了，并且任务队列又空了，那么就让线程退出
                if (!isPoolRunning_ && tasksQue_.empty())
                {
                    return;
                }
            }

            // 从任务队列中取出一个任务来执行
            task = tasksQue_.front();
            tasksQue_.pop();
            task_Size_--;
            idleThreadSize_--; // 空闲线程数量减1

            // 取出一个任务，通知可以提交生产任务
            notFull_.notify_one();
        } // 出了这个花括号，就释放锁

        if (task)
        {
            task(); // 执行任务
            std::unique_lock<std::mutex> lock(taskQueMtx_);
            idleThreadSize_++; // 执行完任务了，空闲线程数量加1
            if (task_Size_ == 0 && curThreadSize_ == idleThreadSize_)
            {
                allTaskDone_.notify_all(); // 如果任务数量为0了，并且空闲线程数量等于当前线程数量了，那么就通知等待所有任务完成的线程，所有任务都完成了
            }
        }
    }
}

void ThreadPool::waitAllTaskDone()
{
    // 等待所有任务完成，条件是任务队列为空了，并且任务数量为0了
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    // 等待所有任务完成的条件是：空闲线程数量等于当前线程数量，并且任务数量为0了
    allTaskDone_.wait(lock, [&]() -> bool
                      { return idleThreadSize_ == curThreadSize_ && task_Size_ == 0; });
}

/////////////////////////////线程方法实现
// 启动线程
void Thread::start()
{
    // 创建一个线程，执行一个线程函数
    thread_ = std::thread(func_);
}

void Thread::join()
{
    if (thread_.joinable())
    {
        thread_.join();
    }
}

// 线程构造
Thread::Thread(ThreadFunc func)
    : func_(func)
{
}

// 线程析构
Thread::~Thread()
{
}
