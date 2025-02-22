//
// Created by lipei on 25-1-28.
//

#ifndef CLOUDCONFERENCE_TEST_H
#define CLOUDCONFERENCE_TEST_H

#include <iostream>
#include <vector>
#include <list>
#include "MsgQueue.h"
#include "ThreadPool.h"

using namespace std;
//lock_free_queue<int> lfq;

// 定义小数据结构
struct SmallItem {
    int data[20];
};

// 定义大数据结构
struct LargeItem {
    int data[1024];
};

// 测试参数
//const int NUM_THREADS = thread::hardware_concurrency();
const int NUM_THREADS = 2;
const int NUM_ITEMS = 2000;
ThreadPool<40> tp{static_cast<size_t>(NUM_THREADS)};

// 测试函数模板
template<typename T>
void testPerformance(std::atomic_flag& lock, std::list<T>& container, std::vector<T>& data) {
    vector<future<void>> vecfut{static_cast<size_t>(NUM_THREADS)};

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_THREADS; ++i) {
        vecfut[i] = tp.submit([&]() {
            for (const auto& item : data) {
                while (lock.test_and_set(std::memory_order_acquire));
                container.push_back(item);
                lock.clear(std::memory_order_release);
            }
        });
    }
    for (int i = 0; i < NUM_THREADS; ++i) {
        vecfut[i].get();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Push time: " << elapsed.count() << " seconds\n";

    vecfut.clear();

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_THREADS; ++i) {
        vecfut[i] = tp.submit([&]() {
            for (int j = 0; j < NUM_ITEMS / NUM_THREADS; ++j) {
                while (lock.test_and_set(std::memory_order_acquire));
                if (!container.empty()) {
                    container.pop_front();
                }
                lock.clear(std::memory_order_release);
            }
        });
    }
    for (int i = 0; i < NUM_THREADS; ++i) {
        vecfut[i].get();
    }

    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    std::cout << "Pop time: " << elapsed.count() << " seconds\n";
}

template<typename T>
void testPerformance(std::mutex& mtx, std::list<T>& container, std::vector<T>& data) {
    vector<future<void>> vecfut{static_cast<size_t>(NUM_THREADS)};

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_THREADS; ++i) {
        vecfut[i] = tp.submit([&]() {
            for (const auto& item : data) {
                std::lock_guard<std::mutex> lock(mtx);
                container.push_back(item);
            }
        });
    }
    for (int i = 0; i < NUM_THREADS; ++i) {
        vecfut[i].get();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Push time: " << elapsed.count() << " seconds\n";

    vecfut.clear();

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_THREADS; ++i) {
        vecfut[i] = tp.submit([&]() {
            for (int j = 0; j < NUM_ITEMS / NUM_THREADS; ++j) {
                std::lock_guard<std::mutex> lock(mtx);
                if (!container.empty()) {
                    container.pop_front();
                }
            }
        });
    }
    for (int i = 0; i < NUM_THREADS; ++i) {
        vecfut[i].get();
    }

    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    std::cout << "Pop time: " << elapsed.count() << " seconds\n";
}

template<typename T, size_t NUM_ITEMS>
void testPerformance(lock_free_queue<T, NUM_ITEMS>& queue, std::vector<T>& data) {
    vector<future<void>> vecfut{static_cast<size_t>(NUM_THREADS)};

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_THREADS; ++i) {
        vecfut[i] = tp.submit([&]() {
            for (const auto& item : data) {
                queue.push(item);
            }
        });
    }
    for (int i = 0; i < NUM_THREADS; ++i) {

        vecfut[i].get();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Push time: " << elapsed.count() << " seconds\n";

    vecfut.clear();

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_THREADS; ++i) {
        vecfut[i] = tp.submit([&]() {
            T item;
            for (int j = 0; j < NUM_ITEMS / NUM_THREADS; ++j) {
                queue.pop();
            }
        });
    }
    for (int i = 0; i < NUM_THREADS; ++i) {
        //vecfut[i].get();
    }

    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    std::cout << "Pop time: " << elapsed.count() << " seconds\n";
}

void test() {
    cout << NUM_THREADS << endl;

    std::mutex mtx;
    std::atomic_flag atomicFlag = ATOMIC_FLAG_INIT;

    std::vector<SmallItem> smallData(NUM_ITEMS);

    std::cout << "Testing with std::atomic_flag and SmallItem\n";
    std::list<SmallItem> smallList;
    testPerformance(atomicFlag, smallList, smallData);

    std::cout << "Testing with std::mutex and SmallItem\n";
    std::list<SmallItem> smallListMutex;
    testPerformance(mtx, smallListMutex, smallData);

    std::cout << "Testing with ConcurrentQueue and SmallItem\n";
    lock_free_queue<SmallItem, NUM_ITEMS * NUM_THREADS> smallQueue;
    testPerformance(smallQueue, smallData);

    std::vector<LargeItem> largeData(NUM_ITEMS);

    std::cout << "Testing with std::atomic_flag and LargeItem\n";
    std::list<LargeItem> largeList;
    testPerformance(atomicFlag, largeList, largeData);

    std::cout << "Testing with std::mutex and LargeItem\n";
    std::list<LargeItem> largeListMutex;
    testPerformance(mtx, largeListMutex, largeData);

    std::cout << "Testing with ConcurrentQueue and LargeItem\n";
    lock_free_queue<LargeItem, NUM_ITEMS * NUM_THREADS> largeQueue;
    testPerformance(largeQueue, largeData);
    //this_thread::sleep_for(5s);
}

#endif //CLOUDCONFERENCE_TEST_H
