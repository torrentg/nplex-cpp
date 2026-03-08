#pragma once

#include "nplex-cpp/client.hpp"

namespace nplex {

using logger_ptr = std::shared_ptr<logger>;

class loggable
{
  public:  // methods

    void set_logger(const logger_ptr &log) { m_logger = log; }

  protected:  // methods

    template<typename... Args>
    void log(logger::log_level_e severity, fmt::format_string<Args...> fmt_str, Args&&... args) {
        if (m_logger && static_cast<int>(m_logger->level()) <= static_cast<int>(severity))
            m_logger->log(severity, fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void log_trace(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(logger::log_level_e::TRACE, fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_debug(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(logger::log_level_e::DEBUG, fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_info(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(logger::log_level_e::INFO, fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_warn(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(logger::log_level_e::WARN, fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_error(fmt::format_string<Args...> fmt_str, Args&&... args) {
        log(logger::log_level_e::ERROR, fmt_str, std::forward<Args>(args)...);
    }

  protected:  // members

    logger_ptr m_logger;
};

} // namespace nplex
