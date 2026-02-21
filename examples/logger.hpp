#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <syncstream>
#include <thread>
#include <fmt/std.h>
#include "nplex-cpp/client.hpp"

class logger final : public nplex::logger
{
  public:  // methods

    logger(log_level_e level = log_level_e::INFO) : nplex::logger(level) {}

    void log([[maybe_unused]] nplex::client &cli, log_level_e severity, const std::string &msg) override
    {
        // Get the current time
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        // Format the time
        std::ostringstream time_stream;
        time_stream << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S")
                    << '.' << std::setfill('0') << std::setw(3) << now_ms.count();

        // Get the thread ID
        auto thread_id = std::this_thread::get_id();

        // Format the severity
        const char *str_severity = nullptr;
        switch (severity)
        {
            case log_level_e::TRACE: str_severity = "[TRACE]"; break;
            case log_level_e::DEBUG: str_severity = "[DEBUG]"; break;
            case log_level_e::INFO:  str_severity = "[INFO] "; break;
            case log_level_e::WARN:  str_severity = "[WARN] "; break;
            case log_level_e::ERROR: str_severity = "[ERROR]"; break;
            default:                 str_severity = "[PANIC]"; break;
        }

        // Format and print the log message
        std::osyncstream out(std::cout);
        fmt::print(out, "{} [{}] {} - {}\n", time_stream.str(), thread_id, str_severity, msg);
    }
};
