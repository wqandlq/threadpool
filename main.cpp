#include "ThreadPool.h"
#include <iostream>

#include <thread>

int main() {

    ThreadPool threadPool;
    threadPool.start(6);

    // std::this_thread::sleep_for(std::chrono::seconds(5));
    std::cin.get();
    return 0;
}