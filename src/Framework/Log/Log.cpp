//
// Created by prodigg on 08.09.26.
//
#include "Log.h"

#include <iostream>

uint32_t sim::framework::LoggerHandler_t::attachLogConsumer(LogConsumer_t &logConsumer) {
    std::scoped_lock mux(m_logConsumerMutex);
    m_logConsumers.emplace_back(++m_currentLogConsumerID, logConsumer);
    return m_currentLogConsumerID;
}

void sim::framework::LoggerHandler_t::startLogDispatcherThread() {
    m_logDispatcherThread.emplace([this](const std::stop_token &stopToken) {
        while (!stopToken.stop_requested()) {
            dispatchLog();
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });
}

void sim::framework::LoggerHandler_t::writeLog(const LogLevel_t logLevel, const std::string_view logMessage,
                                               const nlohmann::json &additionalData, const std::source_location location) {
    std::scoped_lock mux(m_logQueueMutex);
    m_logQueue.emplace(logLevel, std::string(logMessage), additionalData, location, generateTimestamp());
}

void sim::framework::LoggerHandler_t::dispatchLog() {
    {
        std::scoped_lock mux(m_logQueueMutex);
        if (m_logConsumers.empty())
            return; // nothing to do, log is empty
        std::swap(m_logQueue, m_handleLogQueue);
    }

    std::scoped_lock mux(m_logConsumerMutex);
    while (!m_handleLogQueue.empty()) {
        auto&[logLevel, logMessage, additionalData, location, timestamp] = m_handleLogQueue.front();
        writeLogToConsumer(logLevel, logMessage, additionalData, location, timestamp);
        m_handleLogQueue.pop();
    }
}

std::string sim::framework::LoggerHandler_t::generateTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};

    localtime_r(&time, &tm);

    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ) % 1000;

    std::ostringstream timestamp;
    timestamp
        << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << '.'
        << std::setfill('0')
        << std::setw(3)
        << milliseconds.count();

    return timestamp.str();
}

void sim::framework::LoggerHandler_t::writeLogToConsumer(LogLevel_t logLevel, std::string_view logMessage,
    const nlohmann::json &additionalData, std::source_location location, std::string_view timestamp) {
    for (const auto &[logConsumerID, logConsumer]: m_logConsumers) {
        logConsumer.writeLog(logLevel, logMessage, additionalData, location, timestamp);
    }
}



void sim::framework::StdOutLogConsumer_t::writeLog(
    LogLevel_t logLevel,
    std::string_view logMessage,
    const nlohmann::json& additionalData,
    const std::source_location location,
    std::string_view timestamp)
{
    constexpr auto ANSI_RESET = "\033[0m";
    constexpr auto ANSI_BOLD  = "\033[1m";

    constexpr auto ANSI_CYAN        = "\033[36m";
    constexpr auto ANSI_BLUE        = "\033[34m";
    constexpr auto ANSI_GREEN       = "\033[32m";
    constexpr auto ANSI_YELLOW      = "\033[33m";
    constexpr auto ANSI_RED         = "\033[31m";
    constexpr auto ANSI_BRIGHT_RED  = "\033[91m";

    constexpr auto TRACE     = "[TRACE   ]";
    constexpr auto DEBUG     = "[DEBUG   ]";
    constexpr auto INFO      = "[INFO    ]";
    constexpr auto WARNING   = "[WARNING ]";
    constexpr auto ERROR     = "[ERROR   ]";
    constexpr auto CRITICAL  = "[CRITICAL]";

    const char* levelText = nullptr;
    const char* levelColor = nullptr;

    switch (logLevel)
    {
        case LogLevel_t::TRACE:
            levelText = TRACE;
            levelColor = ANSI_CYAN;
            break;

        case LogLevel_t::DEBUG:
            levelText = DEBUG;
            levelColor = ANSI_BLUE;
            break;

        case LogLevel_t::INFO:
            levelText = INFO;
            levelColor = ANSI_GREEN;
            break;

        case LogLevel_t::WARNING:
            levelText = WARNING;
            levelColor = ANSI_YELLOW;
            break;

        case LogLevel_t::ERROR:
            levelText = ERROR;
            levelColor = ANSI_RED;
            break;

        case LogLevel_t::CRITICAL:
            levelText = CRITICAL;
            levelColor = ANSI_BRIGHT_RED;
            break;
    }


    std::cout
        << timestamp
        << " "
        << levelColor
        << ANSI_BOLD
        << levelText
        << ANSI_RESET
        << " ["
        << location.file_name()
        << ":"
        << location.line()
        << " "
        << location.function_name()
        << "] "
        << logMessage;
    std::cout << std::endl;
}