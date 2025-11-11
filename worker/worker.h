#pragma once

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <list>
#include <functional>
#include <string>

class Worker
{
    std::mutex m_mtx;

    std::condition_variable m_cv; // work queue cv
    bool m_workAdded = false;

    std::condition_variable m_cvf; // flushing cv, no need for flag since we use a timeout for it

    std::atomic<bool> m_quit = false;
    std::atomic<bool> m_flush = false;

    size_t m_jobCount = 0;
    std::thread m_thread;
    std::list<std::pair<bool, std::function<void(void)>>> m_work{};
    std::string m_name;

    void workerFunction();

public:
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&&) = delete;
    Worker& operator=(Worker&&) = delete;

    Worker(std::string name, int priority);

    ~Worker();

    std::cv_status flush(uint32_t timeoutMs = 500);

    size_t getJobCount();

    void scheduleWork(std::function<void(void)> func, bool perpetual = false);
};
