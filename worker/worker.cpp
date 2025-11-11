#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <list>
#include <functional>
#include <string>
#include <chrono>
#include <Windows.h>

#include "worker.h"
#include "utils/log/ILog.h"

Worker::Worker(std::string name, int priority)
    : m_name(std::move(name))
{
    m_thread = std::thread(&Worker::workerFunction, this);
    
    // Use try-catch to ensure thread is properly joined on exception
    try
    {
        if (!SetThreadPriority(m_thread.native_handle(), priority))
        {
            LOG_WARN("Failed to set thread priority to %d for thread '%s'", priority, m_name.c_str());
        }
        
        // Convert to wide string for Windows API
        std::wstring wname(m_name.begin(), m_name.end());
        SetThreadDescription(m_thread.native_handle(), wname.c_str());
    }
    catch (...)
    {
        // Ensure thread is properly stopped if setup fails
        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_quit = true;
            m_workAdded = true;
        }
        m_cv.notify_all();
        m_thread.join();
        throw;
    }
}

Worker::~Worker()
{
    {
        std::unique_lock<std::mutex> lock(m_mtx);
        m_quit = true; // set to true so that worker thread can exit its loop
        m_workAdded = true; // set to true so that worker thread exit its wait after the notify call
    }
    m_cv.notify_all(); // wake up thread
    m_thread.join(); // block until thread exits
}

void Worker::workerFunction()
{
    while (!m_quit)
    {
        std::unique_lock<std::mutex> lock(m_mtx);
        if (m_work.empty())
        {
            // Tell threads waiting on flush that we are done
            m_cvf.notify_all();

            // Check if there was work added or quit requested while the work queue was empty
            m_cv.wait(lock, [this] { return m_workAdded || m_quit; });
            m_workAdded = false;
        }
        else
        {
            // Move instead of copy to avoid expensive function object copy
            auto [perpetual, func] = std::move(m_work.front());
            m_work.pop_front();
            
            // Check flush status while holding the lock to avoid race condition
            bool shouldRequeue = perpetual && !m_flush.load();
            
            lock.unlock();
            // NOTE: No need to wrap this in the exception handler
            // since all internal workers are already executing within one.
            func();
            lock.lock();
            
            // Update job count and requeue if needed
            if (!shouldRequeue)
            {
                m_jobCount--;
            }
            else
            {
                // Back to the queue to execute again but after other workloads (if any)
                m_work.push_back({ perpetual, std::move(func) });
            }
        }
    }
}

std::cv_status Worker::flush(uint32_t timeoutMs)
{
    // Atomic swap to true and check that it was false
    if (m_flush.exchange(true))
    {
        // Another thread is already flushing - wait for that flush to complete
        // This is better than returning immediately with false success
        std::unique_lock<std::mutex> lock(m_mtx);
        auto result = m_cvf.wait_for(lock, std::chrono::milliseconds(timeoutMs), 
                                      [this]() { return m_work.empty() && !m_flush.load(); });
        return result ? std::cv_status::no_timeout : std::cv_status::timeout;
    }

    // We're the thread doing the flush
    std::unique_lock<std::mutex> lock(m_mtx);
    bool isTimeout = !m_cvf.wait_for(lock, std::chrono::milliseconds(timeoutMs), 
                                      [this]() { return m_work.empty(); });
    
    if (isTimeout)
    {
        LOG_WARN("Worker thread '%s' timed out", m_name.c_str());
    }
    
    m_flush = false;
    m_cvf.notify_all();  // Wake up any other threads waiting on flush
    
    return isTimeout ? std::cv_status::timeout : std::cv_status::no_timeout;
}

size_t Worker::getJobCount()
{
    std::unique_lock<std::mutex> lock(m_mtx);
    return m_jobCount;
}

void Worker::scheduleWork(std::function<void(void)> func, bool perpetual)
{
    std::unique_lock<std::mutex> lock(m_mtx);
    m_work.push_back({ perpetual, std::move(func) });
    m_workAdded = true;
    m_jobCount++;

    m_cv.notify_one();
}

