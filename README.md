# C++ 线程池项目说明

## 项目简介

这是一个基于 C++11/14 标准库实现的线程池项目，支持：

- 固定线程数模式（`MODE_FIXED`）
- 动态扩缩容模式（`MODE_CACHED`）
- 基于模板的任务提交
- 基于 `std::future` 的异步结果获取
- 任务队列容量控制
- 线程池安全退出
- 等待全部任务执行完成


---

## 主要功能

### 1. 固定线程池模式

通过 `MODE_FIXED` 运行线程池，线程数量在启动后保持不变。

适合：

- 任务量相对稳定
- 更关注资源可控性
- 希望线程数量固定

### 2. 缓存线程池模式

通过 `MODE_CACHED` 运行线程池，线程池会根据任务压力动态创建新线程；多余线程在空闲超时后自动退出。

适合：

- 存在突发任务高峰
- 任务量波动明显
- 希望线程池具备弹性扩缩容能力

### 3. 任务异步提交与返回值获取

支持提交任意可调用对象：

- 普通函数
- Lambda 表达式
- 函数对象
- 带参数任务

提交任务后可直接获得 `std::future`，通过 `future.get()` 获取执行结果。

### 4. 任务队列容量控制

支持配置任务队列上限。

当任务队列已满时：

- 提交线程会等待一段时间
- 超时仍无法提交则抛出异常

### 5. 安全退出

线程池析构时会：

1. 设置线程池停止标志
2. 唤醒阻塞中的线程
3. 等待所有线程退出

避免出现线程访问已析构对象的问题。

### 6. 等待所有任务执行完成

提供 `waitAllTaskDone()` 接口，用于显式等待当前线程池中的任务全部执行完毕。

---

## 项目结构

```text
threadpool.h   // 线程池声明与模板接口
threadpool.cpp // 线程池非模板实现
README.md      // 项目说明文档
```

---

## 核心类设计

### `Thread`

对 `std::thread` 做简单封装，负责：

- 启动线程
- 等待线程结束

### `ThreadPool`

线程池主体，负责：

- 管理线程集合
- 管理任务队列
- 提交任务
- 调度工作线程
- fixed / cached 模式切换
- 线程池生命周期管理

---

## 主要成员说明

### 线程相关

- `initThreadSize_`：初始线程数量
- `curThreadSize_`：当前线程总数量
- `idleThreadSize_`：当前空闲线程数量
- `threadSizeThreshHold_`：线程数量上限（cached 模式使用）

### 任务相关

- `tasksQue_`：任务队列，类型为 `std::queue<std::function<void()>>`
- `task_Size_`：当前任务数量
- `taskQueMaxThreshHold_`：任务队列上限

### 同步相关

- `taskQueMtx_`：任务队列互斥锁
- `notEmpty_`：任务队列非空条件变量
- `notFull_`：任务队列非满条件变量
- `allTaskDone_`：所有任务完成条件变量

### 线程池状态

- `poolMode_`：线程池模式（fixed / cached）
- `isPoolRunning_`：线程池是否运行

---

## 关键实现思路

## 1. 为什么任务队列使用 `std::function<void()>`

线程池内部不关心任务原始类型，只关心“能否执行”。

因此统一把任务抽象成：

```cpp
std::function<void()>
```

这样线程池内部只需要做两件事：

1. 从任务队列取任务
2. 执行 `task()`

这使得线程池可以支持任意 callable，而不依赖某个特定基类。

---

## 2. 为什么 `submitTask` 返回 `std::future`

任务由线程池异步执行，调用方不能立即拿到结果。

因此通过：

- `std::packaged_task`
- `std::future`

实现“后台执行、前台取结果”的模型。

基本过程：

1. 把用户传入的函数与参数绑定成任务
2. 用 `std::packaged_task` 包装任务
3. 通过 `get_future()` 拿到 `future`
4. 把执行 `packaged_task` 的 lambda 放入任务队列
5. 工作线程执行任务
6. 调用方通过 `future.get()` 获取结果

