//
// Created by 王强 on 2026/4/16.
//
#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TaskQueueMaxThreshHold = 1024;

// 线程池构造
ThreadPool::ThreadPool() : initThreadSize_(4),
                           poolMode_(PoolMode::MODE_FIXED),
                           task_Size_(0),
                           taskQueMaxThreshHold_(TaskQueueMaxThreshHold),
                           isPoolRunning_(false)
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

// 给线程池提交任务  用户调用submitTask提交任务对象
// bool ThreadPool::submitTask(std::shared_ptr<Task> task)
// {
//     // 获取锁
//     std::unique_lock<std::mutex> lock(taskQueMtx_);
//     // 如果线程池没有运行了，就不让提交任务了
//     if (!isPoolRunning_)
//     {
//         std::cerr << "thread pool is not running, submit task failed!" << std::endl;
//         return false;
//     }
//     // 如果在这里等待超过一秒钟还没提交上去，就判断提交任务失败，返回
//     if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool
//                            { return tasksQue_.size() < (size_t)taskQueMaxThreshHold_ || !isPoolRunning_; }))
//     {
//         // 表示notFull_等待一秒钟，条件依然没有满足
//         std::cerr << "task queue is full, submit task failed" << std::endl;
//         return false;
//     }
//     // 如果线程池没有运行了，就不让提交任务了
//     if (!isPoolRunning_)
//     {
//         std::cerr << "thread pool is not running, submit task failed!" << std::endl;
//         return false;
//     }

//     // 如果有空余，把任务放入任务队列中
//     tasksQue_.emplace([task]()
//                       { task->run(); }); // 这里把任务对象的run()方法放入任务队列中，线程池中的线程从任务队列里取出一个任务对象，就执行它的run()方法    
//     task_Size_++;
//     // 因为新放了任务，任务队列肯定不空了，在not_Empty上进行通知,赶快分配线程执行任务
//     notEmpty_.notify_one();
//     return true;
// }

// //设置初始的线程数量
// void ThreadPool::setInitThreadSize(int threadSize) {
//     initThreadSize_ = threadSize;
// }

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
    // std::cout << "begin ThreadPool::threadFunc() tid:" << std::this_thread::get_id() << std::endl;
    // std::cout << "end ThreadPool::threadFunc()" << std::endl;

    for (;;)
    {
        std::function<void()> task; // 定义一个任务对象，类型是函数对象，初始值是空的
        {
            // 先获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);
            // std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务..." << std::endl;

            // 等待notEmpty_条件
            notEmpty_.wait(lock, [&]() -> bool
                           { return !tasksQue_.empty() || !isPoolRunning_; });
            // 如果不空，那么就从任务队列中取一个任务出来

            if (!isPoolRunning_ && tasksQue_.empty())
            {
                // std::cout << "tid:" << std::this_thread::get_id() << "线程池已经关闭，且任务队列已经空了，线程退出！" << std::endl;
                return;
            }

            // std::cout << "tid:" << std::this_thread::get_id() << "任务获取成功！" << std::endl;
            task = tasksQue_.front();
            tasksQue_.pop();
            task_Size_--;

            // 取出一个任务，通知可以提交生产任务
            notFull_.notify_one();
        } // 出了这个花括号，就释放锁

        // 当前线程负责执行这个任务  使用多态，用基类指针指向它的run()方法
        if (task)
        {
            task();// 执行任务对象的run()方法
        }
    }
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
