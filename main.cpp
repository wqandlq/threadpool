#include "threadpool.h"
#include <iostream>
#include <string>

int add(int a, int b)
{
    return a + b;
}

int main()
{
    ThreadPool pool;
    pool.start(3);

    auto f1 = pool.submitTask(add, 10, 20);

    auto f2 = pool.submitTask([](std::string s) {
        return s + " thread pool";
    }, std::string("hello"));

    auto f3 = pool.submitTask([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return 3.14;
    });

    std::cout << "f1 = " << f1.get() << std::endl;
    std::cout << "f2 = " << f2.get() << std::endl;
    std::cout << "f3 = " << f3.get() << std::endl;

    return 0;
}