---

## 3. 为什么 `task()` 一定要在锁外执行

互斥锁只负责保护共享资源：

- 任务队列
- 任务数量
- 线程状态

真正执行任务可能是耗时操作，如果持锁执行：

- 其他线程无法取任务
- 提交线程无法提交任务
- 并发能力严重下降

因此线程池应当：

1. 在锁内完成取任务
2. 释放锁
3. 在锁外执行任务

---

## 4. fixed 模式和 cached 模式的区别

### fixed 模式

- 线程数量固定
- 逻辑更简单
- 资源更稳定

### cached 模式

- 线程数量可动态增加
- 任务压力增大时自动扩容
- 空闲线程超过设定时间后自动回收

扩容条件大致为：

- 当前模式为 `MODE_CACHED`
- 当前任务数大于空闲线程数
- 当前线程总数未达到线程上限

回收条件大致为：

- 当前模式为 `MODE_CACHED`
- 线程空闲等待超时
- 当前线程总数大于初始线程数

---

## 5. 为什么析构时不能使用分离线程

线程池中的工作线程会持续访问线程池内部成员：

- 任务队列
- 互斥锁
- 条件变量

如果线程使用 `detach()`，线程池析构后，后台线程仍可能访问已经被释放的对象，导致未定义行为。

因此线程池必须：

- 显式管理线程对象
- 在线程池销毁前通知线程退出
- 使用 `join()` 等待所有线程结束

---

## 使用示例

### 1. 固定线程池

```cpp
#include "threadpool.h"
#include <iostream>

int add(int a, int b)
{
    return a + b;
}

int main()
{
    ThreadPool pool;
    pool.setMode(MODE_FIXED);
    pool.start(4);

    auto f1 = pool.submitTask(add, 10, 20);
    auto f2 = pool.submitTask([]() {
        return 100;
    });

    std::cout << "f1 = " << f1.get() << std::endl;
    std::cout << "f2 = " << f2.get() << std::endl;

    pool.waitAllTaskDone();
    return 0;
}
```

### 2. 缓存线程池

```cpp
#include "threadpool.h"
#include <iostream>
#include <chrono>
#include <thread>

int main()
{
    ThreadPool pool;
    pool.setMode(MODE_CACHED);
    pool.setThreadSizeThreshHold(16);
    pool.start(2);

    for (int i = 0; i < 20; ++i)
    {
        pool.submitTask([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::cout << "task " << i << " done" << std::endl;
            return i * i;
        });
    }

    pool.waitAllTaskDone();
    return 0;
}
```

---

## 编译方式

如果你的文件名为：

- `threadpool.h`
- `threadpool.cpp`
- `main.cpp`

则可使用：

```bash
g++ -std=c++17 -pthread threadpool.cpp main.cpp -o main
./main
```

---

## 当前版本已实现能力总结

- [X]  固定线程池模式
- [X]  缓存线程池模式
- [X]  条件变量同步
- [X]  安全退出
- [X]  任务队列容量控制
- [X]  模板任务提交
- [X]  `future` 返回值获取
- [X]  等待所有任务完成

---

## 当前版本可继续优化的方向

### 1. 线程唯一标识管理

目前线程对象管理较轻量，可以进一步为线程分配唯一 id，便于日志和精细回收控制。

### 2. 更细粒度的任务完成统计

当前通过任务数和空闲线程数联合判断“任务全部完成”，后续可以引入“正在执行任务数”做更精细统计。

### 3. 支持线程池关闭接口

目前通过析构自动关闭，后续可以增加显式 `stop()` 接口。

### 4. 更完整的异常处理

当前提交失败通过异常抛出，后续可以进一步补充更细的错误分类。

### 5. 更工程化的参数配置

例如：

- 最大空闲时间可配置
- 最大线程数可配置
- 默认任务队列大小可配置
