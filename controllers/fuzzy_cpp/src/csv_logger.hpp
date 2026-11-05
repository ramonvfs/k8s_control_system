#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/*
 * Asynchronous CSV writer.
 *
 * Request handlers only append a preformatted line to an in-memory buffer
 * (O(1) under a short lock); a background thread swaps the buffer and
 * writes it to disk once per flush period. This keeps disk I/O off the
 * request path so logging does not inflate the measured service time.
 * Lines still in memory are written by close(), which runs on SIGTERM
 * (e.g. when Kubernetes scales the Deployment down).
 */
class CsvLogger {
public:
    ~CsvLogger() { close(); }

    bool open(const std::string& path, const std::string& header,
              std::chrono::milliseconds flush_period = std::chrono::milliseconds(1000)) {
        file_ = std::fopen(path.c_str(), "w");
        if (!file_) return false;
        std::fputs(header.c_str(), file_);
        std::fflush(file_);
        flush_period_ = flush_period;
        running_ = true;
        writer_ = std::thread([this] { run(); });
        return true;
    }

    void push(std::string line) {
        if (!file_) return;
        std::lock_guard<std::mutex> lock(mtx_);
        pending_.push_back(std::move(line));
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (!running_) return;
            running_ = false;
        }
        cv_.notify_one();
        writer_.join();
        std::fclose(file_);
        file_ = nullptr;
    }

private:
    void run() {
        std::vector<std::string> batch;
        std::unique_lock<std::mutex> lock(mtx_);
        while (true) {
            cv_.wait_for(lock, flush_period_, [this] { return !running_; });
            batch.swap(pending_);
            bool stop = !running_;
            lock.unlock();

            for (const auto& line : batch) std::fputs(line.c_str(), file_);
            std::fflush(file_);
            batch.clear();

            if (stop) return;
            lock.lock();
        }
    }

    std::FILE* file_ = nullptr;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::vector<std::string> pending_;
    std::thread writer_;
    std::chrono::milliseconds flush_period_{1000};
    bool running_ = false;
};
