//
// Created by prodigg on 08.09.26.
//

#ifndef SIMULATEDFACTORY_LOGCONSUMER_H
#define SIMULATEDFACTORY_LOGCONSUMER_H
#include <source_location>
#include <string_view>
#include <nlohmann/json.hpp>


namespace sim::framework {
    enum class LogLevel_t {
        TRACE = 0,
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        CRITICAL
    };

    /*!
     * @brief the log consumer is a interface for any consumer and is used to write the logs
     */
    class LogConsumer_t {
    public:
        /*!
         * @brief write a log
         * @param logLevel
         * @param logMessage message to log
         * @param additionalData additional json data
         * @param location call location
         * @param timestamp timestamp of log message
         */
        virtual void writeLog(LogLevel_t logLevel, std::string_view logMessage, const nlohmann::json& additionalData, const std::source_location location, std::string_view timestamp) = 0;
        virtual ~LogConsumer_t() = default;


    protected:
        static const char *logLevelToString(const LogLevel_t logLevel) {
            switch (logLevel) {
                case LogLevel_t::TRACE:
                    return "TRACE";
                case LogLevel_t::DEBUG:
                    return "DEBUG";
                case LogLevel_t::INFO:
                    return "INFO";
                case LogLevel_t::WARNING:
                    return "WARNING";
                case LogLevel_t::ERROR:
                    return "ERROR";
                case LogLevel_t::CRITICAL:
                    return "CRITICAL";
                default:
                    return "UNKNOWN";
            }
        }
    };
}

#endif //SIMULATEDFACTORY_LOGCONSUMER_H
